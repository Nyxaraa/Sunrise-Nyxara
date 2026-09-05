#include "queuez_outcome_staging.h"

#include <algorithm>
#include <limits>
#include <optional>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../../../middleware/secure_channel/runtime.h"
#include "queuez_state_validation.h"

namespace sunrise::server::bap::encrypted::queuez {
namespace {

/** Extends the active feed overlay with the two identities touched by this equip. */
[[nodiscard]] bool merge_equipment_presentation_rows(
    std::span<const AcquisitionPresentationRow> existing,
    const state::PendingEquipmentSwap& mutation,
    std::array<AcquisitionPresentationRow, kAcquisitionPresentationRowCapacity>& output,
    std::uint8_t& count) noexcept {
    if (existing.size() > output.size()) {
        return false;
    }
    std::copy(existing.begin(), existing.end(), output.begin());
    std::size_t used = existing.size();

    const state::AccountState account = state::account_snapshot();
    middleware::datagen::family4::loadout::ResolvedLoadout loadout{};
    if (!mutation.prepared || mutation.characterIndex >= account.characterCount
        || account.characters[mutation.characterIndex].soid != mutation.characterSoid
        || !middleware::datagen::family4::loadout::resolve(
            account, mutation.characterIndex, loadout)) {
        return false;
    }

    const auto add = [&](std::uint64_t instanceSoid) noexcept -> bool {
        if (instanceSoid == 0) {
            return true;
        }
        for (std::size_t index = 0; index < used; ++index) {
            if (output[index].instanceSoid == instanceSoid) {
                return true;
            }
        }
        if (used >= output.size()) {
            return false;
        }
        for (std::size_t index = 0; index < loadout.itemCount; ++index) {
            const auto& item = loadout.items[index];
            if (item.instance.instanceSoid != instanceSoid) {
                continue;
            }
            for (std::size_t prior = 0; prior < used; ++prior) {
                if (output[prior].inventoryRow == item.inventoryRow) {
                    return false;
                }
            }
            output[used++] = AcquisitionPresentationRow{
                instanceSoid, static_cast<std::uint16_t>(item.inventoryRow)};
            return true;
        }
        return false;
    };

    if (!add(mutation.requestedInstanceSoid) || !add(mutation.previousInstanceSoid)) {
        return false;
    }
    count = static_cast<std::uint8_t>(used);
    return true;
}

/** Stages the character upsert and appended resident an item acquisition promised. */
[[nodiscard]] bool
stage_item_acquisition_push(Scratch& scratch,
                            const SessionState& before,
                            const ItemAcquisition& acquisition,
                            const state::PendingItemAcquisition& pending,
                            std::optional<std::uint16_t> pendingSeasonReward,
                            std::span<const AcquisitionPresentationRow> presentationRows,
                            std::span<const std::byte, state::kAesKeySize> key,
                            std::array<std::byte, state::kBapNonceSize>& nonce,
                            std::span<std::byte> response,
                            std::size_t& written,
                            SessionState& after) noexcept {
    const std::size_t appendedIndex = before.family4ResidentCount;
    bool preservedManifest = acquisition.after.family4ResidentCount == appendedIndex + 1U;
    for (std::size_t index = 0; preservedManifest && index < appendedIndex; ++index) {
        preservedManifest = acquisition.after.family4Residents[index].objectSoid
                                == before.family4Residents[index].objectSoid
                            && acquisition.after.family4Residents[index].definitionId
                                   == before.family4Residents[index].definitionId;
    }
    if (!valid(acquisition.after) || !preservedManifest
        || acquisition.accountSoid != pending.accountSoid
        || acquisition.characterSoid != pending.characterSoid
        || acquisition.acquiredInstanceSoid != pending.acquiredInstanceSoid
        || acquisition.updatesAccount != (pending.profileChanged || pendingSeasonReward.has_value())
        || acquisition.accountSoid != before.family4RootSoid
        || acquisition.after.family4RootSoid != before.family4RootSoid
        || before.family4ResidentCount >= before.family4Residents.size()
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || acquisition.after.family4Version != before.family4Version + 1
        || acquisition.after.family4Residents[appendedIndex].objectSoid
               != acquisition.acquiredInstanceSoid
        || acquisition.after.family4Residents[appendedIndex].definitionId
               != acquisition.itemInstanceDefinitionId
        || !push::append_item_acquisition_notification(scratch,
                                                       acquisition,
                                                       pending,
                                                       pendingSeasonReward,
                                                       presentationRows,
                                                       key,
                                                       nonce,
                                                       response,
                                                       written)) {
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    after = acquisition.after;
    return true;
}

/** Stages the account upsert and optional manifest append a profile acquisition promised. */
[[nodiscard]] bool
stage_profile_item_acquisition_push(Scratch& scratch,
                                    const SessionState& before,
                                    const ProfileItemAcquisition& acquisition,
                                    const state::PendingProfileItemAcquisition& pending,
                                    std::optional<std::uint16_t> pendingSeasonReward,
                                    std::span<const std::byte, state::kAesKeySize> key,
                                    std::array<std::byte, state::kBapNonceSize>& nonce,
                                    std::span<std::byte> response,
                                    std::size_t& written,
                                    SessionState& after) noexcept {
    const std::size_t priorResidentCount = before.family4ResidentCount;
    const std::size_t expectedResidentCount =
        priorResidentCount + static_cast<std::size_t>(acquisition.appendedResident);
    bool validManifest = expectedResidentCount <= acquisition.after.family4Residents.size()
                         && acquisition.after.family4ResidentCount == expectedResidentCount;
    for (std::size_t index = 0; validManifest && index < priorResidentCount; ++index) {
        validManifest = acquisition.after.family4Residents[index].objectSoid
                            == before.family4Residents[index].objectSoid
                        && acquisition.after.family4Residents[index].definitionId
                               == before.family4Residents[index].definitionId;
    }
    std::size_t priorProfileResidentMatches = 0;
    for (std::size_t index = 0; index < priorResidentCount; ++index) {
        const ResidentObject& resident = before.family4Residents[index];
        priorProfileResidentMatches += static_cast<std::size_t>(
            acquisition.acquiredInstanceSoid != 0
            && resident.objectSoid == acquisition.acquiredInstanceSoid
            && resident.definitionId == acquisition.itemInstanceDefinitionId);
    }
    const bool appendedResidentValid =
        !acquisition.appendedResident
        || (priorResidentCount < acquisition.after.family4Residents.size()
            && acquisition.after.family4Residents[priorResidentCount].objectSoid
                   == acquisition.acquiredInstanceSoid
            && acquisition.after.family4Residents[priorResidentCount].definitionId
                   == acquisition.itemInstanceDefinitionId
            && priorProfileResidentMatches == 0);
    const bool sourceIdentityValid =
        acquisition.actionSource == (acquisition.acquiredInstanceSoid != 0)
        && (acquisition.actionSource
                ? acquisition.itemInstanceDefinitionId != 0 && appendedResidentValid
                      && (acquisition.appendedResident || priorProfileResidentMatches == 1)
                : acquisition.itemInstanceDefinitionId == 0 && !acquisition.appendedResident
                      && priorProfileResidentMatches == 0);
    if (!valid(acquisition.after) || !validManifest || !sourceIdentityValid
        || acquisition.accountSoid != pending.accountSoid
        || acquisition.acquiredInstanceSoid != pending.acquiredInstanceSoid
        || acquisition.actionSource != pending.actionSource
        || acquisition.appendedResident != (pending.appended && pending.actionSource)
        || acquisition.accountSoid != before.family4RootSoid || before.family4ResidentCount == 0
        || acquisition.accountDefinitionId != before.family4Residents.front().definitionId
        || acquisition.after.family4RootSoid != before.family4RootSoid
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || acquisition.after.family4Version != before.family4Version + 1
        || !push::append_profile_item_acquisition_notification(
            scratch, acquisition, pending, pendingSeasonReward, key, nonce, response, written)) {
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    after = acquisition.after;
    return true;
}

/**
 * Says whether equipping into one slot changes what the family-two member record publishes.
 *
 * That record carries two fields fed by different slots. The emblem comes from the emblem slot,
 * and the light is the mean of the eight gear slots -- the three weapons and the five armour
 * pieces -- so an armour swap moves the record just as surely as an emblem swap does. Gating on
 * the emblem alone would leave the roster row holding a stale light until the next subscribe.
 *
 * Everything else is excluded because it moves neither field: a ghost, sparrow, ship, subclass,
 * clan banner, emote or finisher carries no Power in this season and is not the emblem, so a
 * swap there would spend a re-push republishing an unchanged object.
 *
 * @param equipmentSlotIndex Authored semantic slot the equip targeted.
 * @return True when the slot feeds the emblem or the light the member record carries.
 */
[[nodiscard]] constexpr bool moves_social_roster(std::size_t equipmentSlotIndex) noexcept {
    namespace inventory = state::account::inventory;
    return equipmentSlotIndex <= static_cast<std::size_t>(inventory::EquipmentSlot::classItem)
           || equipmentSlotIndex == static_cast<std::size_t>(inventory::EquipmentSlot::emblem);
}

} // namespace

/** Stages queuez subscription, unsubscription, or character-move output for one peer. */
bool stage_service_outcome(Scratch& scratch,
                           const SessionState& before,
                           const ServiceOutcome& outcome,
                           bool preserveAcquisitionPresentation,
                           std::span<const AcquisitionPresentationRow> acquisitionPresentationRows,
                           std::span<const std::byte, state::kAesKeySize> key,
                           std::array<std::byte, state::kBapNonceSize>& nonce,
                           std::span<std::byte> response,
                           std::size_t& written,
                           StagedPublication& publication) noexcept {
    publication = {};
    SessionState after{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    std::uint64_t bannerRoot = 0;
    std::uint64_t socialRosterRoot = 0;
    bool armsAbilityRefresh = false;
    const auto* changeCharacter = transaction_if<ChangeCharacter>(outcome);
    const auto* selectCharacter = transaction_if<SelectCharacter>(outcome);
    const auto* equipment = transaction_if<EquipmentSwapTransaction>(outcome);
    const auto* subclassSelection = transaction_if<SubclassSelectionTransaction>(outcome);
    const auto* itemState = transaction_if<ItemStateTransaction>(outcome);
    const auto* currentActivity = transaction_if<CurrentActivityTransaction>(outcome);
    const auto* artifactPurchase = transaction_if<ArtifactPurchaseTransaction>(outcome);
    const auto* socket = transaction_if<SocketPlugTransaction>(outcome);
    const auto* itemAcquisition = transaction_if<ItemAcquisitionTransaction>(outcome);
    const auto* profileAcquisition = transaction_if<ProfileItemAcquisitionTransaction>(outcome);
    const auto* itemDismantle = transaction_if<ItemDismantleTransaction>(outcome);
    const auto* recordRewardGrant = transaction_if<RecordRewardGrantTransaction>(outcome);
    const auto* seasonPassReward = transaction_if<SeasonPassRewardTransaction>(outcome);
    const auto presentationRows = preserveAcquisitionPresentation
                                      ? acquisitionPresentationRows
                                      : std::span<const AcquisitionPresentationRow>{};
    // Set before the branch chain rather than inside the equipment arm. That arm returns early
    // when the staged after-image fails validation, and the equip has already moved State by
    // then -- so the published record is stale on exactly the path the arm never finishes.
    publication.rearmsSocialRosterRepush =
        equipment != nullptr && equipment->pending != nullptr
        && moves_social_roster(equipment->pending->equipmentSlotIndex);
    if (outcome.hasSubscription) {
        push::append_queuez_notification(scratch,
                                         before,
                                         outcome.subscription,
                                         key,
                                         nonce,
                                         response,
                                         written,
                                         after,
                                         armsRepush,
                                         armsBannerRepush);
        bannerRoot = outcome.subscription.familyRootSoid;
        // The subscribe is the only moment a family-two root arrives. Recorded rather than acted
        // on: the inline answer to this subscribe lands, so nothing is owed until an equip makes
        // what it published stale.
        if (outcome.subscription.familyType == kSocialRosterFamilyType) {
            socialRosterRoot = outcome.subscription.familyRootSoid;
        }
    } else if (outcome.hasUnsubscription) {
        stage_unsubscription(before,
                             outcome.unsubscription.familyType,
                             outcome.unsubscription.familyRootSoid,
                             after);
    } else if (outcome.hasChangeCharacter) {
        // The reply already carries the version this patch promises. A patch that cannot be built
        // leaves the ladder where it is, instead of holding back that reply.
        if (changeCharacter == nullptr
            || !push::append_change_character_notification(
                scratch, *changeCharacter, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=change result=fail");
            return true;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = changeCharacter->after;
    } else if (equipment != nullptr) {
        // Body processing already staged this exact after-image so the correlated opcode-403
        // response could promise its version. Reuse it here; staging a second revision would make
        // the response and pushed Family-4 ladder disagree.
        if (equipment->pending == nullptr) {
            return false;
        }
        const auto& pending = *equipment->pending;
        const EquipmentSwap& swap = equipment->update;
        std::array<AcquisitionPresentationRow, kAcquisitionPresentationRowCapacity>
            mergedPresentationRows{};
        std::uint8_t mergedPresentationRowCount = 0;
        const bool hasPresentation =
            preserveAcquisitionPresentation
            && merge_equipment_presentation_rows(acquisitionPresentationRows,
                                                 pending,
                                                 mergedPresentationRows,
                                                 mergedPresentationRowCount);
        if (preserveAcquisitionPresentation && !hasPresentation) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=equip_presentation result=fail");
        }
        const auto equipmentPresentationRows =
            hasPresentation ? std::span(mergedPresentationRows).first(mergedPresentationRowCount)
                            : std::span<const AcquisitionPresentationRow>{};
        if (!valid(swap.after) || swap.characterSoid != pending.characterSoid
            || swap.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || swap.after.family4Version != before.family4Version + 1
            || !push::append_equipment_swap_notification(
                scratch, swap, pending, equipmentPresentationRows, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=equip result=fail");
            // The State transaction must not commit when the Client cannot receive its after-image.
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = swap.after;
        if (preserveAcquisitionPresentation) {
            if (hasPresentation) {
                publication.acquisitionPresentationRows = mergedPresentationRows;
                publication.acquisitionPresentationRowCount = mergedPresentationRowCount;
            }
            publication.updatesAcquisitionPresentationRows = true;
        }
        // Swapping the subclass slot invalidates the published ability buckets the same way an
        // opcode-801 pick does; the rebuild is likewise asynchronous, so this owes the same
        // delayed re-derivation rather than risking a race with whatever refresh runs below.
        if (pending.equipmentSlotIndex
            == static_cast<std::size_t>(state::account::inventory::EquipmentSlot::subclass)) {
            armsAbilityRefresh = true;
        }
        // Family four drives inventory placement, while Family zero owns the rendered appearance
        // the cosmetic panels and world player consume. Its resident character record is updated
        // in place: releasing and re-adding the same key tears down the ship/banner binding.
        if (after.family0Active) {
            CharacterAppearanceRefresh refresh{};
            if (!stage_character_appearance_refresh(after, pending.characterSoid, refresh)
                || !push::append_equipment_appearance_refresh_notification(
                    scratch, refresh, pending, key, nonce, response, written)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=queuez stage=equip_appearance result=fail");
                // The State transaction and both peer ladders remain unpublished when either
                // member of the paired Family-4/Family-0 delivery cannot fit.
                return false;
            }
            after = refresh.after;
        }
        // Family three owns the orbit roster and a separate copy of the same appearance record.
        // Equipment movement changes both bodies, so publish character first and roster second at
        // one exact +1 Family-3 revision.  Failure keeps all three peer ladders and State private.
        if (after.family3Active) {
            RosterAppearanceRefresh refresh{};
            if (!stage_roster_appearance_refresh(after, pending.characterSoid, true, refresh)
                || !push::append_equipment_roster_refresh_notification(
                    scratch, refresh, pending, key, nonce, response, written)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=queuez stage=equip_roster result=fail");
                return false;
            }
            after = refresh.after;
        }
    } else if (artifactPurchase != nullptr) {
        if (artifactPurchase->pending == nullptr) {
            return false;
        }
        const auto& pending = *artifactPurchase->pending;
        const EquipmentSwap& update = artifactPurchase->update;
        bool preservedManifest = update.after.family4ResidentCount == before.family4ResidentCount;
        for (std::size_t index = 0; preservedManifest && index < before.family4ResidentCount;
             ++index) {
            preservedManifest = update.after.family4Residents[index].objectSoid
                                    == before.family4Residents[index].objectSoid
                                && update.after.family4Residents[index].definitionId
                                       == before.family4Residents[index].definitionId;
        }
        if (!valid(update.after) || !preservedManifest
            || update.characterSoid != pending.characterSoid
            || update.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || update.after.family4Version != before.family4Version + 1
            || !push::append_artifact_purchase_notification(
                scratch, update, pending, presentationRows, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=artifact result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = update.after;
    } else if (itemState != nullptr) {
        // Item-state bits live in the selected-character inventory row. Publish only that
        // resident character body; item-instance, appearance, roster and manifest are unchanged.
        if (itemState->pending == nullptr) {
            return false;
        }
        const auto& pending = *itemState->pending;
        const EquipmentSwap& update = itemState->update;
        bool preservedManifest = update.after.family4ResidentCount == before.family4ResidentCount;
        for (std::size_t index = 0; preservedManifest && index < before.family4ResidentCount;
             ++index) {
            preservedManifest = update.after.family4Residents[index].objectSoid
                                    == before.family4Residents[index].objectSoid
                                && update.after.family4Residents[index].definitionId
                                       == before.family4Residents[index].definitionId;
        }
        if (!valid(update.after) || !preservedManifest
            || update.characterSoid != pending.characterSoid
            || update.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || update.after.family4Version != before.family4Version + 1
            || !push::append_item_state_notification(
                scratch, update, pending, presentationRows, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=item_state result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = update.after;
    } else if (currentActivity != nullptr) {
        // Only the selected character's own body changes. The reply is the client's task
        // completion and this upsert rides behind it in the same write.
        const EquipmentSwap& update = currentActivity->update;
        bool preservedManifest = update.after.family4ResidentCount == before.family4ResidentCount;
        for (std::size_t index = 0; preservedManifest && index < before.family4ResidentCount;
             ++index) {
            preservedManifest = update.after.family4Residents[index].objectSoid
                                    == before.family4Residents[index].objectSoid
                                && update.after.family4Residents[index].definitionId
                                       == before.family4Residents[index].definitionId;
        }
        if (!valid(update.after) || !preservedManifest
            || update.characterSoid != currentActivity->pending.characterSoid
            || update.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || update.after.family4Version != before.family4Version + 1
            || !push::append_current_activity_notification(
                scratch, update, currentActivity->pending, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=current_activity result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = update.after;
    } else if (subclassSelection != nullptr) {
        // Body processing already staged the exact +1 revision opcode 801 promised. The instance
        // upsert goes first, then the appearance and roster refreshes, so gameplay reads the new
        // selection now rather than on the next unrelated poll.
        if (subclassSelection->pending == nullptr) {
            return false;
        }
        const auto& pending = *subclassSelection->pending;
        const SubclassSelection& selection = subclassSelection->update;
        bool preservedManifest =
            selection.after.family4ResidentCount == before.family4ResidentCount;
        std::size_t targetMatches = 0;
        for (std::size_t index = 0; preservedManifest && index < before.family4ResidentCount;
             ++index) {
            const ResidentObject& resident = before.family4Residents[index];
            const ResidentObject& staged = selection.after.family4Residents[index];
            preservedManifest = staged.objectSoid == resident.objectSoid
                                && staged.definitionId == resident.definitionId;
            targetMatches += static_cast<std::size_t>(
                resident.objectSoid == selection.subclassInstanceSoid
                && resident.definitionId == selection.itemInstanceDefinitionId);
        }
        if (!valid(selection.after) || !preservedManifest || targetMatches != 1
            || selection.accountSoid != pending.accountSoid
            || selection.characterSoid != pending.characterSoid
            || selection.subclassInstanceSoid != pending.subclassInstanceSoid
            || selection.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || selection.after.family4Version != before.family4Version + 1
            || !push::append_subclass_selection_notification(
                scratch, selection, pending, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=subclass_select result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = selection.after;
        // Family zero and Family three no longer fit beside the enlarged upstream Family-four
        // selection notification in this bounded reply. Publish them through the already-proven
        // deferred refresh channel after the primary mutation commits.
        armsAbilityRefresh = true;
    } else if (socket != nullptr) {
        // Body processing staged this exact +1 revision before encoding opcode 903's status pair.
        // A socket selection changes only one already-resident item-instance body.
        if (socket->pending == nullptr) {
            return false;
        }
        const auto& pending = *socket->pending;
        const SocketPlug& socketPlug = socket->update;
        bool preservedManifest =
            socketPlug.after.family4ResidentCount == before.family4ResidentCount;
        std::size_t accountMatches = 0;
        std::size_t targetMatches = 0;
        for (std::size_t index = 0; preservedManifest && index < before.family4ResidentCount;
             ++index) {
            const ResidentObject& resident = before.family4Residents[index];
            const ResidentObject& staged = socketPlug.after.family4Residents[index];
            preservedManifest = staged.objectSoid == resident.objectSoid
                                && staged.definitionId == resident.definitionId;
            targetMatches += static_cast<std::size_t>(
                resident.objectSoid == socketPlug.targetInstanceSoid
                && resident.definitionId == socketPlug.itemInstanceDefinitionId);
            accountMatches += static_cast<std::size_t>(resident.objectSoid == socketPlug.accountSoid
                                                       && resident.definitionId
                                                              == socketPlug.accountDefinitionId);
        }
        if (!valid(socketPlug.after) || !preservedManifest || accountMatches != 1
            || targetMatches != 1 || socketPlug.accountSoid != pending.accountSoid
            || socketPlug.characterSoid != pending.characterSoid
            || socketPlug.targetInstanceSoid != pending.targetInstanceSoid
            || socketPlug.updatesAccount != pending.profileChanged
            || socketPlug.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || socketPlug.after.family4Version != before.family4Version + 1
            || !push::append_socket_plug_notification(
                scratch, socketPlug, pending, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=socket_plug result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = socketPlug.after;
        // Equipped plugs alter Family-zero and Family-three presentation. Those enlarged records
        // are published separately so this mutation cannot fail after its primary frame is built.
        if (pending.targetEquipped) {
            armsAbilityRefresh = true;
        }
    } else if (itemAcquisition != nullptr) {
        // Body processing staged this exact manifest append before encoding the response version.
        // The character and new item objects must both fit or the State insertion is not committed.
        if (itemAcquisition->pending == nullptr
            || !stage_item_acquisition_push(scratch,
                                            before,
                                            itemAcquisition->update,
                                            *itemAcquisition->pending,
                                            std::nullopt,
                                            presentationRows,
                                            key,
                                            nonce,
                                            response,
                                            written,
                                            after)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=acquire result=fail");
            return false;
        }
    } else if (profileAcquisition != nullptr) {
        // A source-backed profile append creates one dependency before the account starts naming
        // it. Existing stacks and non-actionable currency rows preserve the complete manifest.
        if (profileAcquisition->pending == nullptr
            || !stage_profile_item_acquisition_push(scratch,
                                                    before,
                                                    profileAcquisition->update,
                                                    *profileAcquisition->pending,
                                                    std::nullopt,
                                                    key,
                                                    nonce,
                                                    response,
                                                    written,
                                                    after)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=profile_acquire result=fail");
            return false;
        }
    } else if (recordRewardGrant != nullptr) {
        if (recordRewardGrant->pending == nullptr) {
            return false;
        }
        if (!push::append_record_reward_notification(scratch,
                                                     before,
                                                     recordRewardGrant->update,
                                                     *recordRewardGrant->pending,
                                                     std::nullopt,
                                                     presentationRows,
                                                     key,
                                                     nonce,
                                                     response,
                                                     written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=record_reward result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = recordRewardGrant->update.after;
    } else if (seasonPassReward != nullptr) {
        if (seasonPassReward->pending == nullptr) {
            return false;
        }
        const auto& pending = *seasonPassReward->pending;
        bool staged = false;
        if (const auto* itemUpdate = std::get_if<ItemAcquisition>(&seasonPassReward->update)) {
            if (const auto* itemPending =
                    std::get_if<state::PendingItemAcquisition>(&pending.grant)) {
                staged = stage_item_acquisition_push(scratch,
                                                     before,
                                                     *itemUpdate,
                                                     *itemPending,
                                                     pending.rewardIndex,
                                                     presentationRows,
                                                     key,
                                                     nonce,
                                                     response,
                                                     written,
                                                     after);
            }
        } else if (const auto* profileUpdate =
                       std::get_if<ProfileItemAcquisition>(&seasonPassReward->update)) {
            if (const auto* profilePending =
                    std::get_if<state::PendingProfileItemAcquisition>(&pending.grant)) {
                staged = stage_profile_item_acquisition_push(scratch,
                                                             before,
                                                             *profileUpdate,
                                                             *profilePending,
                                                             pending.rewardIndex,
                                                             key,
                                                             nonce,
                                                             response,
                                                             written,
                                                             after);
            }
        } else if (const auto* bundle =
                       std::get_if<state::PendingDirectItemBundle>(&pending.grant)) {
            staged = push::append_season_pass_package_notification(scratch,
                                                                   before,
                                                                   *bundle,
                                                                   pending.rewardIndex,
                                                                   presentationRows,
                                                                   key,
                                                                   nonce,
                                                                   response,
                                                                   written,
                                                                   after);
            if (staged) {
                middleware::secure_channel::advance_nonce(nonce);
            }
        } else if (const auto* resourceUpdate =
                       std::get_if<RecordRewardGrant>(&seasonPassReward->update)) {
            if (const auto* resources =
                    std::get_if<state::PendingRecordRewardGrant>(&pending.grant)) {
                staged = push::append_record_reward_notification(scratch,
                                                                 before,
                                                                 *resourceUpdate,
                                                                 *resources,
                                                                 pending.rewardIndex,
                                                                 presentationRows,
                                                                 key,
                                                                 nonce,
                                                                 response,
                                                                 written);
                if (staged) {
                    middleware::secure_channel::advance_nonce(nonce);
                    after = resourceUpdate->after;
                }
            }
        }
        if (!staged) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=ws2400 stage=queuez_reward result=fail");
            return false;
        }
    } else if (outcome.hasArtifactReset) {
        const state::AccountState account = state::account_snapshot();
        std::uint64_t selectedCharacter = 0;
        for (std::size_t index = 0; index < account.characterCount; ++index) {
            if (account.characters[index].selected) {
                selectedCharacter = account.characters[index].soid;
                break;
            }
        }
        EquipmentSwap reset{};
        if (selectedCharacter == 0 || !stage_equipment_swap(before, selectedCharacter, reset)
            || !push::append_artifact_reset_notification(
                scratch, reset, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=ws901 stage=artifact_reset_resync result=fail");
            return true;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = reset.after;
    } else if (outcome.hasRecordClaim) {
        // A claim rewrites one byte of the account flag bank and leaves the manifest alone, so a
        // full account snapshot at the next version carries it with no other staging.
        if (!push::append_account_resync_notification(
                scratch, before, presentationRows, key, nonce, response, written, after)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=ws1801 stage=queuez_resync result=fail");
            return true;
        }
    } else if (itemDismantle != nullptr) {
        // A dismantle removes exactly one resident while preserving the relative order of every
        // survivor. The character after-image and empty release descriptor must fit together or
        // the State removal is not committed.
        if (itemDismantle->pending == nullptr) {
            return false;
        }
        const auto& pending = *itemDismantle->pending;
        const ItemDismantle& dismantle = itemDismantle->update;
        bool compactedManifest =
            before.family4ResidentCount != 0
            && dismantle.after.family4ResidentCount + 1U == before.family4ResidentCount;
        std::size_t afterIndex = 0;
        std::size_t removedCount = 0;
        for (std::size_t beforeIndex = 0;
             compactedManifest && beforeIndex < before.family4ResidentCount;
             ++beforeIndex) {
            const ResidentObject& resident = before.family4Residents[beforeIndex];
            if (resident.objectSoid == dismantle.dismantledInstanceSoid) {
                compactedManifest = resident.definitionId == dismantle.itemInstanceDefinitionId;
                ++removedCount;
                continue;
            }
            if (afterIndex >= dismantle.after.family4ResidentCount) {
                compactedManifest = false;
                break;
            }
            const ResidentObject& survivor = dismantle.after.family4Residents[afterIndex++];
            compactedManifest = survivor.objectSoid == resident.objectSoid
                                && survivor.definitionId == resident.definitionId;
        }
        compactedManifest = compactedManifest && removedCount == 1U
                            && afterIndex == dismantle.after.family4ResidentCount;

        if (!valid(dismantle.after) || !compactedManifest
            || dismantle.accountSoid != pending.accountSoid
            || dismantle.characterSoid != pending.characterSoid
            || dismantle.dismantledInstanceSoid != pending.dismantledInstanceSoid
            || dismantle.updatesAccount != pending.profileChanged
            || dismantle.accountSoid != before.family4RootSoid || before.family4ResidentCount == 0
            || dismantle.accountDefinitionId != before.family4Residents.front().definitionId
            || dismantle.after.family4RootSoid != before.family4RootSoid
            || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
            || dismantle.after.family4Version != before.family4Version + 1
            || !push::append_item_dismantle_notification(
                scratch, dismantle, pending, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=dismantle result=fail");
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = dismantle.after;
    } else if (outcome.hasSelectCharacter) {
        // The reply is the Client's task completion and the move is a separate frame. A move that
        // cannot be built leaves the selection where it is, instead of holding back that reply.
        if (selectCharacter == nullptr
            || !push::append_select_character_notification(
                scratch, *selectCharacter, key, nonce, response, written)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=select result=fail");
            return true;
        }
        middleware::secure_channel::advance_nonce(nonce);
        after = selectCharacter->after;
        // The banner pair follows the family-four move, so it is sent against the state that move
        // made. A pair that cannot be built leaves the emblem where it is.
        const SessionState& bannerBefore = after;
        SessionState bannerAfter{};
        if (push::append_banner_move_notification(scratch,
                                                  bannerBefore,
                                                  selectCharacter->selectedCharacterSoid,
                                                  key,
                                                  nonce,
                                                  response,
                                                  written,
                                                  bannerAfter)) {
            after = bannerAfter;
        }
        // A family-zero subscribe that arrived before this pick was held, not answered. The pick
        // is the first moment the pair can be built, and the peer will not ask again.
        if (after.pendingBannerRoot != 0) {
            middleware::queuez::Subscription held{};
            held.familyType = kBannerFamilyType;
            held.familyRootSoid = after.pendingBannerRoot;
            SessionState heldAfter{};
            bool heldRepush = false;
            bool heldBannerRepush = false;
            const SessionState heldBefore = after;
            push::append_queuez_notification(scratch,
                                             heldBefore,
                                             held,
                                             key,
                                             nonce,
                                             response,
                                             written,
                                             heldAfter,
                                             heldRepush,
                                             heldBannerRepush);
            if (valid(heldAfter)) {
                after = heldAfter;
            }
            if (heldBannerRepush) {
                armsBannerRepush = true;
                bannerRoot = held.familyRootSoid;
            }
        }
    } else {
        return true;
    }
    // The frame is already written by here. A mirror that fails validation is logged and dropped,
    // never turned into a refusal to send what the Client waits for.
    publication.hasState = valid(after);
    if (publication.hasState) {
        publication.after = after;
    } else {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=publish result=unrecorded");
    }
    publication.armsFamily4Repush = armsRepush;
    publication.family4RepushRoot = armsRepush ? outcome.subscription.familyRootSoid : 0;
    publication.armsBannerRepush = armsBannerRepush && bannerRoot != 0;
    publication.bannerRepushRoot = publication.armsBannerRepush ? bannerRoot : 0;
    publication.socialRosterRepushRoot = socialRosterRoot;
    publication.armsAbilityRefresh = armsAbilityRefresh;
    return true;
}

} // namespace sunrise::server::bap::encrypted::queuez
