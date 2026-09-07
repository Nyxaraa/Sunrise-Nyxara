#pragma once
#include "../../../middleware/content/packages/reader/reader.h"
namespace sunrise::client::content::activity::entity_position_profiles {
/** Checks the content fingerprint before the package pass skips extraction. */
[[nodiscard]] bool ready() noexcept;
/** Confirms restored shared-cache rows or extracts profiles through the package reader. */
[[nodiscard]] bool build(const middleware::content::packages::reader::Source& source,
                         middleware::content::packages::reader::Scratch& scratch) noexcept;
} // namespace sunrise::client::content::activity::entity_position_profiles
