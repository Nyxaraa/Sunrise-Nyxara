#pragma once
#include "../../state/activity/runtime.h"
#include "../../state/gameplay/external/squad_entity_retirement.h"
namespace sunrise::server::gameplay::entity_identities {
class PublicationLease;
}
namespace sunrise::server::activity::host {
struct PendingScriptableOverride;
}
namespace sunrise::server::gameplay::squad_entity_retirement {
using RetirementPlan = state::gameplay::squad_entity_retirement::RetirementPlan;
/** The authenticated abdication freezes exact identities before a later renewal can retire them. */
void observe_abdication(const state::activity::SessionBinding&,
                        std::uint64_t generation,
                        std::uint8_t bubble,
                        const state::activity::bubble_authority::EntitySlotMask&) noexcept;
/** Returned slots cannot remain eligible for an earlier release. */
void returned_slots(const state::activity::SessionBinding&,
                    std::uint64_t generation,
                    const state::activity::bubble_authority::EntitySlotMask&) noexcept;
/** Renewal preparation never consumes retained entities. */
[[nodiscard]] bool prepare_retirement(const state::activity::SessionBinding&,
                                      std::uint64_t generation,
                                      std::uint8_t bubble,
                                      RetirementPlan&) noexcept;
/** Publication must revalidate the exact staged source, mask, and revision. */
[[nodiscard]] bool validate_retirement(const state::activity::SessionBinding&,
                                       std::uint64_t generation,
                                       const RetirementPlan&) noexcept;
/** Pins exact identity state through transport publication after the last policy validation. */
[[nodiscard]] bool begin_retirement_publication(const state::activity::SessionBinding&,
                                                std::uint64_t generation,
                                                const RetirementPlan&,
                                                entity_identities::PublicationLease&) noexcept;
/** Commits only after the complete carrying transport publication succeeds. */
void commit_retirement(const RetirementPlan&) noexcept;
/** Authored opt-in becomes eligible only after its positive Auth body was delivered. */
void record_delivered_target(const state::activity::SessionBinding&,
                             std::uint64_t generation,
                             const activity::host::PendingScriptableOverride&) noexcept;
void reset() noexcept;
} // namespace sunrise::server::gameplay::squad_entity_retirement
