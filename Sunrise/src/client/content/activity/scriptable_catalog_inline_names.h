#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using InlineNameVisitor = bool (*)(void* context,
                                   std::uint32_t hash,
                                   std::span<const std::byte> bytes) noexcept;

/** Visits every bounded valid inline UTF-8 record in one reached package blob. */
[[nodiscard]] bool visit_inline_names(std::span<const std::byte> blob,
                                      InlineNameVisitor visitor,
                                      void* context) noexcept;

/** Adds every valid inline UTF-8 record in one blob to the snapshot-local evidence bank. */
[[nodiscard]] bool collect_inline_name_evidence(state::build_data::scriptables::Snapshot& output,
                                                std::span<const std::byte> blob) noexcept;

/** Sorts and repacks the complete snapshot-local evidence bank by hash and exact bytes. */
[[nodiscard]] bool
canonicalize_inline_name_evidence(state::build_data::scriptables::Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
