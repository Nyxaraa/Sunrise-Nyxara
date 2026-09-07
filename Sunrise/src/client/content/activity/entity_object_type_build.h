#pragma once
#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/gameplay/external/entity_object_types.h"
namespace sunrise::client::content::activity::entity_object_types {
/** Scans installed RSATs and validates every reverse and forward class link. */
[[nodiscard]] bool build(const middleware::content::packages::reader::Source&,
                         middleware::content::packages::reader::Scratch&,
                         const state::gameplay::entity_object_types::Fingerprint&) noexcept;
} // namespace sunrise::client::content::activity::entity_object_types
