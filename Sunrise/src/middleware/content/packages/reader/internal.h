#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "reader.h"

namespace sunrise::middleware::content::packages::reader {

/** Header fields the package readers use. */
struct Header {
    std::uint16_t version{};
    std::uint16_t packageId{};
    std::uint16_t patchId{};
    std::uint32_t entryCount{};
    std::uint32_t blockCount{};
    std::uint64_t entryTable{};
    std::uint64_t blockTable{};
};

/**
 * Sizes one key-to-slot index and drops every chain it held.
 * @param index Index to size.
 * @param slots Cache slots the index must cover.
 * @return True when the storage was reserved.
 */
[[nodiscard]] bool prepare_slot_index(SlotIndex& index, std::size_t slots) noexcept;

/** @param index Index whose chains are dropped, keeping the storage it already holds. */
void clear_slot_index(SlotIndex& index) noexcept;

/** @return First slot chained under one key, or kNoSlot. */
[[nodiscard]] std::size_t slot_index_first(const SlotIndex& index, std::uint64_t key) noexcept;

/** @return Next slot sharing one slot's bucket, or kNoSlot. */
[[nodiscard]] std::size_t slot_index_next(const SlotIndex& index, std::size_t slot) noexcept;

/**
 * Chains one slot under a key. The slot must hold no key.
 * @param index Index to change.
 * @param key Key the slot now holds.
 * @param slot Cache slot being chained.
 */
void slot_index_insert(SlotIndex& index, std::uint64_t key, std::size_t slot) noexcept;

/**
 * Unchains one slot from the key it was chained under.
 * @param index Index to change.
 * @param key Key the slot was chained under.
 * @param slot Cache slot being released.
 */
void slot_index_erase(SlotIndex& index, std::uint64_t key, std::size_t slot) noexcept;

/**
 * Parses the public header prefix.
 * @param bytes Whole header prefix.
 * @param header Receives the fields it reads.
 * @return True for a supported version whose table offsets fit.
 */
[[nodiscard]] bool parse_header(std::span<const std::byte, layout::kHeaderSize> bytes,
                                Header& header) noexcept;

/**
 * Parses the package id and patch index out of one leaf name.
 * @param fileName Whole leaf name.
 * @param packageId Receives the package id.
 * @param patchIndex Receives the patch index.
 * @return True when the leaf has that exact form.
 */
[[nodiscard]] bool parse_leaf(std::wstring_view fileName,
                              std::uint16_t& packageId,
                              std::uint32_t& patchIndex) noexcept;

/**
 * Finds the highest-patch file for one package id.
 * @param directory Installed packages directory.
 * @param packageId Package id from the tag handle.
 * @param stem Receives the file stem without its patch suffix and extension.
 * @param patchIndex Receives the highest patch index found.
 * @return True when at least one matching package exists.
 */
[[nodiscard]] bool find_latest(std::wstring_view directory,
                               std::uint16_t packageId,
                               Path& stem,
                               std::uint32_t& patchIndex) noexcept;

/**
 * Resolves one package through storage owned by this reader.
 * The first package read reaches the shared directory catalog; later reads are lock-free.
 * @param scratch Reader-local location storage.
 * @param directory Installed packages directory, immutable while this Scratch is in use.
 * @param packageId Package id from the tag handle.
 * @param output Receives the cached location on success.
 * @return True when at least one matching package exists.
 */
[[nodiscard]] bool resolve_latest(Scratch& scratch,
                                  std::wstring_view directory,
                                  std::uint16_t packageId,
                                  const PackageLocation*& output) noexcept;

/**
 * Builds the full path of one patch of a package stem.
 * @param stem Stem produced by find_latest.
 * @param patchIndex Requested patch index.
 * @param path Receives the full path.
 * @return True when the path fits fixed storage.
 */
[[nodiscard]] bool build_path(const Path& stem, std::uint32_t patchIndex, Path& path) noexcept;

/** @param scratch Reader whose held package locations are dropped. */
void release_locations(Scratch& scratch) noexcept;

/**
 * Reads an exact byte range from one file, through files no reader owns.
 * The class sweeps use this. It locks, so nothing on a parallel read path may call it.
 * @param path Full package path.
 * @param offset File offset.
 * @param output Exact destination.
 * @return True when every requested byte is read.
 */
[[nodiscard]] bool
read_at(const Path& path, std::uint64_t offset, std::span<std::byte> output) noexcept;

/**
 * Reads an exact byte range through the files one reader keeps open.
 * Nothing here is shared, so several readers run this at once without locking.
 * @param scratch The reader's own storage.
 * @param path Full package path.
 * @param offset File offset.
 * @param output Exact destination.
 * @return True when every requested byte is read.
 */
[[nodiscard]] bool read_at(Scratch& scratch,
                           const Path& path,
                           std::uint64_t offset,
                           std::span<std::byte> output) noexcept;

} // namespace sunrise::middleware::content::packages::reader
