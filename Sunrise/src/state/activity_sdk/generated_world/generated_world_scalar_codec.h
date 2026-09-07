#pragma once

#include "internal.h"

namespace sunrise::state::activity_sdk::generated_world::internal {

/** Copies every non-vector snapshot field into its packed disk representation. */
[[nodiscard]] format::Scalars
encode_scalars(const build_data::scriptables::Snapshot& snapshot) noexcept;

/** @return True when packed scalar enums, flags, strings, and reserved bytes are canonical. */
[[nodiscard]] bool valid_scalars(const format::Scalars& value) noexcept;

/** Decodes every checked packed scalar into one otherwise-empty native snapshot. */
void decode_scalars(const format::Scalars& value,
                    build_data::scriptables::Snapshot& snapshot) noexcept;

} // namespace sunrise::state::activity_sdk::generated_world::internal
