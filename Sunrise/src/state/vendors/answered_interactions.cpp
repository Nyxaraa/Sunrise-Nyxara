#include "answered_interactions.h"

#include <array>
#include <atomic>

#include "../build_data/vendors/definition.h"

namespace sunrise::state::vendors {

// The list is indexed by the installed vendor index, so it has to span the same range the index
// itself does. One that fell short would simply stop answering for the vendors past its end, and
// nothing else would say so.
static_assert(kVendorCapacity >= build_data::vendors::kIndexCapacity);

namespace {

/** Answered interactions, packed as `vendorIndex << 16 | interactionIndex`. Append only. */
std::array<std::atomic<std::uint32_t>, kAnsweredCapacity> g_answered{};
std::atomic<std::size_t> g_answeredCount{0};

/** Interaction each vendor is showing, so an answered one can be named afterwards. */
std::array<std::atomic<std::uint16_t>, kVendorCapacity> g_shown{};

/** @param vendorIndex Vendor row. @param interactionIndex Interaction row. @return Packed key. */
[[nodiscard]] constexpr std::uint32_t pack(std::uint16_t vendorIndex,
                                           std::uint16_t interactionIndex) noexcept {
    return (static_cast<std::uint32_t>(vendorIndex) << 16) | interactionIndex;
}

/** @param key Packed pair. @return True while the pair is answered. */
[[nodiscard]] bool contains(std::uint32_t key) noexcept {
    const std::size_t held = g_answeredCount.load(std::memory_order_acquire);
    for (std::size_t slot = 0; slot < held; ++slot) {
        if (g_answered[slot].load(std::memory_order_relaxed) == key) {
            return true;
        }
    }
    return false;
}

} // namespace

/** Records the interaction one vendor is showing right now. */
void record_shown(std::uint16_t vendorIndex, std::uint16_t interactionIndex) noexcept {
    if (vendorIndex < kVendorCapacity) {
        g_shown[vendorIndex].store(interactionIndex, std::memory_order_relaxed);
    }
}

/** Answers whether one interaction has been answered this session. */
bool is_answered(std::uint16_t vendorIndex, std::uint16_t interactionIndex) noexcept {
    return vendorIndex < kVendorCapacity && contains(pack(vendorIndex, interactionIndex));
}

/** Marks the interaction one vendor is showing right now as answered. */
bool answer_shown(std::uint16_t vendorIndex) noexcept {
    if (vendorIndex >= kVendorCapacity) {
        return false;
    }
    const std::uint16_t shown = g_shown[vendorIndex].load(std::memory_order_relaxed);
    if (shown == kAbsentIndex) {
        return false;
    }
    const std::uint32_t key = pack(vendorIndex, shown);
    if (contains(key)) {
        return true;
    }
    const std::size_t slot = g_answeredCount.load(std::memory_order_relaxed);
    if (slot >= kAnsweredCapacity) {
        return false;
    }
    g_answered[slot].store(key, std::memory_order_relaxed);
    g_answeredCount.store(slot + 1, std::memory_order_release);
    return true;
}

/** Forgets every answer and every shown interaction. */
void clear() noexcept {
    g_answeredCount.store(0, std::memory_order_release);
    for (auto& shown : g_shown) {
        shown.store(kAbsentIndex, std::memory_order_relaxed);
    }
}

} // namespace sunrise::state::vendors
