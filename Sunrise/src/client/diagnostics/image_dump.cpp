#include "image_dump.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"

namespace sunrise::client::diagnostics {
namespace {

/** Dumps are isolated below the shared generated-artifact directory, beside the logs. */
constexpr std::wstring_view kDumpDirectorySuffix = L"\\dumps";
/** One stable name, so a second diagnostic run replaces the first rather than filling the disk. */
constexpr std::wstring_view kImageFileSuffix = L"\\game_image.bin";
/** The manifest carries the load base, without which the dump's addresses mean nothing. */
constexpr std::wstring_view kManifestFileSuffix = L"\\game_image.txt";
/**
 * Bytes moved per read.
 * Reads are page-granular in effect, so this only bounds the staging buffer and the cost of one
 * failed read. 64 KiB keeps the buffer off the stack-sized path while staying one allocation.
 */
constexpr std::size_t kChunkBytes = 64 * 1024;
/** A mapped image larger than this is not one this build can be looking at. */
constexpr std::size_t kMaximumImageBytes = 1024ULL * 1024ULL * 1024ULL;

/**
 * Creates one directory, tolerating an existing one.
 * @param path Full directory path.
 * @return True when the directory exists afterwards.
 */
[[nodiscard]] bool ensure_directory(const core::path::Buffer& path) noexcept {
    if (CreateDirectoryW(path.chars.data(), nullptr) != FALSE) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    // ERROR_ALREADY_EXISTS also covers files, so verify the existing object is a directory.
    const DWORD attributes = GetFileAttributesW(path.chars.data());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/**
 * Opens one file for writing, replacing anything already there.
 * @param path Full file path.
 * @return Open handle, or INVALID_HANDLE_VALUE.
 */
[[nodiscard]] HANDLE create_file(const core::path::Buffer& path) noexcept {
    return CreateFileW(path.chars.data(),
                       GENERIC_WRITE,
                       0,
                       nullptr,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       nullptr);
}

/**
 * Writes one whole buffer.
 * @param file Open file handle.
 * @param data First byte.
 * @param size Byte count.
 * @return True when every byte reached the file.
 */
[[nodiscard]] bool write_all(HANDLE file, const void* data, std::size_t size) noexcept {
    const auto* cursor = static_cast<const std::byte*>(data);
    std::size_t remaining = size;
    while (remaining != 0) {
        const DWORD wanted =
            static_cast<DWORD>(remaining < kChunkBytes ? remaining : kChunkBytes);
        DWORD written = 0;
        if (WriteFile(file, cursor, wanted, &written, nullptr) == FALSE || written == 0) {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

/** Header fields the dump is described by, read once from the mapped image. */
struct ImageHeader {
    std::byte* base{};
    std::size_t imageSize{};
    std::uint16_t sectionCount{};
    std::size_t sectionOffset{};
};

/**
 * Reads the mapped PE headers of the main module.
 * @param output Receives the load base and image span.
 * @return True when the headers are a usable 64-bit PE.
 */
[[nodiscard]] bool read_header(ImageHeader& output) noexcept {
    output = {};
    auto* base = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return false;
    }
    const auto& dos = *reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return false;
    }
    const auto& nt = *reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos.e_lfanew);
    if (nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    const std::size_t imageSize = nt.OptionalHeader.SizeOfImage;
    if (imageSize == 0 || imageSize > kMaximumImageBytes) {
        return false;
    }
    output.base = base;
    output.imageSize = imageSize;
    output.sectionCount = nt.FileHeader.NumberOfSections;
    output.sectionOffset = static_cast<std::size_t>(dos.e_lfanew) + sizeof(DWORD)
                           + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    return true;
}

/**
 * Writes the flat image span, substituting zeroes for pages the process will not read.
 * @param file Open destination.
 * @param header Mapped image description.
 * @param unreadable Receives the byte count that had to be zero-filled.
 * @return True when the whole span was written.
 */
[[nodiscard]] bool
write_image(HANDLE file, const ImageHeader& header, std::size_t& unreadable) noexcept {
    unreadable = 0;
    static std::array<std::byte, kChunkBytes> chunk{};
    for (std::size_t offset = 0; offset < header.imageSize; offset += kChunkBytes) {
        const std::size_t remaining = header.imageSize - offset;
        const std::size_t wanted = remaining < kChunkBytes ? remaining : kChunkBytes;
        SIZE_T copied = 0;
        // ReadProcessMemory rather than memcpy: a guard or no-access page inside the image is
        // normal for a packed binary and must not fault the game we are dumping from.
        if (ReadProcessMemory(
                GetCurrentProcess(), header.base + offset, chunk.data(), wanted, &copied)
                == FALSE
            || copied != wanted) {
            chunk.fill(std::byte{});
            unreadable += wanted;
        }
        if (!write_all(file, chunk.data(), wanted)) {
            return false;
        }
    }
    return true;
}

/**
 * Writes the manifest naming the load base and every section.
 * @param file Open destination.
 * @param header Mapped image description.
 * @param unreadable Bytes the image pass had to zero-fill.
 * @return True when the manifest was written.
 */
[[nodiscard]] bool
write_manifest(HANDLE file, const ImageHeader& header, std::size_t unreadable) noexcept {
    std::array<char, 512> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "# Sunrise mapped-image dump of the running game.\r\n"
                                "# Load with the base below, e.g.  r2 -B 0x%llX game_image.bin\r\n"
                                "base=0x%llX\r\n"
                                "image_size=0x%zX\r\n"
                                "unreadable_bytes=%zu\r\n"
                                "sections=%u\r\n",
                                static_cast<unsigned long long>(
                                    reinterpret_cast<std::uintptr_t>(header.base)),
                                static_cast<unsigned long long>(
                                    reinterpret_cast<std::uintptr_t>(header.base)),
                                header.imageSize,
                                unreadable,
                                static_cast<unsigned>(header.sectionCount));
    if (written <= 0 || !write_all(file, line.data(), static_cast<std::size_t>(written))) {
        return false;
    }
    for (std::uint16_t index = 0; index < header.sectionCount; ++index) {
        const auto& section = *reinterpret_cast<const IMAGE_SECTION_HEADER*>(
            header.base + header.sectionOffset + index * sizeof(IMAGE_SECTION_HEADER));
        // The name field is not guaranteed to be null-terminated at 8 characters.
        std::array<char, IMAGE_SIZEOF_SHORT_NAME + 1> name{};
        for (std::size_t character = 0; character < IMAGE_SIZEOF_SHORT_NAME; ++character) {
            name[character] = static_cast<char>(section.Name[character]);
        }
        written = std::snprintf(line.data(),
                                line.size(),
                                "section name=%-8s va=0x%08lX size=0x%08lX flags=0x%08lX\r\n",
                                name.data(),
                                static_cast<unsigned long>(section.VirtualAddress),
                                static_cast<unsigned long>(section.Misc.VirtualSize),
                                static_cast<unsigned long>(section.Characteristics));
        if (written <= 0 || !write_all(file, line.data(), static_cast<std::size_t>(written))) {
            return false;
        }
    }
    return true;
}

/**
 * Reports the outcome of one dump attempt.
 * @param stage Step that decided the outcome.
 * @param succeeded Whether the dump completed.
 * @param bytes Image bytes written, or zero.
 * @param unreadable Bytes zero-filled because the page would not read.
 */
void report(const char* stage,
            bool succeeded,
            std::size_t bytes,
            std::size_t unreadable) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=image_dump stage=%s result=%s bytes=%zu unreadable=%zu",
                                      stage,
                                      succeeded ? "ok" : "fail",
                                      bytes,
                                      unreadable);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         succeeded ? core::log::Level::info : core::log::Level::error,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

/** Writes the game's mapped image to disk so it can be disassembled offline. */
bool dump_game_image(void* module) noexcept {
    ImageHeader header{};
    if (!read_header(header)) {
        report("header", false, 0, 0);
        return false;
    }
    core::path::Buffer directory{};
    if (!core::path::artifact_directory(module, directory)
        || !core::path::append(directory, kDumpDirectorySuffix) || !ensure_directory(directory)) {
        report("path", false, 0, 0);
        return false;
    }

    core::path::Buffer imagePath = directory;
    HANDLE file = core::path::append(imagePath, kImageFileSuffix) ? create_file(imagePath)
                                                                  : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        report("create", false, 0, 0);
        return false;
    }
    std::size_t unreadable = 0;
    const bool wrote = write_image(file, header, unreadable);
    CloseHandle(file);
    if (!wrote) {
        report("write", false, 0, unreadable);
        return false;
    }

    core::path::Buffer manifestPath = directory;
    file = core::path::append(manifestPath, kManifestFileSuffix) ? create_file(manifestPath)
                                                                 : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        report("manifest", false, header.imageSize, unreadable);
        return false;
    }
    const bool described = write_manifest(file, header, unreadable);
    CloseHandle(file);
    report(described ? "complete" : "manifest", described, header.imageSize, unreadable);
    return described;
}

} // namespace sunrise::client::diagnostics
