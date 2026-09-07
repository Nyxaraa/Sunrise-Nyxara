#include "path.h"

#include <Windows.h>

#include <cstring>

namespace sunrise::core::path {
namespace {

/** One owned folder prevents generated files from accumulating beside game binaries. */
constexpr std::wstring_view kArtifactDirectorySuffix = L"Sunrise";

} // namespace

/** Replaces one fixed path without truncation. */
bool assign(Buffer& path, std::wstring_view value) noexcept {
    if (value.size() >= path.chars.size()) {
        return false;
    }
    path = {};
    std::memcpy(path.chars.data(), value.data(), value.size() * sizeof(wchar_t));
    path.length = value.size();
    path.chars[path.length] = L'\0';
    return true;
}

/** Resolves a loaded module path and trims it to the containing directory. */
bool module_directory(void* module, Buffer& output) noexcept {
    if (module == nullptr) {
        return false;
    }

    const DWORD copied = GetModuleFileNameW(
        static_cast<HMODULE>(module), output.chars.data(), static_cast<DWORD>(output.chars.size()));
    if (copied == 0 || copied == output.chars.size()) {
        return false;
    }

    output.length = copied;
    while (output.length != 0 && output.chars[output.length - 1] != L'\\') {
        --output.length;
    }
    if (output.length == 0) {
        return false;
    }
    output.chars[output.length] = L'\0';
    return true;
}

/** Resolves and creates the shared module-relative root for every generated artifact. */
bool artifact_directory(void* module, Buffer& output) noexcept {
    if (!module_directory(module, output) || !append(output, kArtifactDirectorySuffix)) {
        return false;
    }
    if (CreateDirectoryW(output.chars.data(), nullptr) != FALSE) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    // ERROR_ALREADY_EXISTS also covers files, so verify the existing object is a directory.
    const DWORD attributes = GetFileAttributesW(output.chars.data());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/** Resolves one Sunrise-owned file beside this DLL. */
bool artifact_file(std::wstring_view relative, Buffer& output) noexcept {
    HMODULE self{};
    // From this function's own address, so it names the DLL rather than the host executable. The
    // two differ: the game sits in the install root and this module in `bin\x64`.
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&artifact_file),
                           &self)
            == FALSE
        || self == nullptr) {
        return false;
    }
    if (!artifact_directory(self, output)) {
        return false;
    }
    // Create each directory named before the file, so `exports\x.txt` works on a fresh install.
    std::size_t start = 0;
    for (std::size_t index = 0; index < relative.size(); ++index) {
        if (relative[index] != L'\\') {
            continue;
        }
        if (!append(output, L"\\") || !append(output, relative.substr(start, index - start))) {
            return false;
        }
        if (CreateDirectoryW(output.chars.data(), nullptr) == FALSE
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        start = index + 1;
    }
    return append(output, L"\\") && append(output, relative.substr(start));
}

/** Appends a path suffix without exceeding fixed storage. */
bool append(Buffer& path, std::wstring_view suffix) noexcept {
    if (path.length + suffix.size() >= path.chars.size()) {
        return false;
    }
    std::memcpy(path.chars.data() + path.length, suffix.data(), suffix.size() * sizeof(wchar_t));
    path.length += suffix.size();
    path.chars[path.length] = L'\0';
    return true;
}

} // namespace sunrise::core::path
