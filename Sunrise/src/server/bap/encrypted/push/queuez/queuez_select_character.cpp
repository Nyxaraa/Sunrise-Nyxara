#include <array>
#include <cstdio>
#include <optional>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../queuez/queuez_state_validation.h"
#include "../snapshot/internal.h"
#include "queuez_push_reporting.h"
#include "queuez_update_frame.h"

namespace sunrise::server::bap::encrypted::push {

/** Appends the opcode-504 Family-4 character-selection update. */
bool append_select_character_notification(Scratch& scratch,
                                          const queuez::SelectCharacter& select,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::span<const std::byte, state::kBapNonceSize> nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_selection_move(scratch, select, prepared)) {
        return false;
    }
    if (!queuez_frame::append(scratch,
                              prepared.family,
                              prepared.rawClearSize,
                              prepared.compressedClearSize,
                              key,
                              nonce,
                              response,
                              written)) {
        return false;
    }
    return true;
}

/** Appends the opcode-403 character upsert as one increment above the current peer version. */
bool append_equipment_swap_notification(
    Scratch& scratch,
    const queuez::EquipmentSwap& swap,
    const state::PendingEquipmentSwap& mutation,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_equipment_swap(
            scratch, swap, mutation, acquisitionPresentationRows, prepared)) {
        return false;
    }
    if (!queuez_frame::append(scratch,
                              prepared.family,
                              prepared.rawClearSize,
                              prepared.compressedClearSize,
                              key,
                              nonce,
                              response,
                              written)) {
        return false;
    }
    return true;
}

/** Appends the character upsert carrying the character's new current activity. */
bool append_current_activity_notification(Scratch& scratch,
                                          const queuez::EquipmentSwap& swap,
                                          const state::PendingCurrentActivity& mutation,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::span<const std::byte, state::kBapNonceSize> nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_current_activity_character(scratch, swap, mutation, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    if (objectCount != 1 || prepared.family.objects.front().id != swap.characterDefinitionId
        || prepared.family.objects.front().version != swap.characterSoid
        || prepared.family.objects.front().payload.empty()
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=current_activity stage=character_object result=ok family_version=%d "
                      "character=0x%llX activity=%u",
                      prepared.family.version,
                      static_cast<unsigned long long>(swap.characterSoid),
                      static_cast<unsigned>(mutation.activityIndex));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    return true;
}

/** Appends one opcode-406 selected-character item-state upsert. */
bool append_item_state_notification(
    Scratch& scratch,
    const queuez::EquipmentSwap& update,
    const state::PendingItemState& mutation,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_item_state(
            scratch, update, mutation, acquisitionPresentationRows, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    if (objectCount != 1 || prepared.family.objects.front().id != update.characterDefinitionId
        || prepared.family.objects.front().version != update.characterSoid
        || prepared.family.objects.front().encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects.front().payload.empty()
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

/** Appends one opcode-901 selected-character artifact ownership upsert. */
bool append_artifact_purchase_notification(
    Scratch& scratch,
    const queuez::EquipmentSwap& update,
    const state::PendingArtifactPurchase& mutation,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_artifact_purchase(
            scratch, update, mutation, acquisitionPresentationRows, prepared)) {
        return false;
    }
    return prepared.family.objects.size() == 1
           && prepared.family.objects.front().id == update.characterDefinitionId
           && prepared.family.objects.front().version == update.characterSoid
           && prepared.family.objects.front().encoding == middleware::queuez::Encoding::oodle
           && !prepared.family.objects.front().payload.empty()
           && queuez_frame::append(scratch,
                                   prepared.family,
                                   prepared.rawClearSize,
                                   prepared.compressedClearSize,
                                   key,
                                   nonce,
                                   response,
                                   written);
}

/** Appends a reset increment without the full-snapshot acquisition semantics. */
bool append_artifact_reset_notification(
    Scratch& scratch,
    const queuez::EquipmentSwap& update,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_artifact_reset(scratch, update, prepared)) {
        return false;
    }
    return prepared.family.objects.size() == 2
           && prepared.family.objects[1].id == update.characterDefinitionId
           && prepared.family.objects[1].version == update.characterSoid
           && queuez_frame::append(scratch,
                                   prepared.family,
                                   prepared.rawClearSize,
                                   prepared.compressedClearSize,
                                   key,
                                   nonce,
                                   response,
                                   written);
}

/** Appends one exact resident upsert after artifact reset. */
bool append_artifact_item_refresh_notification(
    Scratch& scratch,
    const queuez::EquipmentSwap& update,
    std::uint64_t instanceSoid,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_artifact_item_refresh(scratch, update, instanceSoid, prepared)) {
        return false;
    }
    return prepared.family.objects.size() == 1
           && prepared.family.objects.front().version == instanceSoid
           && prepared.family.objects.front().encoding == middleware::queuez::Encoding::oodle
           && !prepared.family.objects.front().payload.empty()
           && queuez_frame::append(scratch,
                                   prepared.family,
                                   prepared.rawClearSize,
                                   prepared.compressedClearSize,
                                   key,
                                   nonce,
                                   response,
                                   written);
}

/** Appends one socket item upsert and its charged account balances when required. */
bool append_socket_plug_notification(Scratch& scratch,
                                     const queuez::SocketPlug& socketPlug,
                                     const state::PendingSocketPlug& mutation,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::span<const std::byte, state::kBapNonceSize> nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_socket_plug(scratch, socketPlug, mutation, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t expectedObjectCount = socketPlug.updatesAccount ? 2U : 1U;
    if (objectCount != expectedObjectCount
        || prepared.family.objects.front().id != socketPlug.itemInstanceDefinitionId
        || prepared.family.objects.front().version != socketPlug.targetInstanceSoid
        || prepared.family.objects.front().encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects.front().payload.empty()
        || (socketPlug.updatesAccount
            && (prepared.family.objects[1].id != socketPlug.accountDefinitionId
                || prepared.family.objects[1].version != socketPlug.accountSoid
                || prepared.family.objects[1].encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects[1].payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

/** Appends one subclass item-instance upsert after an opcode-801 selection. */
bool append_subclass_selection_notification(Scratch& scratch,
                                            const queuez::SubclassSelection& selection,
                                            const state::PendingSubclassSelection& mutation,
                                            std::span<const std::byte, state::kAesKeySize> key,
                                            std::span<const std::byte, state::kBapNonceSize> nonce,
                                            std::span<std::byte> response,
                                            std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_subclass_selection(scratch, selection, mutation, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    if (objectCount != 1 || prepared.family.objects.front().id != selection.itemInstanceDefinitionId
        || prepared.family.objects.front().version != selection.subclassInstanceSoid
        || prepared.family.objects.front().encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects.front().payload.empty()
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

/** Appends one atomic new-instance-before-character Family-4 acquisition update. */
bool append_item_acquisition_notification(
    Scratch& scratch,
    const queuez::ItemAcquisition& acquisition,
    const state::PendingItemAcquisition& mutation,
    std::optional<std::uint16_t> pendingSeasonReward,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_item_acquisition(scratch,
                                            acquisition,
                                            mutation,
                                            pendingSeasonReward,
                                            acquisitionPresentationRows,
                                            prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t expectedObjectCount = acquisition.updatesAccount ? 3U : 2U;
    if (objectCount != expectedObjectCount
        || prepared.family.objects[0].id != acquisition.itemInstanceDefinitionId
        || prepared.family.objects[0].version != acquisition.acquiredInstanceSoid
        || prepared.family.objects[0].encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects[0].payload.empty()
        || prepared.family.objects[1].id != acquisition.characterDefinitionId
        || prepared.family.objects[1].version != acquisition.characterSoid
        || prepared.family.objects[1].encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects[1].payload.empty()
        || (acquisition.updatesAccount
            && (prepared.family.objects[2].id != acquisition.accountDefinitionId
                || prepared.family.objects[2].version != acquisition.accountSoid
                || prepared.family.objects[2].encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects[2].payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

/** Appends an optional new profile resident followed by the full account after-image. */
bool append_profile_item_acquisition_notification(
    Scratch& scratch,
    const queuez::ProfileItemAcquisition& acquisition,
    const state::PendingProfileItemAcquisition& mutation,
    std::optional<std::uint16_t> pendingSeasonReward,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_profile_item_acquisition(
            scratch, acquisition, mutation, pendingSeasonReward, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t expectedObjectCount = acquisition.appendedResident ? 2U : 1U;
    const std::size_t accountIndex = acquisition.appendedResident ? 1U : 0U;
    if (prepared.family.type != queuez::kAccountFamilyType
        || prepared.family.rootSoid != acquisition.after.family4RootSoid
        || prepared.family.version != acquisition.after.family4Version || prepared.family.flags != 0
        || objectCount != expectedObjectCount || accountIndex >= objectCount
        || prepared.family.objects[accountIndex].id != acquisition.accountDefinitionId
        || prepared.family.objects[accountIndex].version != acquisition.accountSoid
        || prepared.family.objects[accountIndex].encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects[accountIndex].payload.empty()
        || (acquisition.appendedResident
            && (prepared.family.objects.front().id != acquisition.itemInstanceDefinitionId
                || prepared.family.objects.front().version != acquisition.acquiredInstanceSoid
                || prepared.family.objects.front().encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects.front().payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

/** Publishes the transient XP row and its matching account progression in one increment. */
bool append_seasonal_experience_notification(
    Scratch& scratch,
    const queuez::SessionState& before,
    std::int32_t amount,
    std::int32_t mutationSerial,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written,
    queuez::SessionState& after) noexcept {
    after = before;
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_seasonal_experience_presentation(
            scratch, before, amount, mutationSerial, acquisitionPresentationRows, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    if (objectCount != 2 || prepared.family.type != queuez::kAccountFamilyType
        || prepared.family.rootSoid != before.family4RootSoid
        || prepared.family.version != before.family4Version + 1 || prepared.family.flags != 0
        || prepared.family.objects[0].version != before.family4RootSoid
        || prepared.family.objects[0].id != before.family4Residents.front().definitionId
        || prepared.family.objects[0].payload.empty()
        || prepared.family.objects[1].version == before.family4RootSoid
        || prepared.family.objects[1].payload.empty()
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    after.family4Version = prepared.family.version;
    if (!queuez::valid(after)) {
        after = before;
        return false;
    }
    return true;
}

/** Publishes every item in one rank-one class package as an ordinary acquisition. */
bool append_season_pass_package_notification(
    Scratch& scratch,
    const queuez::SessionState& before,
    const state::PendingDirectItemBundle& mutation,
    std::uint16_t rewardIndex,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written,
    queuez::SessionState& after) noexcept {
    after = before;
    snapshot::Prepared prepared{};
    const std::size_t itemCount = mutation.itemCount;
    if (!snapshot::prepare_season_pass_package(
            scratch, before, mutation, rewardIndex, acquisitionPresentationRows, prepared)
        || itemCount == 0 || prepared.family.objects.size() != itemCount + 2U
        || prepared.family.type != queuez::kAccountFamilyType
        || prepared.family.rootSoid != before.family4RootSoid
        || prepared.family.version != before.family4Version + 1 || prepared.family.flags != 0
        || before.family4ResidentCount + itemCount > before.family4Residents.size()) {
        return false;
    }

    std::uint32_t itemDefinitionId = 0;
    if (!middleware::datagen::object_id(
            queuez::kAccountFamilyType, middleware::datagen::kItemInstanceSlot, itemDefinitionId)) {
        return false;
    }
    for (std::size_t index = 0; index < itemCount; ++index) {
        const middleware::queuez::Object& object = prepared.family.objects[index];
        if (object.id != itemDefinitionId || object.version != mutation.firstInstanceSoid + index
            || object.payload.empty()) {
            return false;
        }
        for (std::size_t residentIndex = 0; residentIndex < before.family4ResidentCount;
             ++residentIndex) {
            if (before.family4Residents[residentIndex].objectSoid == object.version) {
                return false;
            }
        }
        after.family4Residents[after.family4ResidentCount++] =
            queuez::ResidentObject{object.version, object.id};
    }
    const middleware::queuez::Object& characterObject = prepared.family.objects[itemCount];
    const middleware::queuez::Object& accountObject = prepared.family.objects[itemCount + 1U];
    std::size_t characterMatches = 0;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const queuez::ResidentObject& resident = before.family4Residents[index];
        characterMatches +=
            static_cast<std::size_t>(resident.objectSoid == characterObject.version
                                     && resident.definitionId == characterObject.id);
    }
    if (characterMatches != 1 || characterObject.payload.empty()
        || accountObject.version != before.family4RootSoid
        || accountObject.id != before.family4Residents.front().definitionId
        || accountObject.payload.empty()) {
        return false;
    }
    after.family4Version = prepared.family.version;
    if (!queuez::valid(after)
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        after = before;
        return false;
    }
    return true;
}

/** Publishes one prepared record-reward batch. */
bool append_record_reward_notification(
    Scratch& scratch,
    const queuez::SessionState& before,
    const queuez::RecordRewardGrant& update,
    const state::PendingRecordRewardGrant& mutation,
    std::optional<std::uint16_t> pendingSeasonReward,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    std::span<const std::byte, state::kAesKeySize> key,
    std::span<const std::byte, state::kBapNonceSize> nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_record_reward_grant(scratch,
                                               before,
                                               update,
                                               mutation,
                                               pendingSeasonReward,
                                               acquisitionPresentationRows,
                                               prepared)
        || prepared.family.type != queuez::kAccountFamilyType
        || prepared.family.rootSoid != before.family4RootSoid
        || prepared.family.version != before.family4Version + 1 || prepared.family.flags != 0
        || prepared.family.objects.size() != update.appendedResidentCount + 2U) {
        return false;
    }
    for (std::size_t index = 0; index < update.appendedResidentCount; ++index) {
        const auto& object = prepared.family.objects[index];
        const auto& resident = update.after.family4Residents[before.family4ResidentCount + index];
        if (object.id != update.itemInstanceDefinitionId || object.version != resident.objectSoid
            || object.payload.empty()) {
            return false;
        }
    }
    const auto& character = prepared.family.objects[update.appendedResidentCount];
    const auto& account = prepared.family.objects[update.appendedResidentCount + 1U];
    return character.id == update.characterDefinitionId && character.version == update.characterSoid
           && !character.payload.empty() && account.id == update.accountDefinitionId
           && account.version == update.accountSoid && !account.payload.empty()
           && queuez_frame::append(scratch,
                                   prepared.family,
                                   prepared.rawClearSize,
                                   prepared.compressedClearSize,
                                   key,
                                   nonce,
                                   response,
                                   written);
}

/** Appends one atomic dismantle update, including any account-wide material payout. */
bool append_item_dismantle_notification(Scratch& scratch,
                                        const queuez::ItemDismantle& dismantle,
                                        const state::PendingItemDismantle& mutation,
                                        std::span<const std::byte, state::kAesKeySize> key,
                                        std::span<const std::byte, state::kBapNonceSize> nonce,
                                        std::span<std::byte> response,
                                        std::size_t& written) noexcept {
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_item_dismantle(scratch, dismantle, mutation, prepared)) {
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t expectedObjectCount = dismantle.updatesAccount ? 3U : 2U;
    if (objectCount != expectedObjectCount
        || prepared.family.objects[0].id != dismantle.characterDefinitionId
        || prepared.family.objects[0].version != dismantle.characterSoid
        || prepared.family.objects[1].id != dismantle.itemInstanceDefinitionId
        || prepared.family.objects[1].version != dismantle.dismantledInstanceSoid
        || prepared.family.objects[1].encoding != middleware::queuez::Encoding::oodle
        || !prepared.family.objects[1].payload.empty()
        || (dismantle.updatesAccount
            && (prepared.family.objects[2].id != dismantle.accountDefinitionId
                || prepared.family.objects[2].version != dismantle.accountSoid
                || prepared.family.objects[2].encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects[2].payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    return true;
}

} // namespace sunrise::server::bap::encrypted::push
