// Run against the production allocator with Release optimization enabled.
#include <cstdio>
#include <cstring>
#include "server/activity/mission/mission_script_vm_internal.h"

int main() {
    using namespace sunrise::server::activity::mission::lua_vm;
    detail::Arena arena{};
    for (int cycle = 0; cycle < 3; ++cycle) {
        if (!detail::arena_initialize(arena) || arena.capacity != kArenaByteCapacity) return 1;
        void* small = detail::arena_allocate(&arena, nullptr, 0, 1024);
        if (!small) return 2;
        std::memset(small, 0x5A, 1024);
        void* grown = detail::arena_allocate(&arena, small, 1024, 8192);
        if (!grown) return 3;
        for (int i = 0; i < 1024; ++i) {
            if (static_cast<unsigned char*>(grown)[i] != 0x5A) return 4;
        }
        if (detail::arena_allocate(&arena, nullptr, 0, kArenaByteCapacity) != nullptr) return 5;
        detail::arena_allocate(&arena, grown, 8192, 0);
        if (arena.used != 0) return 6;
        void* large = detail::arena_allocate(&arena, nullptr, 0, kArenaByteCapacity / 2);
        if (!large) return 7;
        detail::arena_release(arena);
        if (arena.bytes || arena.capacity || arena.used || arena.initialized) return 8;
    }
    std::puts("production arena allocation, growth, bounds, coalescing and reopen passed");
}
