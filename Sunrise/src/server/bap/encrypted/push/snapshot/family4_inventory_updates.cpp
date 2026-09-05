#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family4/account/account_encoder.h"
#include "../../../../../middleware/datagen/family4/account/layout.h"
#include "../../../../../middleware/datagen/family4/character/character_encoder.h"
#include "../../../../../middleware/datagen/family4/character/layout.h"
#include "../../../../../middleware/datagen/family4/instance/instance_encoder.h"
#include "../../../../../middleware/datagen/family4/instance/layout.h"
#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/progression/season_pass_reward_catalog.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../queuez/queuez_state_validation.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace sunrise::server::bap::encrypted::push::snapshot {

namespace family4_datagen = middleware::datagen::family4;

constexpr auto kChangeRecordIsZero = [](const auto& record) noexcept {
    return record.sequence == 0 && record.reserved == 0 && record.mutationSerial == 0
           && record.kind == 0 && record.reservedKind == 0 && record.flags == 0;
};
namespace {

namespace account_layout = middleware::datagen::family4::account::layout;

/**
 * The change ring the native account observer reads.
 *
 * It compares profile quantities but only draws pickup feedback for a row whose mutation serial
 * also appears here, so a gain the ring does not name lands silently. The bank is local to one
 * incremental upsert - an ordinary snapshot encodes it empty - while the row's own rising serial
 * stays persistent State.
 */
constexpr std::uint8_t kChangeKind = 1;
/** Clear policy bits leave the record enabled; the observer skips any other pair. */
constexpr std::uint16_t kChangeFlags = 0;

/** @return True when the ring carries no record, which is how every snapshot encodes it. */
[[nodiscard]] bool ring_is_empty(const account_layout::Object& accountObject) noexcept {
    const auto recordIsZero = [](const account_layout::ProfileInventoryChangeRecord& record) {
        return record.sequence == 0 && record.reserved == 0 && record.mutationSerial == 0
               && record.kind == 0 && record.reservedKind == 0 && record.flags == 0;
    };
    return accountObject.profileInventoryChanges.writeSlot == 0
           && accountObject.profileInventoryChanges.nextSequence == 0
           && std::all_of(accountObject.profileInventoryChanges.records.cbegin(),
                          accountObject.profileInventoryChanges.records.cend(),
                          recordIsZero);
}

/** Points one ring record at one profile row. */
void name_row(account_layout::ProfileInventoryChangeRecord& record,
              std::size_t sequence,
              std::int32_t mutationSerial) noexcept {
    record.sequence = static_cast<std::uint16_t>(sequence);
    record.mutationSerial = mutationSerial;
    record.kind = kChangeKind;
    record.flags = kChangeFlags;
}

/**
 * Names every row an exchange credited, so each gain is drawn and repeats accumulate.
 *
 * @param accountObject Encoded account object being upserted.
 * @param mutation Prepared exchange carrying the rows it credited.
 * @return Null on success, or the reason the ring could not be written.
 */
[[nodiscard]] const char* write_exchange_changes(
    account_layout::Object& accountObject,
    const state::PendingProfileItemAcquisition& mutation) noexcept {
    if (mutation.changeCount > accountObject.profileInventoryChanges.records.size()
        || !ring_is_empty(accountObject)) {
        return "exchange_inventory_change_state";
    }
    for (std::size_t change = 0; change < mutation.changeCount; ++change) {
        const state::ProfileStackChange& announced = mutation.changes[change];
        std::size_t matchedRows = 0;
        for (const auto& row : accountObject.profileItems) {
            if (row.mutationSerial != announced.mutationSerial) {
                continue;
            }
            if (row.quantity != announced.afterQuantity) {
                return "exchange_change_quantity";
            }
            ++matchedRows;
        }
        if (matchedRows != 1) {
            return "exchange_change_row";
        }
        name_row(accountObject.profileInventoryChanges.records[change],
                 change,
                 announced.mutationSerial);
    }
    accountObject.profileInventoryChanges.writeSlot =
        static_cast<std::uint16_t>(mutation.changeCount);
    accountObject.profileInventoryChanges.nextSequence =
        static_cast<std::uint16_t>(mutation.changeCount);
    return nullptr;
}

/**
 * Names the one row an ordinary acquisition added to or grew.
 *
 * @param accountObject Encoded account object being upserted.
 * @param mutation Prepared acquisition naming its acquired row.
 * @param acquiredRow Receives that row's position, for the checkpoint line.
 * @return Null on success, or the reason the ring could not be written.
 */
[[nodiscard]] const char* write_acquisition_change(
    account_layout::Object& accountObject,
    const state::PendingProfileItemAcquisition& mutation,
    std::size_t& acquiredRow) noexcept {
    acquiredRow = accountObject.profileItems.size();
    for (std::size_t row = 0; row < accountObject.profileItems.size(); ++row) {
        if (accountObject.profileItems[row].mutationSerial != mutation.acquiredMutationSerial) {
            continue;
        }
        if (acquiredRow != accountObject.profileItems.size()) {
            return "profile_acquire_row_duplicate";
        }
        acquiredRow = row;
    }
    if (acquiredRow >= accountObject.profileItems.size()
        || accountObject.profileItems[acquiredRow].quantity != mutation.acquiredQuantity
        || !ring_is_empty(accountObject)) {
        return "profile_acquire_inventory_change_state";
    }
    accountObject.profileInventoryChanges.writeSlot = 1;
    accountObject.profileInventoryChanges.nextSequence = 1;
    name_row(accountObject.profileInventoryChanges.records.front(),
             0,
             mutation.acquiredMutationSerial);
    return nullptr;
}

} // namespace

/** Builds a single full account-object upsert from an uncommitted profile-stack after-image. */
bool prepare_profile_item_acquisition(Scratch& scratch,
                                      const queuez::ProfileItemAcquisition& acquisition,
                                      const state::PendingProfileItemAcquisition& mutation,
                                      std::optional<std::uint16_t> pendingSeasonReward,
                                      Prepared& prepared) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("profile_acquire_reservation");
    }
    if (!mutation.prepared || mutation.accountSoid == 0
        || mutation.accountSoid != acquisition.accountSoid
        || mutation.acquiredInstanceSoid != acquisition.acquiredInstanceSoid
        || mutation.actionSource != acquisition.actionSource
        || acquisition.appendedResident != (mutation.appended && mutation.actionSource)
        || acquisition.accountSoid != acquisition.after.family4RootSoid
        || acquisition.accountDefinitionId == 0
        || (acquisition.appendedResident && acquisition.itemInstanceDefinitionId == 0)) {
        return report_failure("profile_acquire_mutation");
    }
    state::AccountState account{};
    if (!state::preview_profile_item_acquisition(mutation, account)
        || account.primarySoid != acquisition.accountSoid || !state::account::valid(account)) {
        return report_failure("profile_acquire_account");
    }

    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (family4_datagen::account::layout::kObjectSize > rawStorage.size()) {
        return report_failure("profile_acquire_account_storage");
    }
    const auto accountBytes = rawStorage.first(family4_datagen::account::layout::kObjectSize);
    if (!family4_datagen::account::encode(account, accountBytes, pendingSeasonReward)) {
        return report_failure("profile_acquire_account_encode");
    }

    auto& accountObject =
        *reinterpret_cast<family4_datagen::account::layout::Object*>(accountBytes.data());
    std::size_t acquiredRow = accountObject.profileItems.size();
    // An exchange names every row it credited; an ordinary acquisition names the one row it added
    // to or grew. Both write the same kind of record, which is what the observer draws.
    const char* const ringFailure =
        mutation.changeCount != 0
            ? write_exchange_changes(accountObject, mutation)
            : write_acquisition_change(accountObject, mutation, acquiredRow);
    if (ringFailure != nullptr) {
        clear_after(scratch, reservation);
        return report_failure(ringFailure);
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset + family4_datagen::account::layout::kObjectSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    const std::size_t accountObjectIndex = acquisition.appendedResident ? 1U : 0U;
    if (!append_object(scratch,
                       accountBytes,
                       acquisition.accountDefinitionId,
                       acquisition.accountSoid,
                       staged.objects[accountObjectIndex],
                       compressedExtent)) {
        return report_failure("profile_acquire_account_object");
    }
    std::size_t objectCount = 1;
    if (acquisition.appendedResident) {
        if (mutation.profileIndex >= account.profileItemCount) {
            return report_failure("profile_acquire_instance_index");
        }
        family4_datagen::instance::ResolvedInstance instance{};
        const state::account::inventory::ProfileItem& profileItem =
            account.profileItems[mutation.profileIndex];
        const auto instanceBytes = rawStorage.first(family4_datagen::instance::layout::kObjectSize);
        if (!resolve_profile_item_instance(profileItem, instance)
            || instance.instanceSoid != acquisition.acquiredInstanceSoid
            || !family4_datagen::instance::encode(instance, instanceBytes)
            || !append_object(scratch,
                              instanceBytes,
                              acquisition.itemInstanceDefinitionId,
                              acquisition.acquiredInstanceSoid,
                              staged.objects.front(),
                              compressedExtent)) {
            return report_failure("profile_acquire_instance_object");
        }
        objectCount = 2;
    }
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kAccountFamilyType,
        acquisition.after.family4RootSoid,
        acquisition.after.family4Version,
        0,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("profile_acquire_commit");
    }

    return true;
}

/** Builds the native XP pickup signal without making its one-slot reward item persistent. */
bool prepare_seasonal_experience_presentation(
    Scratch& scratch,
    const queuez::SessionState& before,
    std::int32_t amount,
    std::int32_t mutationSerial,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    Prepared& prepared) noexcept {
    constexpr std::uint32_t kExperienceItemHash = 2211488305U;
    constexpr std::size_t kBitsPerFlagByte = 8;
    constexpr std::int32_t kOccupiedRowWatermark = 1;
    constexpr std::uint16_t kChangeSequence = 0;
    constexpr std::uint16_t kChangeNextWriteSlot = 1;
    constexpr std::uint16_t kChangeNextSequence = 1;

    if (amount <= 0 || mutationSerial < 0 || !queuez::valid(before) || !before.family4Active
        || before.family4RootSoid == 0 || before.family4ResidentCount == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()) {
        return report_failure("season_xp_session");
    }

    const state::AccountState account = state::account_snapshot();
    const std::optional<std::size_t> selectedIndex = find_character_index(account);
    Resolved selected{};
    state::build_data::items::Definition item{};
    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (!state::account::valid(account) || account.primarySoid != before.family4RootSoid
        || !selectedIndex.has_value() || !resolve(account, *selectedIndex, selected)
        || !state::build_data::find_item_definition_hash(kExperienceItemHash, item)
        || !state::build_data::find_configured_item_detail(item.definitionIndex, detail)
        || detail.definitionIndex != item.definitionIndex
        || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
        || detail.instancedDefinitionState
               != state::build_data::items::details::InstancedDefinitionState::stackable
        || detail.maxStackSize < 1 || detail.equipmentSlot.has_value()
        || !state::build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)
        || bucket.arraySelector != state::build_data::inventory::buckets::ArraySelector::character
        || bucket.slotCount != 1
        || bucket.firstSlot
               >= middleware::datagen::family4::character::layout::kInventoryCapacity) {
        return report_failure("season_xp_definition");
    }

    const state::CharacterState& character = account.characters[*selectedIndex];
    std::size_t characterResidentMatches = 0;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const queuez::ResidentObject& resident = before.family4Residents[index];
        characterResidentMatches +=
            static_cast<std::size_t>(resident.objectSoid == character.soid
                                     && resident.definitionId == selected.characterObjectId);
    }
    if (before.family4Residents.front().objectSoid != account.primarySoid
        || characterResidentMatches != 1
        || static_cast<std::uint32_t>(mutationSerial) >= character.nextInventorySerial) {
        return report_failure("season_xp_manifest");
    }

    const Reservation reservation = reserve_prior(scratch, prepared);
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (middleware::datagen::family4::character::layout::kObjectSize > rawStorage.size()) {
        return report_failure("season_xp_character_storage");
    }
    const auto characterBytes =
        rawStorage.first(middleware::datagen::family4::character::layout::kObjectSize);
    if (!middleware::datagen::family4::character::encode(
            character, selected.loadout, selected.lightEvaluation, characterBytes)) {
        return report_failure("season_xp_character_encode");
    }

    namespace character_layout = middleware::datagen::family4::character::layout;
    auto& characterObject = *reinterpret_cast<character_layout::Object*>(characterBytes.data());
    const std::size_t rowIndex = bucket.firstSlot;
    auto& row = characterObject.inventoryItems[rowIndex];
    if (row.definitionIndex != (std::numeric_limits<std::uint16_t>::max)()
        || characterObject.inventoryChanges.writeSlot != 0
        || characterObject.inventoryChanges.nextSequence != 0
        || !std::all_of(characterObject.inventoryChanges.records.cbegin(),
                        characterObject.inventoryChanges.records.cend(),
                        kChangeRecordIsZero)) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_character_state");
    }

    row.definitionIndex = item.definitionIndex;
    // The virtual item carries the gain; the account object carries cumulative XP.
    row.quantity = amount;
    row.mutationSerial = mutationSerial;
    characterObject.newItemFlags[rowIndex / kBitsPerFlagByte] |= std::byte{1U}
                                                                 << (rowIndex % kBitsPerFlagByte);
    characterObject.instanceProgressWatermarks[rowIndex] = kOccupiedRowWatermark;
    characterObject.inventoryChanges.writeSlot = kChangeNextWriteSlot;
    characterObject.inventoryChanges.nextSequence = kChangeNextSequence;
    auto& change = characterObject.inventoryChanges.records.front();
    change.sequence = kChangeSequence;
    change.mutationSerial = mutationSerial;
    change.kind = kChangeKind;
    change.flags = kChangeFlags;
    if (!apply_acquisition_presentation(
            characterBytes, selected.loadout, acquisitionPresentationRows)) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_presentation");
    }

    Prepared staged{};
    staged.rawClearSize = (std::max)(reservation.rawClearSize,
                                     reservation.rawWriteOffset + character_layout::kObjectSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    if (!append_object(scratch,
                       characterBytes,
                       selected.characterObjectId,
                       character.soid,
                       staged.objects[1],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_character_object");
    }

    if (middleware::datagen::family4::account::layout::kObjectSize > rawStorage.size()) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_account_storage");
    }
    const auto accountBytes =
        rawStorage.first(middleware::datagen::family4::account::layout::kObjectSize);
    // Publish cumulative account XP before the transient delta row triggers the HUD.
    if (!middleware::datagen::family4::account::encode(account, accountBytes)
        || !append_object(scratch,
                          accountBytes,
                          before.family4Residents.front().definitionId,
                          account.primarySoid,
                          staged.objects[0],
                          compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_account_object");
    }

    staged.rawClearSize =
        (std::max)(staged.rawClearSize,
                   reservation.rawWriteOffset
                       + middleware::datagen::family4::account::layout::kObjectSize);
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        queuez::kAccountFamilyType,
        account.primarySoid,
        before.family4Version + 1,
        0,
        std::span(staged.objects).first(2),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("season_xp_commit");
    }
    return true;
}

/** Builds one atomic Season package update with one acquisition record per granted item. */
bool prepare_season_pass_package(
    Scratch& scratch,
    const queuez::SessionState& before,
    const state::PendingDirectItemBundle& mutation,
    std::uint16_t rewardIndex,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    Prepared& prepared) noexcept {
    namespace character_layout = family4_datagen::character::layout;
    constexpr std::uint8_t kAcquisitionChangeKind = 1;
    constexpr std::uint16_t kAcquisitionChangeFlags = 0;
    const std::size_t itemCount = mutation.itemCount;
    const auto* reward = state::progression::season_pass::find(rewardIndex);
    if (itemCount == 0 || itemCount > character_layout::kInventoryChangeRecordCapacity
        || !mutation.prepared || reward == nullptr || reward->quantity != 1
        || reward->itemHash != mutation.sourceDefinitionHash
        || state::progression::season_pass::find_premium_class_package(reward->itemHash) == nullptr
        || !queuez::valid(before) || !before.family4Active || before.family4ResidentCount == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || before.family4ResidentCount + itemCount > before.family4Residents.size()) {
        return report_failure("season_package_session");
    }

    state::AccountState account{};
    if (!state::preview_direct_item_bundle(mutation, account)) {
        return report_failure("season_package_preview");
    }
    const std::optional<std::size_t> selectedIndex = find_character_index(account);
    Resolved selected{};
    if (!state::account::valid(account) || account.primarySoid != before.family4RootSoid
        || account.primarySoid != mutation.accountSoid || !selectedIndex.has_value()
        || *selectedIndex != mutation.characterIndex
        || !resolve(account, *selectedIndex, selected)) {
        return report_failure("season_package_account");
    }
    const state::CharacterState& character = account.characters[*selectedIndex];
    if (character.soid != mutation.characterSoid
        || mutation.expectedInventoryCount > character.inventory.count
        || character.inventory.count != mutation.expectedInventoryCount + itemCount) {
        return report_failure("season_package_count");
    }

    std::uint32_t itemInstanceObjectId = 0;
    if (!middleware::datagen::object_id(queuez::kAccountFamilyType,
                                        middleware::datagen::kItemInstanceSlot,
                                        itemInstanceObjectId)
        || before.family4Residents.front().objectSoid != account.primarySoid
        || before.family4Residents.front().definitionId == 0) {
        return report_failure("season_package_definitions");
    }
    std::size_t characterResidentMatches = 0;
    for (std::size_t index = 0; index < before.family4ResidentCount; ++index) {
        const queuez::ResidentObject& resident = before.family4Residents[index];
        characterResidentMatches +=
            static_cast<std::size_t>(resident.objectSoid == character.soid
                                     && resident.definitionId == selected.characterObjectId);
    }
    if (characterResidentMatches != 1) {
        return report_failure("season_package_character_resident");
    }

    family4_datagen::loadout::ResolvedInstances acquired{};
    const std::size_t firstGranted = mutation.expectedInventoryCount;
    for (std::size_t grantIndex = 0; grantIndex < itemCount; ++grantIndex) {
        const auto& granted = character.inventory.values[firstGranted + grantIndex];
        if (granted.instanceSoid != mutation.firstInstanceSoid + grantIndex
            || granted.mutationSerial < 0) {
            return report_failure("season_package_item_identity");
        }
        for (std::size_t residentIndex = 0; residentIndex < before.family4ResidentCount;
             ++residentIndex) {
            if (before.family4Residents[residentIndex].objectSoid == granted.instanceSoid) {
                return report_failure("season_package_item_resident");
            }
        }
        std::size_t matches = 0;
        for (std::size_t loadoutIndex = 0; loadoutIndex < selected.loadout.itemCount;
             ++loadoutIndex) {
            const auto& resolvedItem = selected.loadout.items[loadoutIndex];
            if (resolvedItem.instance.instanceSoid != granted.instanceSoid) {
                continue;
            }
            acquired.items[grantIndex].equipmentSlot = resolvedItem.equipmentSlot;
            acquired.items[grantIndex].instance = resolvedItem.instance;
            if (resolvedItem.mutationSerial != granted.mutationSerial) {
                return report_failure("season_package_item_serial");
            }
            ++matches;
        }
        if (matches != 1) {
            return report_failure("season_package_item_loadout");
        }
    }
    acquired.itemCount = itemCount;

    const Reservation reservation = reserve_prior(scratch, prepared);
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (character_layout::kObjectSize > rawStorage.size()
        || family4_datagen::account::layout::kObjectSize > rawStorage.size()) {
        return report_failure("season_package_storage");
    }

    Prepared staged{};
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t itemCursor = 0;
    if (!append_items(scratch,
                      rawStorage,
                      itemInstanceObjectId,
                      acquired,
                      0,
                      staged,
                      itemCursor,
                      compressedExtent)
        || itemCursor != itemCount) {
        clear_after(scratch, reservation);
        return report_failure("season_package_item_objects");
    }

    const auto characterBytes = rawStorage.first(character_layout::kObjectSize);
    if (!family4_datagen::character::encode(
            character, selected.loadout, selected.lightEvaluation, characterBytes)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_character_encode");
    }
    auto& characterObject = *reinterpret_cast<character_layout::Object*>(characterBytes.data());
    if (characterObject.inventoryChanges.writeSlot != 0
        || characterObject.inventoryChanges.nextSequence != 0
        || !std::all_of(characterObject.inventoryChanges.records.cbegin(),
                        characterObject.inventoryChanges.records.cend(),
                        kChangeRecordIsZero)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_change_state");
    }
    for (std::size_t index = 0; index < itemCount; ++index) {
        auto& change = characterObject.inventoryChanges.records[index];
        change.sequence = static_cast<std::uint16_t>(index);
        change.mutationSerial = character.inventory.values[firstGranted + index].mutationSerial;
        change.kind = kAcquisitionChangeKind;
        change.flags = kAcquisitionChangeFlags;
    }
    characterObject.inventoryChanges.writeSlot = static_cast<std::uint16_t>(itemCount);
    characterObject.inventoryChanges.nextSequence = static_cast<std::uint16_t>(itemCount);
    if (!apply_acquisition_presentation(
            characterBytes, selected.loadout, acquisitionPresentationRows)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_presentation");
    }
    if (!append_object(scratch,
                       characterBytes,
                       selected.characterObjectId,
                       character.soid,
                       staged.objects[itemCount],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_character_object");
    }

    const auto accountBytes = rawStorage.first(family4_datagen::account::layout::kObjectSize);
    if (!family4_datagen::account::encode(account, accountBytes, rewardIndex)
        || !append_object(scratch,
                          accountBytes,
                          before.family4Residents.front().definitionId,
                          account.primarySoid,
                          staged.objects[itemCount + 1U],
                          compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_account_object");
    }

    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset
                       + (std::max)(character_layout::kObjectSize,
                                    family4_datagen::account::layout::kObjectSize));
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{queuez::kAccountFamilyType,
                                               account.primarySoid,
                                               before.family4Version + 1,
                                               0,
                                               std::span(staged.objects).first(itemCount + 2U)};
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("season_package_commit");
    }
    return true;
}

/** Builds all record rewards and the pending claim into one Family-4 revision. */
bool prepare_record_reward_grant(
    Scratch& scratch,
    const queuez::SessionState& before,
    const queuez::RecordRewardGrant& update,
    const state::PendingRecordRewardGrant& mutation,
    std::optional<std::uint16_t> pendingSeasonReward,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    Prepared& prepared) noexcept {
    namespace account_layout = family4_datagen::account::layout;
    namespace character_layout = family4_datagen::character::layout;
    if (!mutation.prepared || mutation.rewardCount == 0
        || mutation.rewardCount > mutation.rewards.size() || !queuez::valid(before)
        || !queuez::valid(update.after) || !before.family4Active || before.family4ResidentCount == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || update.accountSoid != mutation.accountSoid
        || update.characterSoid != mutation.characterSoid
        || update.accountSoid != before.family4RootSoid
        || update.after.family4Version != before.family4Version + 1
        || update.appendedResidentCount > mutation.rewardCount
        || update.after.family4ResidentCount
               != before.family4ResidentCount + update.appendedResidentCount
        || update.accountDefinitionId != before.family4Residents.front().definitionId
        || update.characterDefinitionId == 0 || update.itemInstanceDefinitionId == 0) {
        return report_failure("record_reward_session");
    }

    state::AccountState account{};
    Resolved selected{};
    if (!state::preview_record_reward_grant(mutation, account)
        || mutation.characterIndex >= account.characterCount
        || account.primarySoid != update.accountSoid
        || account.characters[mutation.characterIndex].soid != update.characterSoid
        || !resolve(account, mutation.characterIndex, selected)
        || selected.characterObjectId != update.characterDefinitionId
        || selected.itemInstanceObjectId != update.itemInstanceDefinitionId) {
        return report_failure("record_reward_account");
    }

    family4_datagen::loadout::ResolvedInstances residents{};
    for (std::size_t rewardIndex = 0; rewardIndex < mutation.rewardCount; ++rewardIndex) {
        const state::PreparedRecordReward& reward = mutation.rewards[rewardIndex];
        if (reward.kind == state::RecordRewardKind::characterInstance) {
            std::size_t matches = 0;
            for (std::size_t itemIndex = 0; itemIndex < selected.loadout.itemCount; ++itemIndex) {
                const auto& item = selected.loadout.items[itemIndex];
                if (item.instance.instanceSoid != reward.instanceSoid) {
                    continue;
                }
                if (item.equipped || item.inventoryRow != reward.inventoryRow
                    || item.mutationSerial != reward.mutationSerial) {
                    return report_failure("record_reward_character_item");
                }
                residents.items[residents.itemCount++] = {item.equipmentSlot, item.instance};
                ++matches;
            }
            if (matches != 1) {
                return report_failure("record_reward_character_instance");
            }
        } else if (reward.kind == state::RecordRewardKind::profileStack
                   && reward.appendedProfileResident) {
            if (reward.stateIndex >= account.profileItemCount
                || residents.itemCount >= residents.items.size()
                || !resolve_profile_item_instance(account.profileItems[reward.stateIndex],
                                                  residents.items[residents.itemCount].instance)) {
                return report_failure("record_reward_profile_instance");
            }
            ++residents.itemCount;
        } else if (reward.kind == state::RecordRewardKind::profileStack
                   && reward.instanceSoid != 0) {
            std::size_t matches = 0;
            for (std::size_t residentIndex = 0; residentIndex < before.family4ResidentCount;
                 ++residentIndex) {
                const auto& resident = before.family4Residents[residentIndex];
                matches += static_cast<std::size_t>(resident.objectSoid == reward.instanceSoid
                                                    && resident.definitionId
                                                           == update.itemInstanceDefinitionId);
            }
            if (matches != 1) {
                return report_failure("record_reward_profile_resident");
            }
        }
    }
    if (residents.itemCount != update.appendedResidentCount) {
        return report_failure("record_reward_resident_count");
    }
    for (std::size_t index = 0; index < residents.itemCount; ++index) {
        const auto& expected = update.after.family4Residents[before.family4ResidentCount + index];
        if (expected.objectSoid != residents.items[index].instance.instanceSoid
            || expected.definitionId != update.itemInstanceDefinitionId) {
            return report_failure("record_reward_resident_order");
        }
    }

    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("record_reward_reservation");
    }
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (account_layout::kObjectSize > rawStorage.size()
        || character_layout::kObjectSize > rawStorage.size()) {
        return report_failure("record_reward_storage");
    }

    Prepared staged{};
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t residentCursor = 0;
    if (!append_items(scratch,
                      rawStorage,
                      update.itemInstanceDefinitionId,
                      residents,
                      0,
                      staged,
                      residentCursor,
                      compressedExtent)
        || residentCursor != residents.itemCount) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_residents");
    }

    const auto characterBytes = rawStorage.first(character_layout::kObjectSize);
    const state::CharacterState& character = account.characters[mutation.characterIndex];
    if (!family4_datagen::character::encode(character,
                                            selected.loadout,
                                            selected.lightEvaluation,
                                            characterBytes,
                                            pendingSeasonReward.has_value() ? nullptr
                                                                            : &mutation.claim)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_character_encode");
    }
    auto& characterObject = *reinterpret_cast<character_layout::Object*>(characterBytes.data());
    if (characterObject.inventoryChanges.writeSlot != 0
        || characterObject.inventoryChanges.nextSequence != 0
        || !std::all_of(characterObject.inventoryChanges.records.cbegin(),
                        characterObject.inventoryChanges.records.cend(),
                        kChangeRecordIsZero)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_character_changes");
    }
    std::size_t characterChanges = 0;
    for (std::size_t rewardIndex = 0; rewardIndex < mutation.rewardCount; ++rewardIndex) {
        const state::PreparedRecordReward& reward = mutation.rewards[rewardIndex];
        if (reward.kind == state::RecordRewardKind::profileStack) {
            continue;
        }
        state::build_data::items::Definition definition{};
        if (characterChanges >= characterObject.inventoryChanges.records.size()
            || !state::build_data::find_item_definition_hash(reward.definitionHash, definition)) {
            clear_after(scratch, reservation);
            return report_failure("record_reward_character_definition");
        }
        std::size_t found = characterObject.inventoryItems.size();
        for (std::size_t row = 0; row < characterObject.inventoryItems.size(); ++row) {
            const auto& item = characterObject.inventoryItems[row];
            const bool matches = reward.kind == state::RecordRewardKind::characterInstance
                                     ? item.instanceSoid == reward.instanceSoid
                                     : item.instanceSoid == 0
                                           && item.definitionIndex == definition.definitionIndex
                                           && item.mutationSerial == reward.mutationSerial;
            if (!matches) {
                continue;
            }
            if (found != characterObject.inventoryItems.size()
                || item.quantity != reward.afterQuantity) {
                clear_after(scratch, reservation);
                return report_failure("record_reward_character_row");
            }
            found = row;
        }
        if (found == characterObject.inventoryItems.size()) {
            clear_after(scratch, reservation);
            return report_failure("record_reward_character_row_missing");
        }
        auto& change = characterObject.inventoryChanges.records[characterChanges];
        change.sequence = static_cast<std::uint16_t>(characterChanges);
        change.mutationSerial = reward.mutationSerial;
        change.kind = kChangeKind;
        change.flags = kChangeFlags;
        ++characterChanges;
    }
    characterObject.inventoryChanges.writeSlot = static_cast<std::uint16_t>(characterChanges);
    characterObject.inventoryChanges.nextSequence = static_cast<std::uint16_t>(characterChanges);
    if (!apply_acquisition_presentation(
            characterBytes, selected.loadout, acquisitionPresentationRows)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_presentation");
    }
    if (!append_object(scratch,
                       characterBytes,
                       update.characterDefinitionId,
                       update.characterSoid,
                       staged.objects[residentCursor],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_character_object");
    }

    const auto accountBytes = rawStorage.first(account_layout::kObjectSize);
    if (!family4_datagen::account::encode(account,
                                          accountBytes,
                                          pendingSeasonReward,
                                          pendingSeasonReward.has_value() ? nullptr
                                                                          : &mutation.claim)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_account_encode");
    }
    auto& accountObject = *reinterpret_cast<account_layout::Object*>(accountBytes.data());
    if (accountObject.profileInventoryChanges.writeSlot != 0
        || accountObject.profileInventoryChanges.nextSequence != 0
        || !std::all_of(accountObject.profileInventoryChanges.records.cbegin(),
                        accountObject.profileInventoryChanges.records.cend(),
                        kChangeRecordIsZero)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_profile_changes");
    }
    std::size_t profileChanges = 0;
    for (std::size_t rewardIndex = 0; rewardIndex < mutation.rewardCount; ++rewardIndex) {
        const state::PreparedRecordReward& reward = mutation.rewards[rewardIndex];
        if (reward.kind != state::RecordRewardKind::profileStack) {
            continue;
        }
        state::build_data::items::Definition definition{};
        if (profileChanges >= accountObject.profileInventoryChanges.records.size()
            || !state::build_data::find_item_definition_hash(reward.definitionHash, definition)) {
            clear_after(scratch, reservation);
            return report_failure("record_reward_profile_definition");
        }
        std::size_t found = accountObject.profileItems.size();
        for (std::size_t row = 0; row < accountObject.profileItems.size(); ++row) {
            const auto& item = accountObject.profileItems[row];
            if (item.definitionIndex != definition.definitionIndex
                || item.mutationSerial != reward.mutationSerial) {
                continue;
            }
            if (found != accountObject.profileItems.size()
                || item.quantity != reward.afterQuantity) {
                clear_after(scratch, reservation);
                return report_failure("record_reward_profile_row");
            }
            found = row;
        }
        if (found == accountObject.profileItems.size()) {
            clear_after(scratch, reservation);
            return report_failure("record_reward_profile_row_missing");
        }
        auto& change = accountObject.profileInventoryChanges.records[profileChanges];
        change.sequence = static_cast<std::uint16_t>(profileChanges);
        change.mutationSerial = reward.mutationSerial;
        change.kind = kChangeKind;
        change.flags = kChangeFlags;
        ++profileChanges;
    }
    accountObject.profileInventoryChanges.writeSlot = static_cast<std::uint16_t>(profileChanges);
    accountObject.profileInventoryChanges.nextSequence = static_cast<std::uint16_t>(profileChanges);
    if (!append_object(scratch,
                       accountBytes,
                       update.accountDefinitionId,
                       update.accountSoid,
                       staged.objects[residentCursor + 1U],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_account_object");
    }

    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset
                       + (std::max)(account_layout::kObjectSize, character_layout::kObjectSize));
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        queuez::kAccountFamilyType,
        update.accountSoid,
        update.after.family4Version,
        0,
        std::span(staged.objects).first(residentCursor + 2U),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("record_reward_commit");
    }
    return true;
}

/** Builds a new item-instance upsert before its character after-image. */
bool prepare_item_acquisition(
    Scratch& scratch,
    const queuez::ItemAcquisition& acquisition,
    const state::PendingItemAcquisition& mutation,
    std::optional<std::uint16_t> pendingSeasonReward,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    Prepared& prepared) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("acquire_reservation");
    }

    state::AccountState account{};
    if (!mutation.prepared || mutation.characterSoid == 0 || mutation.acquiredInstanceSoid == 0
        || mutation.accountSoid == 0 || mutation.accountSoid != acquisition.accountSoid
        || mutation.characterSoid != acquisition.characterSoid
        || mutation.acquiredInstanceSoid != acquisition.acquiredInstanceSoid
        || acquisition.updatesAccount
               != (mutation.profileChanged || pendingSeasonReward.has_value())
        || acquisition.accountSoid != acquisition.after.family4RootSoid
        || acquisition.accountDefinitionId == 0 || acquisition.after.family4ResidentCount == 0
        || acquisition.after.family4Residents[acquisition.after.family4ResidentCount - 1U]
                   .objectSoid
               != mutation.acquiredInstanceSoid
        || acquisition.after.family4Residents[acquisition.after.family4ResidentCount - 1U]
                   .definitionId
               != acquisition.itemInstanceDefinitionId) {
        return report_failure("acquire_mutation");
    }
    if (!state::preview_item_acquisition(mutation, account)
        || mutation.characterIndex >= account.characterCount
        || account.primarySoid != acquisition.accountSoid
        || account.characters[mutation.characterIndex].soid != mutation.characterSoid) {
        return report_failure("acquire_account");
    }

    Resolved selected{};
    const std::optional<std::size_t> selectedIndex = find_character_index(account);
    if (!state::account::valid(account) || !selectedIndex.has_value()
        || *selectedIndex != mutation.characterIndex
        || !resolve(account, mutation.characterIndex, selected)
        || selected.characterObjectId != acquisition.characterDefinitionId
        || selected.itemInstanceObjectId != acquisition.itemInstanceDefinitionId) {
        return report_failure("acquire_selection");
    }

    family4_datagen::loadout::ResolvedInstances acquired{};
    std::int32_t acquiredMutationSerial = -1;
    for (std::size_t index = 0; index < selected.loadout.itemCount; ++index) {
        const family4_datagen::loadout::ResolvedItem& item = selected.loadout.items[index];
        if (item.instance.instanceSoid != mutation.acquiredInstanceSoid) {
            continue;
        }
        if (acquired.itemCount != 0 || item.equipped || item.inventoryRow != mutation.inventoryRow
            || item.equipmentSlot != mutation.equipmentSlot) {
            return report_failure("acquire_item_row");
        }
        acquired.items[0] = {item.equipmentSlot, item.instance};
        acquired.itemCount = 1;
        acquiredMutationSerial = item.mutationSerial;
    }
    if (acquired.itemCount != 1 || acquiredMutationSerial < 0) {
        return report_failure("acquire_item_missing");
    }

    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (family4_datagen::character::layout::kObjectSize > rawStorage.size()) {
        return report_failure("acquire_character_storage");
    }
    const auto characterBytes = rawStorage.first(family4_datagen::character::layout::kObjectSize);
    if (!family4_datagen::character::encode(account.characters[mutation.characterIndex],
                                            selected.loadout,
                                            selected.lightEvaluation,
                                            characterBytes)) {
        return report_failure("acquire_character_object");
    }

    // Acquisition feedback requires a transient change record matching the newly filled row.
    // Keep it local to this push; later snapshots encode an empty bank.
    constexpr std::size_t kBitsPerFlagByte = 8;
    constexpr std::int32_t kEncodedOccupiedRowWatermark = 1;
    constexpr std::uint16_t kAcquisitionChangeSequence = 0;
    constexpr std::uint16_t kAcquisitionChangeNextWriteSlot = 1;
    constexpr std::uint16_t kAcquisitionChangeNextSequence = 1;
    constexpr std::uint8_t kAcquisitionChangeKind = 1;
    constexpr std::uint16_t kAcquisitionChangeFlags = 0;
    auto& characterObject =
        *reinterpret_cast<family4_datagen::character::layout::Object*>(characterBytes.data());
    const std::size_t acquiredRow = mutation.inventoryRow;
    if (acquiredRow >= characterObject.inventoryItems.size()
        || acquiredRow >= characterObject.instanceProgressWatermarks.size()) {
        clear_after(scratch, reservation);
        return report_failure("acquire_new_item_row");
    }
    const std::size_t newItemFlagIndex = acquiredRow / kBitsPerFlagByte;
    const std::byte newItemFlagMask = std::byte{1U} << (acquiredRow % kBitsPerFlagByte);
    const bool unknownIsZero =
        std::all_of(characterObject.inventoryChangeUnknown.cbegin(),
                    characterObject.inventoryChangeUnknown.cend(),
                    [](std::byte value) noexcept { return value == std::byte{}; });
    const bool recordsAreZero = std::all_of(characterObject.inventoryChanges.records.cbegin(),
                                            characterObject.inventoryChanges.records.cend(),
                                            kChangeRecordIsZero);
    const auto& acquiredInventoryRow = characterObject.inventoryItems[acquiredRow];
    if (newItemFlagIndex >= characterObject.newItemFlags.size()
        || acquiredInventoryRow.instanceSoid != mutation.acquiredInstanceSoid
        || acquiredInventoryRow.mutationSerial != acquiredMutationSerial
        || (characterObject.newItemFlags[newItemFlagIndex] & newItemFlagMask) != newItemFlagMask
        || characterObject.instanceProgressWatermarks[acquiredRow] != kEncodedOccupiedRowWatermark
        || !unknownIsZero || characterObject.inventoryChanges.writeSlot != 0
        || characterObject.inventoryChanges.nextSequence != 0 || !recordsAreZero) {
        clear_after(scratch, reservation);
        return report_failure("acquire_inventory_change_state");
    }
    characterObject.inventoryChanges.writeSlot = kAcquisitionChangeNextWriteSlot;
    characterObject.inventoryChanges.nextSequence = kAcquisitionChangeNextSequence;
    auto& acquisitionChange = characterObject.inventoryChanges.records.front();
    acquisitionChange.sequence = kAcquisitionChangeSequence;
    acquisitionChange.mutationSerial = acquiredMutationSerial;
    acquisitionChange.kind = kAcquisitionChangeKind;
    acquisitionChange.flags = kAcquisitionChangeFlags;
    if (!std::all_of(characterObject.inventoryChanges.records.cbegin() + 1,
                     characterObject.inventoryChanges.records.cend(),
                     kChangeRecordIsZero)) {
        clear_after(scratch, reservation);
        return report_failure("acquire_inventory_change_records");
    }
    if (!apply_acquisition_presentation(
            characterBytes, selected.loadout, acquisitionPresentationRows)) {
        clear_after(scratch, reservation);
        return report_failure("acquire_presentation");
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset + family4_datagen::character::layout::kObjectSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    if (!append_object(scratch,
                       characterBytes,
                       acquisition.characterDefinitionId,
                       acquisition.characterSoid,
                       staged.objects[0],
                       compressedExtent)) {
        return report_failure("acquire_character_object");
    }

    std::size_t itemCursor = 0;
    if (!append_items(scratch,
                      rawStorage,
                      acquisition.itemInstanceDefinitionId,
                      acquired,
                      1,
                      staged,
                      itemCursor,
                      compressedExtent)
        || itemCursor != 1) {
        clear_after(scratch, reservation);
        return report_failure("acquire_item_object");
    }

    const auto& acquiredObject =
        *reinterpret_cast<const family4_datagen::instance::layout::Object*>(rawStorage.data());
    if (acquiredObject.instanceSoid != mutation.acquiredInstanceSoid
        || acquiredObject.roll.progress
               != family4_datagen::instance::layout::kInitialInstanceProgress) {
        clear_after(scratch, reservation);
        return report_failure("acquire_item_progress");
    }
    std::size_t objectCount = 2;
    if (acquisition.updatesAccount) {
        if (family4_datagen::account::layout::kObjectSize > rawStorage.size()) {
            clear_after(scratch, reservation);
            return report_failure("acquire_account_storage");
        }
        const auto accountBytes = rawStorage.first(family4_datagen::account::layout::kObjectSize);
        if (!family4_datagen::account::encode(account, accountBytes, pendingSeasonReward)
            || !append_object(scratch,
                              accountBytes,
                              acquisition.accountDefinitionId,
                              acquisition.accountSoid,
                              staged.objects[2],
                              compressedExtent)) {
            clear_after(scratch, reservation);
            return report_failure("acquire_account_object");
        }
        staged.rawClearSize =
            (std::max)(staged.rawClearSize,
                       reservation.rawWriteOffset + family4_datagen::account::layout::kObjectSize);
        objectCount = 3;
    }

    // Publish the new item before the character that references it. Dismantle uses the inverse
    // order, dropping the character reference before releasing the item.
    std::swap(staged.objects[0], staged.objects[1]);

    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kAccountFamilyType,
        acquisition.after.family4RootSoid,
        acquisition.after.family4Version,
        0,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("acquire_commit");
    }

    return true;
}

/** Builds character removal, instance release, and any profile-reward pickup descriptors. */
bool prepare_item_dismantle(Scratch& scratch,
                            const queuez::ItemDismantle& dismantle,
                            const state::PendingItemDismantle& mutation,
                            Prepared& prepared) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("dismantle_reservation");
    }

    state::AccountState account{};
    if (!mutation.prepared || mutation.characterSoid == 0 || mutation.dismantledInstanceSoid == 0
        || mutation.dismantledItem.instanceSoid != mutation.dismantledInstanceSoid
        || mutation.accountSoid != dismantle.accountSoid
        || mutation.characterSoid != dismantle.characterSoid
        || mutation.dismantledInstanceSoid != dismantle.dismantledInstanceSoid
        || mutation.profileChanged != dismantle.updatesAccount
        || mutation.rewardCount > state::kDismantleRewardCapacity
        || mutation.profileChanged != (mutation.rewardCount != 0)
        || dismantle.accountDefinitionId == 0 || dismantle.characterDefinitionId == 0
        || dismantle.itemInstanceDefinitionId == 0
        || !state::preview_item_dismantle(mutation, account)
        || mutation.characterIndex >= account.characterCount
        || account.primarySoid != dismantle.accountSoid
        || account.characters[mutation.characterIndex].soid != mutation.characterSoid) {
        return report_failure("dismantle_mutation");
    }

    Resolved selected{};
    const std::optional<std::size_t> selectedIndex = find_character_index(account);
    if (!state::account::valid(account) || !selectedIndex.has_value()
        || *selectedIndex != mutation.characterIndex
        || !resolve(account, mutation.characterIndex, selected)
        || selected.characterObjectId != dismantle.characterDefinitionId
        || selected.itemInstanceObjectId != dismantle.itemInstanceDefinitionId) {
        return report_failure("dismantle_selection");
    }
    for (std::size_t index = 0; index < selected.loadout.itemCount; ++index) {
        if (selected.loadout.items[index].instance.instanceSoid
            == mutation.dismantledInstanceSoid) {
            return report_failure("dismantle_item_present");
        }
    }

    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (family4_datagen::character::layout::kObjectSize > rawStorage.size()) {
        return report_failure("dismantle_character_storage");
    }
    const auto characterBytes = rawStorage.first(family4_datagen::character::layout::kObjectSize);
    if (!family4_datagen::character::encode(account.characters[mutation.characterIndex],
                                            selected.loadout,
                                            selected.lightEvaluation,
                                            characterBytes)) {
        return report_failure("dismantle_character_object");
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset + family4_datagen::character::layout::kObjectSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    if (!append_object(scratch,
                       characterBytes,
                       dismantle.characterDefinitionId,
                       dismantle.characterSoid,
                       staged.objects[0],
                       compressedExtent)) {
        return report_failure("dismantle_character_object");
    }
    // Queuez represents a release with the ordinary object key and an empty payload. The
    // encoding selector is not read for empty descriptors; oodle matches the surrounding objects.
    staged.objects[1] = middleware::queuez::Object{
        dismantle.itemInstanceDefinitionId,
        dismantle.dismantledInstanceSoid,
        middleware::queuez::Encoding::oodle,
        {},
    };

    std::size_t objectCount = 2;
    if (dismantle.updatesAccount) {
        if (family4_datagen::account::layout::kObjectSize > rawStorage.size()) {
            clear_after(scratch, reservation);
            return report_failure("dismantle_account_storage");
        }
        const auto accountBytes = rawStorage.first(family4_datagen::account::layout::kObjectSize);
        if (!family4_datagen::account::encode(account, accountBytes)) {
            clear_after(scratch, reservation);
            return report_failure("dismantle_account_encode");
        }

        auto& accountObject =
            *reinterpret_cast<family4_datagen::account::layout::Object*>(accountBytes.data());
        if (accountObject.profileInventoryChanges.writeSlot != 0
            || accountObject.profileInventoryChanges.nextSequence != 0
            || !std::all_of(accountObject.profileInventoryChanges.records.cbegin(),
                            accountObject.profileInventoryChanges.records.cend(),
                            kChangeRecordIsZero)
            || mutation.rewardCount == 0
            || mutation.rewardCount > accountObject.profileInventoryChanges.records.size()) {
            clear_after(scratch, reservation);
            return report_failure("dismantle_account_change_state");
        }

        // Kind 1 is the ordinary acquisition path, and clear policy bits leave it enabled. The
        // native observer skips a record with any other pair.
        constexpr std::uint8_t kRewardChangeKind = 1;
        constexpr std::uint16_t kRewardChangeFlags = 0;
        for (std::size_t rewardIndex = 0; rewardIndex < mutation.rewardCount; ++rewardIndex) {
            const state::DismantleReward& reward = mutation.rewards[rewardIndex];
            std::size_t matchedRows = 0;
            for (const auto& row : accountObject.profileItems) {
                if (row.mutationSerial != reward.mutationSerial) {
                    continue;
                }
                if (row.quantity != reward.afterQuantity) {
                    clear_after(scratch, reservation);
                    return report_failure("dismantle_reward_quantity");
                }
                ++matchedRows;
            }
            if (matchedRows != 1) {
                clear_after(scratch, reservation);
                return report_failure("dismantle_reward_row");
            }
            auto& change = accountObject.profileInventoryChanges.records[rewardIndex];
            change.sequence = static_cast<std::uint16_t>(rewardIndex);
            change.mutationSerial = reward.mutationSerial;
            change.kind = kRewardChangeKind;
            change.flags = kRewardChangeFlags;
        }
        accountObject.profileInventoryChanges.writeSlot =
            static_cast<std::uint16_t>(mutation.rewardCount);
        accountObject.profileInventoryChanges.nextSequence =
            static_cast<std::uint16_t>(mutation.rewardCount);

        if (!append_object(scratch,
                           accountBytes,
                           dismantle.accountDefinitionId,
                           dismantle.accountSoid,
                           staged.objects[2],
                           compressedExtent)) {
            clear_after(scratch, reservation);
            return report_failure("dismantle_account_object");
        }
        staged.rawClearSize =
            (std::max)(staged.rawClearSize,
                       reservation.rawWriteOffset + family4_datagen::account::layout::kObjectSize);
        objectCount = 3;
    }
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kAccountFamilyType,
        dismantle.after.family4RootSoid,
        dismantle.after.family4Version,
        0,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return report_failure("dismantle_commit");
    }

    return true;
}
} // namespace sunrise::server::bap::encrypted::push::snapshot
