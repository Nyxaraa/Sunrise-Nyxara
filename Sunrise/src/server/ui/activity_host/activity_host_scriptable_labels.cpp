#include "activity_host_scriptable_labels.h"

namespace sunrise::server::ui::activity_host::scriptable_labels {

namespace catalog = state::build_data::scriptables;

/** @return Stable UI text for one catalog build status. */
const char* status(catalog::BuildStatus value) noexcept {
    switch (value) {
    case catalog::BuildStatus::idle:
        return "idle";
    case catalog::BuildStatus::queued:
        return "queued";
    case catalog::BuildStatus::building:
        return "building";
    case catalog::BuildStatus::ready:
        return "ready";
    case catalog::BuildStatus::failed:
        return "failed";
    }
    return "unknown";
}

/** @return Stable UI text for one package-presence classification. */
const char* presence(catalog::GroupSafety value) noexcept {
    switch (value) {
    case catalog::GroupSafety::notApplicable:
        return "no declared slots";
    case catalog::GroupSafety::destinationSafe:
        return "all destination states";
    case catalog::GroupSafety::bubbleSafe:
        return "all bubble states";
    case catalog::GroupSafety::stateOnly:
        return "selected state only";
    case catalog::GroupSafety::incomplete:
        return "incomplete package read";
    case catalog::GroupSafety::ambiguous:
        return "duplicate identity";
    }
    return "unknown";
}

/** @return Stable UI text for one package-name evidence tier. */
const char* provenance(catalog::NameProvenance value) noexcept {
    switch (value) {
    case catalog::NameProvenance::unresolved:
        return "unresolved";
    case catalog::NameProvenance::packageInline:
        return "package-inline hash match";
    case catalog::NameProvenance::packagePath:
        return "package-path hash match";
    case catalog::NameProvenance::packageIdentifierCandidate:
        return "package identifier candidate";
    }
    return "unresolved";
}

/** @return Stable UI text for one registry descriptor scope. */
const char* scope(std::uint16_t descriptor) noexcept {
    switch (descriptor) {
    case 8:
        return "shared/8";
    case 24:
        return "registry/24";
    case 40:
        return "state-local/40";
    default:
        return "unknown";
    }
}

} // namespace sunrise::server::ui::activity_host::scriptable_labels
