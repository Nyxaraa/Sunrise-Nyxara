#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {
namespace {

constexpr std::size_t kAlignment = alignof(std::max_align_t);
constexpr std::size_t kHeaderSize = sizeof(ArenaBlock);

static_assert(kHeaderSize % kAlignment == 0);

[[nodiscard]] bool aligned_size(std::size_t value, std::size_t& output) noexcept {
    if (value > (std::numeric_limits<std::size_t>::max)() - (kAlignment - 1)) {
        return false;
    }
    output = (value + kAlignment - 1) & ~(kAlignment - 1);
    return true;
}

[[nodiscard]] std::byte* arena_begin(Arena& arena) noexcept {
    return arena.bytes.get();
}

[[nodiscard]] const std::byte* arena_end(const Arena& arena) noexcept {
    return arena.bytes.get() + arena.capacity;
}

[[nodiscard]] ArenaBlock* first_block(Arena& arena) noexcept {
    return reinterpret_cast<ArenaBlock*>(arena_begin(arena));
}

[[nodiscard]] ArenaBlock* next_block(Arena& arena, ArenaBlock& block) noexcept {
    std::byte* const next = reinterpret_cast<std::byte*>(&block) + kHeaderSize + block.size;
    return next < arena_end(arena) ? reinterpret_cast<ArenaBlock*>(next) : nullptr;
}

[[nodiscard]] bool valid_block(const Arena& arena, const ArenaBlock& block) noexcept {
    const std::byte* const begin = reinterpret_cast<const std::byte*>(&block);
    return begin >= arena.bytes.get() && begin + kHeaderSize <= arena_end(arena)
           && block.size <= static_cast<std::size_t>(arena_end(arena) - begin - kHeaderSize);
}

[[nodiscard]] ArenaBlock* block_for_pointer(Arena& arena, void* pointer) noexcept {
    if (pointer == nullptr) {
        return nullptr;
    }
    std::byte* const payload = static_cast<std::byte*>(pointer);
    if (payload < arena_begin(arena) + kHeaderSize || payload >= arena_end(arena)) {
        return nullptr;
    }
    ArenaBlock* const block = reinterpret_cast<ArenaBlock*>(payload - kHeaderSize);
    return valid_block(arena, *block) ? block : nullptr;
}

[[nodiscard]] ArenaBlock* previous_block(Arena& arena, ArenaBlock& block) noexcept {
    std::byte* const begin = reinterpret_cast<std::byte*>(&block);
    if (block.previous == 0 || begin == arena_begin(arena)) {
        return nullptr;
    }
    return reinterpret_cast<ArenaBlock*>(begin - kHeaderSize - block.previous);
}

/** Records a block's payload size on its physical successor, which owns the back link. */
void publish_size(Arena& arena, ArenaBlock& block) noexcept {
    ArenaBlock* const next = next_block(arena, block);
    if (next != nullptr && valid_block(arena, *next)) {
        next->previous = block.size;
    }
}

/** Leaves the unused tail of one free block as its own free successor. */
void split(Arena& arena, ArenaBlock& block, std::size_t size) noexcept {
    const std::size_t remaining = block.size - size;
    if (remaining < kHeaderSize + kAlignment) {
        return;
    }
    std::byte* const address = reinterpret_cast<std::byte*>(&block) + kHeaderSize + size;
    auto* const rest = reinterpret_cast<ArenaBlock*>(address);
    rest->size = remaining - kHeaderSize;
    rest->previous = size;
    rest->free = true;
    block.size = size;
    publish_size(arena, *rest);
}

/** Takes the first free block at or after the rover. A scan from the start would be quadratic. */
[[nodiscard]] void* allocate_new(Arena& arena, std::size_t requested) noexcept {
    std::size_t size = 0;
    if (requested == 0 || !aligned_size(requested, size)) {
        return nullptr;
    }
    const std::size_t start = arena.rover < arena.capacity ? arena.rover : 0;
    std::size_t offset = start;
    bool wrapped = false;
    while (true) {
        auto* const block = reinterpret_cast<ArenaBlock*>(arena_begin(arena) + offset);
        if (offset >= arena.capacity || !valid_block(arena, *block)) {
            if (wrapped || start == 0) {
                return nullptr;
            }
            wrapped = true;
            offset = 0;
            continue;
        }
        if (block->free && block->size >= size) {
            split(arena, *block, size);
            block->free = false;
            arena.used += block->size;
            arena.highWater = (std::max)(arena.highWater, arena.used);
            arena.rover = offset + kHeaderSize + block->size;
            return reinterpret_cast<std::byte*>(block) + kHeaderSize;
        }
        offset += kHeaderSize + block->size;
        if (wrapped && offset > start) {
            return nullptr;
        }
    }
}

/** Merges one freed block with a free neighbour on either side. */
void merge_free(Arena& arena, ArenaBlock& block) noexcept {
    ArenaBlock* merged = &block;
    ArenaBlock* const next = next_block(arena, block);
    if (next != nullptr && valid_block(arena, *next) && next->free) {
        block.size += kHeaderSize + next->size;
        publish_size(arena, block);
    }
    ArenaBlock* const previous = previous_block(arena, block);
    if (previous != nullptr && valid_block(arena, *previous) && previous->free) {
        previous->size += kHeaderSize + block.size;
        publish_size(arena, *previous);
        merged = previous;
    }
    // The rover must never point inside a block that no longer starts there.
    arena.rover =
        static_cast<std::size_t>(reinterpret_cast<std::byte*>(merged) - arena_begin(arena));
}

void release(Arena& arena, void* pointer) noexcept {
    ArenaBlock* const block = block_for_pointer(arena, pointer);
    if (block == nullptr || block->free) {
        return;
    }
    arena.used -= (std::min)(arena.used, block->size);
    block->free = true;
    merge_free(arena, *block);
}

} // namespace

/** Takes one block from the heap and lays it out as a single free maximum-aligned span. */
bool arena_initialize(Arena& arena) noexcept {
    // operator new[] returns at least this alignment, which is what every payload here needs.
    static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= kAlignment);
    arena.used = 0;
    arena.highWater = 0;
    arena.initialized = false;
    if (arena.bytes == nullptr) {
        // Call the allocation function directly: Clang's optimized Windows build folds the
        // nothrow byte-array new-expression into null, preventing every mission from opening.
        // The matching byte-array deleter still releases this storage through operator delete[].
        auto* const block =
            static_cast<std::byte*>(::operator new[](kArenaByteCapacity, std::nothrow));
        if (block == nullptr) {
            arena.capacity = 0;
            return false;
        }
        arena.bytes.reset(block);
        arena.capacity = kArenaByteCapacity;
    }
    ArenaBlock* const block = first_block(arena);
    block->size = arena.capacity - kHeaderSize;
    block->previous = 0;
    block->free = true;
    arena.rover = 0;
    arena.initialized = true;
    return true;
}

/** Frees the block. The high-water mark stays, because a caller reads it after the close. */
void arena_release(Arena& arena) noexcept {
    arena.initialized = false;
    arena.bytes.reset();
    arena.capacity = 0;
    arena.used = 0;
    arena.rover = 0;
}

/** Implements Lua's realloc contract without crossing the fixed arena boundary. */
void* arena_allocate(void* context,
                     void* pointer,
                     std::size_t oldSize,
                     std::size_t newSize) noexcept {
    (void)oldSize;
    auto* const arena = static_cast<Arena*>(context);
    if (arena == nullptr || !arena->initialized) {
        return nullptr;
    }
    if (newSize == 0) {
        release(*arena, pointer);
        return nullptr;
    }
    if (pointer == nullptr) {
        return allocate_new(*arena, newSize);
    }
    ArenaBlock* const block = block_for_pointer(*arena, pointer);
    if (block == nullptr || block->free) {
        return nullptr;
    }
    std::size_t aligned = 0;
    if (!aligned_size(newSize, aligned)) {
        return nullptr;
    }
    if (block->size >= aligned) {
        return pointer;
    }
    void* const replacement = allocate_new(*arena, newSize);
    if (replacement == nullptr) {
        return nullptr;
    }
    std::memcpy(replacement, pointer, (std::min)(block->size, newSize));
    release(*arena, pointer);
    return replacement;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
