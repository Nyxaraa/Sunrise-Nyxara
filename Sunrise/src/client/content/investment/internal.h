#pragma once

#include "../../../state/account/account_state.h"
#include "source.h"

namespace sunrise::client::content::investment {

/**
 * Resolves the one installed investment-globals candidate backed by the live content tables.
 * @param source Receives the checked runtime tag, handle tables, and bounded reader.
 * @return True when the globals, root, and dense item table all resolve.
 */
[[nodiscard]] bool resolve_source(Source& source) noexcept;

/** @return True when the next refresh slice needs one presented overlay before its package sweep.
 */
[[nodiscard]] bool requires_package_sweep() noexcept;

} // namespace sunrise::client::content::investment
