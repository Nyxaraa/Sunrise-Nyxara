#pragma once

#include "runtime.h"

namespace sunrise::state::activity_sdk::validation {

/** Checks scalar bounds, string refs, row ranges, and unique identities. */
[[nodiscard]] bool structure(const Catalog& catalog);
/** Checks every child range against its exact parent and target row. */
[[nodiscard]] bool relations(const Catalog& catalog);

} // namespace sunrise::state::activity_sdk::validation
