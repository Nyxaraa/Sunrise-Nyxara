/** Family-4 reward updates: season pass packages and record rewards, one revision each. */

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family4/account/account_encoder.h"
#include "../../../../../middleware/datagen/family4/account/layout.h"
#include "../../../../../middleware/datagen/family4/character/character_encoder.h"
#include "../../../../../middleware/datagen/family4/character/layout.h"
#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../queuez/queuez_state_validation.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace sunrise::server::bap::encrypted::push::snapshot {

namespace family4_datagen = middleware::datagen::family4;

/** Builds one atomic Season package update with one acquisition record per granted item. */
bool prepare_season_pass_package(
    Scratch& scratch,
    const queuez::SessionState& before,
    const state::PendingDirectItemBundle& mutation,
    std::uint16_t rewardIndex,
    std::span<const queuez::AcquisitionPresentationRow> acquisitionPresentationRows,
    Prepared& prepared) noexcept {
    namespace character_layout = family4_datagen::character::layout;
    const std::size_t itemCount = mutation.itemCount;
    state::build_data::season_pass::Reward reward{};
    state::build_data::season_pass::Package package{};
    if (itemCount == 0 || itemCount > character_layout::kInventoryChangeRecordCapacity
        || !mutation.prepared || !state::build_data::find_season_pass_reward(rewardIndex, reward)
        || reward.quantity != 1 || reward.itemHash != mutation.sourceDefinitionHash
        || !state::build_data::find_season_pass_package(reward.itemHash, package)
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
        change.kind = kChangeKind;
        change.flags = kChangeFlags;
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
    if (!family4_datagen::account::encode(account, accountBytes)
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
    if (!family4_datagen::character::encode(
            character, selected.loadout, selected.lightEvaluation, characterBytes)) {
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
    if (!family4_datagen::account::encode(account, accountBytes)) {
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

} // namespace sunrise::server::bap::encrypted::push::snapshot
