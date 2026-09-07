#pragma once

#include "../../../state/gameplay/external/entity_identity.h"
#include "composite_entity_codec.h"

namespace sunrise::middleware::gameplay::external {

/** Metadata failure never authorizes dropping an otherwise accepted entity observation. */
enum class MetadataStatus : std::uint8_t {
    complete,
    notCreate,
    missingCatalog,
    unsupportedSchema,
    malformedPayload,
    invalidReference
};

/** Only a unique RSAT and reverse-definition join supplies a packaged object type. */
[[nodiscard]] bool
extract_sobject_object_type(std::span<const state::activity_sdk::format::ActorClass> classes,
                            std::uint32_t rsatTag,
                            std::uint8_t& output) noexcept;

/** Extracts only verified baseline identities; output is cleared on failure. */
[[nodiscard]] bool
extract_entity_identity_metadata(const state::activity_sdk::Snapshot& catalog,
                                 const EntityRecord& record,
                                 state::gameplay::entity_identity::Metadata& output,
                                 MetadataStatus* status = nullptr) noexcept;

/** Reads only the source relation staged by a complete SObject update decoder. */
[[nodiscard]] bool extract_actor_source_reference(
    const EntityRecord& record,
    state::gameplay::entity_identity::ActorSourceReference& output) noexcept;

} // namespace sunrise::middleware::gameplay::external
