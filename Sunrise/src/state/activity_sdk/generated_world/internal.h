#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "format.h"

namespace sunrise::state::activity_sdk::generated_world::internal {

/** Serializes every snapshot field and computes the payload digest stored in the header. */
[[nodiscard]] bool build_payload(const Digest& sourceFingerprint,
                                 const build_data::scriptables::Snapshot& snapshot,
                                 format::Header& header,
                                 std::vector<std::byte>& payload);

/** Checks the exact fixed section layout before the file layer allocates a payload. */
[[nodiscard]] bool valid_shape(const format::Header& header, std::uint64_t actualSize) noexcept;

/** Decodes an authenticated payload without partially replacing the caller's snapshot. */
[[nodiscard]] bool decode_payload(const format::Header& header,
                                  std::span<const std::byte> payload,
                                  build_data::scriptables::Snapshot& snapshot);

/** Validates every bounded row range, ownership edge, and cross-table row reference. */
[[nodiscard]] bool valid_snapshot_graph(const build_data::scriptables::Snapshot& snapshot) noexcept;

} // namespace sunrise::state::activity_sdk::generated_world::internal
