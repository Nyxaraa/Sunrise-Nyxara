#include <algorithm>
#include <array>
#include <cstring>

#include "block_cache.h"

namespace sunrise::middleware::content::packages::reader {
namespace {

/** Buckets per cache slot, so an index stays under a one-in-two load. */
constexpr std::size_t kBucketsPerSlot = 2;
/** MurmurHash3 finalizer multiplier, so buckets spread when only the high key bits differ. */
constexpr std::uint64_t kKeyMixMultiplier = 0xFF51AFD7ED558CCDULL;
/** MurmurHash3 finalizer shift that folds the high bits down before each multiply. */
constexpr std::uint8_t kKeyMixShift = 33;

/** @param key Cache key. @return The key spread across all 64 bits. */
[[nodiscard]] std::uint64_t mix_key(std::uint64_t key) noexcept {
    key ^= key >> kKeyMixShift;
    key *= kKeyMixMultiplier;
    key ^= key >> kKeyMixShift;
    return key;
}

/** @param index Sized index. @param key Cache key. @return Its bucket row. */
[[nodiscard]] std::size_t bucket_of(const SlotIndex& index, std::uint64_t key) noexcept {
    return static_cast<std::size_t>(mix_key(key)) & (index.buckets.size() - 1U);
}

} // namespace

/** Sizes one key-to-slot index and drops every chain it held. */
bool prepare_slot_index(SlotIndex& index, std::size_t slots) noexcept {
    std::size_t buckets = 1;
    while (buckets < slots * kBucketsPerSlot) {
        buckets <<= 1U;
    }
    try {
        index.buckets.assign(buckets, kNoSlot);
        index.links.assign(slots, kNoSlot);
    } catch (...) {
        index.buckets.clear();
        index.links.clear();
        return false;
    }
    return true;
}

/** Drops every chain of one index without releasing its storage. */
void clear_slot_index(SlotIndex& index) noexcept {
    std::fill(index.buckets.begin(), index.buckets.end(), kNoSlot);
    std::fill(index.links.begin(), index.links.end(), kNoSlot);
}

/** @return First slot chained under one key, or kNoSlot. */
std::size_t slot_index_first(const SlotIndex& index, std::uint64_t key) noexcept {
    return index.buckets.empty() ? kNoSlot : index.buckets[bucket_of(index, key)];
}

/** @return Next slot sharing one slot's bucket, or kNoSlot. */
std::size_t slot_index_next(const SlotIndex& index, std::size_t slot) noexcept {
    return slot < index.links.size() ? index.links[slot] : kNoSlot;
}

/** Chains one slot under a key. The slot must hold no key. */
void slot_index_insert(SlotIndex& index, std::uint64_t key, std::size_t slot) noexcept {
    if (index.buckets.empty() || slot >= index.links.size()) {
        return;
    }
    const std::size_t bucket = bucket_of(index, key);
    index.links[slot] = index.buckets[bucket];
    index.buckets[bucket] = slot;
}

/** Unchains one slot from the key it was chained under. */
void slot_index_erase(SlotIndex& index, std::uint64_t key, std::size_t slot) noexcept {
    if (index.buckets.empty() || slot >= index.links.size()) {
        return;
    }
    const std::size_t bucket = bucket_of(index, key);
    if (index.buckets[bucket] == slot) {
        index.buckets[bucket] = index.links[slot];
        index.links[slot] = kNoSlot;
        return;
    }
    for (std::size_t held = index.buckets[bucket]; held != kNoSlot; held = index.links[held]) {
        if (index.links[held] == slot) {
            index.links[held] = index.links[slot];
            index.links[slot] = kNoSlot;
            return;
        }
    }
}

} // namespace sunrise::middleware::content::packages::reader

namespace sunrise::middleware::content::packages::reader::block_cache {
namespace {

/** Reads one typed array prefix whose data begins at `dataOffset`. */
[[nodiscard]] bool read_table_prefix(const Path& path,
                                     Scratch& scratch,
                                     std::uint64_t dataOffset,
                                     std::uint32_t expectedClass,
                                     std::uint64_t minimumCount,
                                     std::uint64_t maximumCount,
                                     std::uint64_t& physicalCount) noexcept {
    physicalCount = 0;
    if (dataOffset < layout::kTableArrayPrefixSize) {
        return false;
    }
    std::array<std::byte, layout::kTableArrayPrefixSize> bytes{};
    if (!read_at(scratch, path, dataOffset - bytes.size(), bytes)) {
        return false;
    }
    std::uint32_t marker = 0;
    std::uint32_t elementClass = 0;
    std::memcpy(&marker, bytes.data(), sizeof marker);
    std::memcpy(&physicalCount, bytes.data() + sizeof marker, sizeof physicalCount);
    std::memcpy(
        &elementClass, bytes.data() + sizeof marker + sizeof physicalCount, sizeof elementClass);
    if (marker != layout::kTableArrayMarker || elementClass != expectedClass
        || physicalCount < minimumCount || physicalCount > maximumCount) {
        physicalCount = 0;
        return false;
    }
    return true;
}

/** Resolves the block table from the physical entry-region count and checks its own prefix. */
[[nodiscard]] bool
resolve_block_table(const Path& path, Scratch& scratch, Header& header) noexcept {
    std::uint64_t physicalEntries = 0;
    if (!read_table_prefix(path,
                           scratch,
                           header.entryTable,
                           layout::kEntryTableElementClass,
                           header.entryCount,
                           kEntryCapacity,
                           physicalEntries)) {
        return false;
    }
    header.blockTable =
        header.entryTable + physicalEntries * sizeof(layout::EntryRecord) + layout::kBlockTableGap;
    std::uint64_t physicalBlocks = 0;
    return read_table_prefix(path,
                             scratch,
                             header.blockTable,
                             layout::kBlockTableElementClass,
                             header.blockCount,
                             kBlockCapacity,
                             physicalBlocks);
}

} // namespace

/**
 * Builds the cache key of one package block.
 * @param packageId Package id from the tag handle.
 * @param record Block-table record.
 * @return A value unique to that block of that package patch.
 */
std::uint64_t key_of(std::uint16_t packageId, const layout::BlockRecord& record) noexcept {
    return (static_cast<std::uint64_t>(packageId) << 48U)
           | (static_cast<std::uint64_t>(record.patchId) << 32U) | record.offset;
}

/**
 * Reports the cached copy of one block.
 * @param scratch Lock-owned block storage.
 * @param key Block cache key.
 * @param plaintext Receives the view of the cached block.
 * @return True when the block is cached.
 */
bool find(Scratch& scratch, std::uint64_t key, std::span<const std::byte>& plaintext) noexcept {
    for (std::size_t held = slot_index_first(scratch.blockIndex, key); held != kNoSlot;
         held = slot_index_next(scratch.blockIndex, held)) {
        BlockSlot& slot = scratch.blocks[held];
        if (slot.valid && slot.key == key) {
            plaintext = std::span<const std::byte>(slot.bytes.data(), slot.size);
            ++scratch.blockHits;
            return true;
        }
    }
    ++scratch.blockMisses;
    return false;
}
/**
 * Keeps one decoded block and reports the kept copy.
 * @param scratch Lock-owned block storage.
 * @param key Block cache key.
 * @param decoded Whole decoded block.
 * @param plaintext Receives the view of the kept block.
 */
void store(Scratch& scratch,
           std::uint64_t key,
           std::span<const std::byte> decoded,
           std::span<const std::byte>& plaintext) noexcept {
    plaintext = decoded;
    scratch.blockBytes += decoded.size();
    if (scratch.blocks.empty() && !prepare_blocks(scratch, kBlockCacheSlots)) {
        return;
    }
    // Replacement rotates rather than searching for the least recent, so a cache of thousands
    // of slots costs the same per store as a cache of eight.
    if (scratch.blockCursor >= scratch.blocks.size()) {
        scratch.blockCursor = 0;
    }
    BlockSlot& target = scratch.blocks[scratch.blockCursor];
    if (decoded.size() > target.bytes.size()) {
        return;
    }
    if (target.valid) {
        slot_index_erase(scratch.blockIndex, target.key, scratch.blockCursor);
    }
    std::copy(decoded.begin(), decoded.end(), target.bytes.begin());
    target.size = decoded.size();
    target.key = key;
    target.used = ++scratch.useCounter;
    target.valid = true;
    slot_index_insert(scratch.blockIndex, key, scratch.blockCursor);
    ++scratch.blockCursor;
    plaintext = std::span<const std::byte>(target.bytes.data(), target.size);
}

/**
 * Reads one package header, from the header cache when it is already parsed.
 * @param path Full package path.
 * @param packageId Package id from the tag handle.
 * @param patchIndex Patch index the path names.
 * @param scratch Lock-owned block storage.
 * @param header Receives the header fields.
 * @return True when the header reads and parses.
 */
bool load_header(const Path& path,
                 std::uint16_t packageId,
                 std::uint32_t patchIndex,
                 Scratch& scratch,
                 Header& header) noexcept {
    const std::uint64_t key = (static_cast<std::uint64_t>(packageId) << 32U) | patchIndex;
    // The cache holds only the table offsets, so identity has to come from the caller. Leaving
    // it zero made every package share one table-cache key.
    header.packageId = packageId;
    header.patchId = static_cast<std::uint16_t>(patchIndex);
    for (HeaderSlot& slot : scratch.headers) {
        if (slot.valid && slot.key == key) {
            header.entryCount = slot.entryCount;
            header.blockCount = slot.blockCount;
            header.entryTable = slot.entryTable;
            header.blockTable = slot.blockTable;
            return true;
        }
    }
    std::array<std::byte, layout::kHeaderSize> headerBytes{};
    if (!read_at(scratch, path, 0, headerBytes) || !parse_header(headerBytes, header)
        || !resolve_block_table(path, scratch, header)) {
        return false;
    }
    HeaderSlot& slot = scratch.headers[key % scratch.headers.size()];
    slot.key = key;
    slot.entryCount = header.entryCount;
    slot.blockCount = header.blockCount;
    slot.entryTable = header.entryTable;
    slot.blockTable = header.blockTable;
    slot.valid = true;
    return true;
}

} // namespace sunrise::middleware::content::packages::reader::block_cache

namespace sunrise::middleware::content::packages::reader {
/** Sizes one reader's block cache and drops whatever it held. */
bool prepare_blocks(Scratch& scratch, std::size_t slots) noexcept {
    const std::size_t wanted = slots == 0 ? kBlockCacheSlots : slots;
    scratch.blockCursor = 0;
    if (!prepare_slot_index(scratch.blockIndex, wanted)) {
        return false;
    }
    try {
        scratch.blocks.clear();
        scratch.blocks.shrink_to_fit();
        scratch.blocks.resize(wanted);
    } catch (...) {
        scratch.blocks.clear();
        return false;
    }
    return true;
}
/** Sizes one reader's package-table cache and drops whatever it held. */
bool prepare_tables(Scratch& scratch, std::size_t slots) noexcept {
    const std::size_t wanted = slots == 0 ? kTableSlots : slots;
    scratch.tableCursor = 0;
    if (!prepare_slot_index(scratch.tableIndex, wanted)) {
        return false;
    }
    try {
        scratch.tables.clear();
        scratch.tables.shrink_to_fit();
        scratch.tables.resize(wanted);
    } catch (...) {
        scratch.tables.clear();
        return false;
    }
    return true;
}
} // namespace sunrise::middleware::content::packages::reader
