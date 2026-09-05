#include "lore_grant.h"

#include "../build_data/records/definition.h"
#include "../build_data/records/record_catalog.h"
#include "../record_claims/record_claims.h"

namespace sunrise::state::lore {
namespace {

/** Resolves a record row to the completion flag backing a lore chapter. */
GrantOutcome chapter_flag(std::uint16_t definitionIndex, std::uint16_t& flagIndex) noexcept {
    namespace records = build_data::records;
    records::Definition record{};
    if (!records::find(definitionIndex, record)) {
        return GrantOutcome::recordNotFound;
    }
    if (record.completionFlagIndex == records::kUnavailableFlagIndex) {
        return GrantOutcome::noFlag;
    }
    if (record.loreRow == records::kUnavailableLoreRow) {
        return GrantOutcome::notAChapter;
    }
    flagIndex = record.completionFlagIndex;
    return GrantOutcome::granted;
}

} // namespace

/** Grants one record's completion directly, by the row an sobject's lane 4 names. */
GrantOutcome grant_record(std::uint16_t definitionIndex) noexcept {
    std::uint16_t flagIndex = 0;
    const GrantOutcome resolved = chapter_flag(definitionIndex, flagIndex);
    if (resolved != GrantOutcome::granted) {
        return resolved;
    }
    if (record_claims::mark_claimable(flagIndex)) {
        return GrantOutcome::granted;
    }
    return record_claims::claimed(flagIndex) || record_claims::claimable(flagIndex)
               ? GrantOutcome::alreadyHeld
               : GrantOutcome::refused;
}

/** Advances one counted chapter record without completing it early. */
GrantOutcome advance_record(std::uint16_t definitionIndex) noexcept {
    std::uint16_t flagIndex = 0;
    const GrantOutcome resolved = chapter_flag(definitionIndex, flagIndex);
    if (resolved != GrantOutcome::granted) {
        return resolved;
    }
    const record_claims::ObjectiveAdvance outcome =
        record_claims::advance_single_objective(flagIndex);
    switch (outcome) {
    case record_claims::ObjectiveAdvance::advanced:
        return GrantOutcome::progressed;
    case record_claims::ObjectiveAdvance::completed:
        return GrantOutcome::granted;
    case record_claims::ObjectiveAdvance::alreadyHeld:
        return GrantOutcome::alreadyHeld;
    case record_claims::ObjectiveAdvance::unavailable:
        return GrantOutcome::refused;
    }
    return GrantOutcome::refused;
}

} // namespace sunrise::state::lore
