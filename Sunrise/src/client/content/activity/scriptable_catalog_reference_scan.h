#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sunrise::client::content::activity::scriptables::internal {

/** One aligned package ClientRef record retained without behavior semantics. */
struct RawReference final {
    std::uint32_t configTag{};
    std::uint32_t offset{};
    std::uint32_t targetKey{};
    std::uint16_t targetType{};
    std::uint16_t targetIndex{};
};

/** Retains aligned ClientRef records from one reached config blob. */
void collect_typed_references(std::span<const std::byte> blob,
                              std::uint32_t configTag,
                              std::vector<RawReference>& output);

} // namespace sunrise::client::content::activity::scriptables::internal
