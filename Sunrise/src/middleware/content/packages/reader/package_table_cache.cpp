#include "package_table_cache.h"

#include <array>
#include <cwchar>

namespace sunrise::middleware::content::packages::reader::table_cache {
namespace {

/** @return The key a held slot was stored under. */
[[nodiscard]] std::uint64_t table_key_of(const TableSlot& slot) noexcept {
    return slot.key;
}

/** @param header Parsed package header. @return True when both tables fit a slot. */
[[nodiscard]] bool holdable(const Header& header) noexcept {
    return header.entryCount <= kEntryCapacity && header.blockCount <= kBlockCapacity;
}

/** @return The key naming one held package, which no two patches share. */
[[nodiscard]] std::uint64_t table_key(const Header& header) noexcept {
    return (static_cast<std::uint64_t>(header.packageId) << 32U)
           | static_cast<std::uint64_t>(header.patchId);
}

/**
 * Reports the slot holding one package, reading its tables when no slot holds them.
 * @param scratch The reader's own storage.
 * @param path Full package path.
 * @param header Parsed package header.
 * @return The slot, or null when the tables cannot be held or read.
 */
[[nodiscard]] TableSlot*
acquire(Scratch& scratch, const Path& path, const Header& header) noexcept {
    if (!holdable(header)) {
        return nullptr;
    }
    if (scratch.tables.empty() && !prepare_tables(scratch, kTableSlots)) {
        return nullptr;
    }
    const std::uint64_t key = table_key(header);
    for (std::size_t chained = slot_index_first(scratch.tableIndex, key); chained != kNoSlot;
         chained = slot_index_next(scratch.tableIndex, chained)) {
        TableSlot& held = scratch.tables[chained];
        if (held.occupied && held.key == key) {
            held.used = ++scratch.slotCounter;
            return &held;
        }
    }
    // Replacement rotates, so holding hundreds of packages costs the same per lookup as two.
    if (scratch.tableCursor >= scratch.tables.size()) {
        scratch.tableCursor = 0;
    }
    const std::size_t slot = scratch.tableCursor;
    TableSlot* const target = &scratch.tables[slot];
    if (target->occupied) {
        slot_index_erase(scratch.tableIndex, table_key_of(*target), slot);
    }
    ++scratch.tableCursor;
    const auto entryBytes = std::as_writable_bytes(
        std::span(target->entries).first(static_cast<std::size_t>(header.entryCount)));
    const auto blockBytes = std::as_writable_bytes(
        std::span(target->blocks).first(static_cast<std::size_t>(header.blockCount)));
    target->occupied = false;
    if (!read_at(scratch, path, header.entryTable, entryBytes)
        || !read_at(scratch, path, header.blockTable, blockBytes)) {
        return nullptr;
    }
    target->path = path;
    target->key = key;
    target->entryCount = header.entryCount;
    target->blockCount = header.blockCount;
    target->used = ++scratch.slotCounter;
    target->occupied = true;
    slot_index_insert(scratch.tableIndex, key, slot);
    return target;
}

} // namespace

/** Reports one entry record, holding the package's tables from the first use. */
bool entry_record(Scratch& scratch,
                  const Path& path,
                  const Header& header,
                  std::uint32_t index,
                  layout::EntryRecord& record) noexcept {
    record = {};
    if (index >= header.entryCount) {
        return false;
    }
    const TableSlot* slot = acquire(scratch, path, header);
    if (slot != nullptr) {
        record = slot->entries[index];
        return true;
    }
    // A package too wide to hold still reads, one record at a time.
    const std::uint64_t offset =
        header.entryTable + static_cast<std::uint64_t>(index) * sizeof record;
    return read_at(
        scratch, path, offset, std::as_writable_bytes(std::span{&record, std::size_t{1}}));
}

/** Reports one block record, holding the package's tables from the first use. */
bool block_record(Scratch& scratch,
                  const Path& path,
                  const Header& header,
                  std::uint32_t index,
                  layout::BlockRecord& record) noexcept {
    record = {};
    if (index >= header.blockCount) {
        return false;
    }
    const TableSlot* slot = acquire(scratch, path, header);
    if (slot != nullptr) {
        record = slot->blocks[index];
        return true;
    }
    const std::uint64_t offset =
        header.blockTable + static_cast<std::uint64_t>(index) * sizeof record;
    return read_at(
        scratch, path, offset, std::as_writable_bytes(std::span{&record, std::size_t{1}}));
}

} // namespace sunrise::middleware::content::packages::reader::table_cache
