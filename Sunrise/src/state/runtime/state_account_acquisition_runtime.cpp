#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../build_data/runtime.h"
#include "../progression/season_pass_reward_catalog.h"
#include "../progression/seasonal_experience.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"

namespace sunrise::state {

using namespace runtime::detail;
namespace authored_inventory = account::inventory;
namespace item_details = build_data::items::details;
namespace inventory_buckets = build_data::inventory::buckets;
namespace family4_loadout = middleware::datagen::family4::loadout;

namespace {

[[nodiscard]] bool materialize_record_reward(const AccountState& current,
                                             const PendingRecordRewardGrant& mutation,
                                             AccountState& after) noexcept;

struct GrantSource {
    std::uint32_t materialRequirementSetHash{};
    std::uint16_t collectibleIndex{};
    std::uint8_t materialRequirementCount{};
    bool direct{};
};

[[nodiscard]] std::size_t selected_character_index(const AccountState& account) noexcept {
    const std::size_t count = (std::min)(account.characterCount, account.characters.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (account.characters[index].selected) {
            return index;
        }
    }
    return account.characters.size();
}

/** Stages the common selected-character insertion path. */
[[nodiscard]] bool finalize_item_acquisition(const AccountState& account,
                                             const AccountState& chargedAccount,
                                             std::uint32_t definitionHash,
                                             bool profileChanged,
                                             const GrantSource& source,
                                             PendingItemAcquisition& mutation) noexcept {
    const std::size_t characterIndex = selected_character_index(account);
    if (characterIndex >= account.characterCount) {
        return false;
    }

    const CharacterState& before = account.characters[characterIndex];
    if (before.inventory.count >= before.inventory.values.size()
        || before.nextInventorySerial
               >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
        return false;
    }

    std::uint64_t instanceSoid = 0;
    if (!next_item_instance_soid(account, instanceSoid)) {
        return false;
    }

    CharacterState after = before;
    const std::size_t inventoryIndex = after.inventory.count;
    authored_inventory::Item acquired{};
    acquired.instanceSoid = instanceSoid;
    acquired.definitionHash = definitionHash;
    acquired.level = acquisition_level(before);
    acquired.quantity = 1;
    acquired.mutationSerial = static_cast<std::int32_t>(after.nextInventorySerial++);
    acquired.sockets.policy = authored_inventory::SocketPolicy::nativeDefaults;
    after.inventory.values[inventoryIndex] = acquired;
    ++after.inventory.count;

    AccountState candidate = chargedAccount;
    candidate.characters[characterIndex] = after;
    family4_loadout::ResolvedLoadout resolved{};
    std::uint16_t inventoryRow = 0;
    std::uint8_t equipmentSlot = 0;
    if (!account::valid(candidate) || identity_uses_soid(candidate, instanceSoid)
        || !family4_loadout::resolve(candidate, characterIndex, resolved)
        || !find_unequipped_row(resolved, instanceSoid, inventoryRow, equipmentSlot)) {
        return false;
    }

    mutation.beforeCharacter = before;
    mutation.afterCharacter = after;
    mutation.beforeProfileItems = account.profileItems;
    mutation.afterProfileItems = chargedAccount.profileItems;
    mutation.accountSoid = account.primarySoid;
    mutation.characterSoid = before.soid;
    mutation.acquiredInstanceSoid = instanceSoid;
    mutation.acquiredDefinitionHash = definitionHash;
    mutation.materialRequirementSetHash = source.materialRequirementSetHash;
    mutation.characterIndex = characterIndex;
    mutation.expectedInventoryCount = before.inventory.count;
    mutation.expectedProfileItemCount = account.profileItemCount;
    mutation.afterProfileItemCount = chargedAccount.profileItemCount;
    mutation.inventoryIndex = inventoryIndex;
    mutation.collectibleIndex = source.collectibleIndex;
    mutation.inventoryRow = inventoryRow;
    mutation.equipmentSlot = equipmentSlot;
    mutation.materialRequirementCount = source.materialRequirementCount;
    mutation.profileChanged = profileChanged;
    mutation.directGrant = source.direct;
    mutation.prepared = true;
    return true;
}

} // namespace

/** Prepares one native-row-checked selected-character inventory insertion. */
bool prepare_item_acquisition(std::uint16_t collectibleIndex,
                              std::uint32_t definitionHash,
                              PendingItemAcquisition& mutation) noexcept {
    mutation = {};
    const AccountState account = account_snapshot();
    build_data::collectibles::Definition collectible{};
    build_data::items::Definition grantedDefinition{};
    // A vendor purchase names an item, not a collectible, so the collectible steps are skipped
    // rather than faked. The item is still validated, just by its own hash.
    const bool hasCollectible = collectibleIndex != build_data::collectibles::kNoCollectibleIndex;
    if (definitionHash == authored_inventory::kNoDefinitionHash || !account::valid(account)
        || !valid_profile_inventory(account)) {
        return false;
    }
    if (hasCollectible) {
        if (!build_data::find_collectible_definition(collectibleIndex, collectible)
            || collectible.itemDefinitionIndex
                   == build_data::collectibles::kUnavailableItemDefinitionIndex
            || !build_data::find_item_definition_index(collectible.itemDefinitionIndex,
                                                       grantedDefinition)
            || grantedDefinition.definitionHash != definitionHash) {
            return false;
        }
    } else if (!build_data::find_item_definition_hash(definitionHash, grantedDefinition)
               || grantedDefinition.definitionHash != definitionHash) {
        return false;
    }

    AccountState chargedAccount = account;
    bool profileChanged = false;
    // Nothing is charged without a collectible: the cost lives on the collectible's material
    // requirements, and a sale row's own cost fields are still role-open.
    if (hasCollectible
        && !apply_collection_materials(account, collectible, chargedAccount, profileChanged)) {
        return false;
    }

    return finalize_item_acquisition(
        account,
        chargedAccount,
        definitionHash,
        profileChanged,
        {.materialRequirementSetHash = collectible.materialRequirementSetHash,
         .collectibleIndex = collectibleIndex,
         .materialRequirementCount = collectible.materialRequirementCount},
        mutation);
}

/** Prepares one direct selected-character inventory grant, with no Collections row or charge. */
bool prepare_item_acquisition_for_item(std::uint16_t itemDefinitionIndex,
                                       PendingItemAcquisition& mutation) noexcept {
    mutation = {};
    const AccountState account = account_snapshot();
    build_data::items::Definition grantedDefinition{};
    if (!account::valid(account) || !valid_profile_inventory(account)
        || !build_data::find_item_definition_index(itemDefinitionIndex, grantedDefinition)
        || grantedDefinition.definitionHash == authored_inventory::kNoDefinitionHash) {
        return false;
    }

    return finalize_item_acquisition(
        account, account, grantedDefinition.definitionHash, false, {.direct = true}, mutation);
}

/** Prepares a fixed Season wrapper expansion without exposing a partial package. */
bool prepare_direct_item_bundle(std::uint32_t sourceDefinitionHash,
                                std::span<const std::uint16_t> itemDefinitionIndices,
                                PendingDirectItemBundle& mutation) noexcept {
    mutation = {};
    const auto* package =
        progression::season_pass::find_premium_class_package(sourceDefinitionHash);
    if (package == nullptr || itemDefinitionIndices.size() != package->items.size()) {
        return false;
    }

    std::array<std::uint32_t, progression::season_pass::kPremiumPackageItemCount> hashes{};
    for (std::size_t index = 0; index < itemDefinitionIndices.size(); ++index) {
        const std::uint16_t definitionIndex = itemDefinitionIndices[index];
        build_data::items::Definition definition{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (!build_data::find_item_definition_index(definitionIndex, definition)
            || definition.definitionHash != package->items[index]
            || !build_data::find_configured_item_detail(definitionIndex, detail)
            || detail.definitionIndex != definition.definitionIndex
            || detail.definitionHash != definition.definitionHash
            || detail.bucketId != definition.bucketId
            || detail.instancedDefinitionState != item_details::InstancedDefinitionState::instanced
            || !detail.equipmentSlot.has_value()
            || !build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)
            || bucket.arraySelector != inventory_buckets::ArraySelector::character) {
            return false;
        }
        hashes[index] = definition.definitionHash;
    }

    const AccountState account = account_snapshot();
    const std::size_t characterIndex = selected_character_index(account);
    if (!account::valid(account) || characterIndex >= account.characterCount) {
        return false;
    }
    const CharacterState& before = account.characters[characterIndex];
    if (before.nextInventorySerial
            > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
        || itemDefinitionIndices.size() > before.inventory.values.size() - before.inventory.count
        || itemDefinitionIndices.size()
               > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
                     - before.nextInventorySerial) {
        return false;
    }

    std::uint64_t firstSoid = 0;
    if (!next_item_instance_soid(account, firstSoid)
        || itemDefinitionIndices.size() - 1U
               > (std::numeric_limits<std::uint64_t>::max)() - firstSoid) {
        return false;
    }

    CharacterState after = before;
    const std::int32_t level = acquisition_level(before);
    for (std::size_t index = 0; index < itemDefinitionIndices.size(); ++index) {
        authored_inventory::Item granted{};
        granted.instanceSoid = firstSoid + index;
        granted.definitionHash = hashes[index];
        granted.level = level;
        granted.quantity = 1;
        granted.mutationSerial = static_cast<std::int32_t>(after.nextInventorySerial++);
        after.inventory.values[after.inventory.count++] = granted;
    }

    AccountState candidate = account;
    candidate.characters[characterIndex] = after;
    family4_loadout::ResolvedLoadout resolved{};
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, characterIndex, resolved)) {
        return false;
    }
    for (std::size_t index = 0; index < itemDefinitionIndices.size(); ++index) {
        std::uint16_t row = 0;
        std::uint8_t slot = 0;
        if (!find_unequipped_row(resolved, firstSoid + index, row, slot)) {
            return false;
        }
    }

    mutation.beforeCharacter = before;
    mutation.afterCharacter = after;
    mutation.accountSoid = account.primarySoid;
    mutation.characterSoid = before.soid;
    mutation.firstInstanceSoid = firstSoid;
    mutation.sourceDefinitionHash = sourceDefinitionHash;
    mutation.characterIndex = characterIndex;
    mutation.expectedInventoryCount = before.inventory.count;
    mutation.itemCount = itemDefinitionIndices.size();
    mutation.prepared = true;
    return true;
}

bool reserve_selected_character_inventory_serial(std::int32_t& mutationSerial) noexcept {
    mutationSerial = 0;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState& account = runtime::storage::g_state.account;
    const std::size_t characterIndex = selected_character_index(account);
    const bool ready =
        account::valid(account) && characterIndex < account.characterCount
        && account.characters[characterIndex].nextInventorySerial
               < static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
    if (ready) {
        mutationSerial =
            static_cast<std::int32_t>(account.characters[characterIndex].nextInventorySerial++);
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ready;
}

namespace {

[[nodiscard]] bool valid_item_acquisition_source(const PendingItemAcquisition& mutation) noexcept {
    if (!mutation.prepared || mutation.characterSoid == 0 || mutation.acquiredInstanceSoid == 0
        || mutation.accountSoid == 0
        || mutation.acquiredDefinitionHash == authored_inventory::kNoDefinitionHash
        || mutation.characterIndex >= kCharacterCapacity
        || mutation.expectedInventoryCount >= authored_inventory::kCharacterItemCapacity
        || mutation.inventoryIndex != mutation.expectedInventoryCount
        || mutation.afterCharacter.inventory.count != mutation.expectedInventoryCount + 1U
        || mutation.inventoryIndex >= mutation.afterCharacter.inventory.count
        || mutation.afterCharacter.inventory.values[mutation.inventoryIndex].instanceSoid
               != mutation.acquiredInstanceSoid
        || mutation.afterCharacter.inventory.values[mutation.inventoryIndex].definitionHash
               != mutation.acquiredDefinitionHash
        || mutation.expectedProfileItemCount > authored_inventory::kProfileItemCapacity
        || mutation.afterProfileItemCount > authored_inventory::kProfileItemCapacity) {
        return false;
    }
    if (mutation.directGrant) {
        build_data::items::Definition definition{};
        return mutation.collectibleIndex == 0 && mutation.materialRequirementSetHash == 0
               && mutation.materialRequirementCount == 0
               && same_profile_views(mutation.beforeProfileItems,
                                     mutation.expectedProfileItemCount,
                                     mutation.afterProfileItems,
                                     mutation.afterProfileItemCount)
               && build_data::find_item_definition_hash(mutation.acquiredDefinitionHash,
                                                        definition);
    }

    if (mutation.collectibleIndex == build_data::collectibles::kNoCollectibleIndex) {
        // The guard is that prepare and commit agree. Without a collectible they agree on there
        // being none, which means both cost fields must still be clear.
        build_data::items::Definition definition{};
        return mutation.materialRequirementSetHash == 0 && mutation.materialRequirementCount == 0
               && build_data::find_item_definition_hash(mutation.acquiredDefinitionHash,
                                                        definition);
    }

    build_data::collectibles::Definition collectible{};
    build_data::items::Definition definition{};
    return build_data::find_collectible_definition(mutation.collectibleIndex, collectible)
           && collectible.itemDefinitionIndex
                  != build_data::collectibles::kUnavailableItemDefinitionIndex
           && collectible.materialRequirementSetHash == mutation.materialRequirementSetHash
           && collectible.materialRequirementCount == mutation.materialRequirementCount
           && build_data::find_item_definition_index(collectible.itemDefinitionIndex, definition)
           && definition.definitionHash == mutation.acquiredDefinitionHash;
}

/** Applies one validated insertion over an exact current account without taking State locks. */
[[nodiscard]] bool materialize_item_acquisition(const AccountState& current,
                                                const PendingItemAcquisition& mutation,
                                                AccountState& after) noexcept {
    std::uint64_t nextSoid = 0;
    if (!valid_item_acquisition_source(mutation)
        || mutation.characterIndex >= current.characterCount
        || current.primarySoid != mutation.accountSoid
        || !same_character(current.characters[mutation.characterIndex], mutation.beforeCharacter)
        || !same_profile_inventory(
            current, mutation.beforeProfileItems, mutation.expectedProfileItemCount)
        || !next_item_instance_soid(current, nextSoid)
        || nextSoid != mutation.acquiredInstanceSoid) {
        return false;
    }

    after = current;
    after.profileItems = mutation.afterProfileItems;
    after.profileItemCount = mutation.afterProfileItemCount;
    after.characters[mutation.characterIndex] = mutation.afterCharacter;
    family4_loadout::ResolvedLoadout resolved{};
    std::uint16_t row = 0;
    std::uint8_t slot = 0;
    return account::valid(after) && valid_profile_inventory(after)
           && family4_loadout::resolve(after, mutation.characterIndex, resolved)
           && find_unequipped_row(resolved, mutation.acquiredInstanceSoid, row, slot)
           && row == mutation.inventoryRow && slot == mutation.equipmentSlot;
}

/** Rebuilds one package from installed policy and rejects any altered after-image. */
[[nodiscard]] bool materialize_direct_item_bundle(const AccountState& current,
                                                  const PendingDirectItemBundle& mutation,
                                                  AccountState& after) noexcept {
    const auto* package =
        progression::season_pass::find_premium_class_package(mutation.sourceDefinitionHash);
    std::uint64_t firstSoid = 0;
    if (!mutation.prepared || package == nullptr || mutation.itemCount != package->items.size()
        || mutation.accountSoid == 0 || mutation.characterSoid == 0
        || mutation.firstInstanceSoid == 0 || mutation.characterIndex >= current.characterCount
        || mutation.expectedInventoryCount >= authored_inventory::kCharacterItemCapacity
        || current.primarySoid != mutation.accountSoid
        || !same_character(current.characters[mutation.characterIndex], mutation.beforeCharacter)
        || !current.characters[mutation.characterIndex].selected
        || current.characters[mutation.characterIndex].soid != mutation.characterSoid
        || mutation.beforeCharacter.inventory.count != mutation.expectedInventoryCount
        || mutation.itemCount
               > mutation.beforeCharacter.inventory.values.size() - mutation.expectedInventoryCount
        || mutation.beforeCharacter.nextInventorySerial
               > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
        || mutation.itemCount > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
                                    - mutation.beforeCharacter.nextInventorySerial
        || !next_item_instance_soid(current, firstSoid) || firstSoid != mutation.firstInstanceSoid
        || mutation.itemCount - 1U > (std::numeric_limits<std::uint64_t>::max)() - firstSoid) {
        return false;
    }

    CharacterState canonical = mutation.beforeCharacter;
    const std::int32_t level = acquisition_level(canonical);
    for (std::size_t index = 0; index < mutation.itemCount; ++index) {
        build_data::items::Definition definition{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (!build_data::find_item_definition_hash(package->items[index], definition)
            || !build_data::find_configured_item_detail(definition.definitionIndex, detail)
            || detail.definitionHash != definition.definitionHash
            || detail.definitionIndex != definition.definitionIndex
            || detail.bucketId != definition.bucketId
            || detail.instancedDefinitionState != item_details::InstancedDefinitionState::instanced
            || !detail.equipmentSlot.has_value()
            || !build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)
            || bucket.arraySelector != inventory_buckets::ArraySelector::character) {
            return false;
        }
        authored_inventory::Item granted{};
        granted.instanceSoid = firstSoid + index;
        granted.definitionHash = package->items[index];
        granted.level = level;
        granted.quantity = 1;
        granted.mutationSerial = static_cast<std::int32_t>(canonical.nextInventorySerial++);
        canonical.inventory.values[canonical.inventory.count++] = granted;
    }
    if (!same_character(canonical, mutation.afterCharacter)) {
        return false;
    }

    after = current;
    after.characters[mutation.characterIndex] = canonical;
    family4_loadout::ResolvedLoadout resolved{};
    if (!account::valid(after)
        || !family4_loadout::resolve(after, mutation.characterIndex, resolved)) {
        return false;
    }
    for (std::size_t index = 0; index < mutation.itemCount; ++index) {
        std::uint16_t row = 0;
        std::uint8_t slot = 0;
        if (!find_unequipped_row(resolved, firstSoid + index, row, slot)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool reward_matches(const progression::season_pass::Reward& reward,
                                  const PendingSeasonPassReward& mutation) noexcept {
    if (!mutation.prepared || mutation.sourceDefinitionHash != reward.itemHash) {
        return false;
    }
    if (const auto* item = std::get_if<PendingItemAcquisition>(&mutation.grant)) {
        if (reward.quantity != 1) {
            return false;
        }
        if (reward.itemHash != progression::season_pass::kLegendaryEngramHash
            && reward.itemHash != progression::season_pass::kExoticEngramHash) {
            return item->acquiredDefinitionHash == reward.itemHash;
        }
        return progression::season_pass::contains_engram_reward(
            reward.itemHash,
            item->acquiredDefinitionHash,
            static_cast<std::uint8_t>(item->afterCharacter.characterClass));
    }
    if (const auto* profile = std::get_if<PendingProfileItemAcquisition>(&mutation.grant)) {
        return profile->acquiredDefinitionHash == reward.itemHash
               && profile->acquiredQuantity - profile->previousQuantity == reward.quantity;
    }
    if (const auto* bundle = std::get_if<PendingDirectItemBundle>(&mutation.grant)) {
        return reward.quantity == 1 && bundle->sourceDefinitionHash == reward.itemHash
               && progression::season_pass::find_premium_class_package(reward.itemHash) != nullptr;
    }
    const auto* resources = std::get_if<PendingRecordRewardGrant>(&mutation.grant);
    if (resources == nullptr || reward.quantity != 1
        || reward.itemHash != progression::season_pass::kDestinationResourceBundleHash
        || resources->rewardCount != progression::season_pass::kDestinationResourceHashes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < resources->rewardCount; ++index) {
        if (resources->rewards[index].definitionHash
                != progression::season_pass::kDestinationResourceHashes[index]
            || resources->rewards[index].quantity
                   != progression::season_pass::kDestinationResourceQuantity) {
            return false;
        }
    }
    return true;
}

void restore_reward_grant(AccountState& account, const PendingItemAcquisition& mutation) noexcept {
    account.profileItems = mutation.beforeProfileItems;
    account.profileItemCount = mutation.expectedProfileItemCount;
    account.characters[mutation.characterIndex] = mutation.beforeCharacter;
}

void restore_reward_grant(AccountState& account,
                          const PendingProfileItemAcquisition& mutation) noexcept {
    account.profileItems = mutation.beforeItems;
    account.profileItemCount = mutation.expectedItemCount;
}

void restore_reward_grant(AccountState& account, const PendingDirectItemBundle& mutation) noexcept {
    account.characters[mutation.characterIndex] = mutation.beforeCharacter;
}

void restore_reward_grant(AccountState& account,
                          const PendingRecordRewardGrant& mutation) noexcept {
    account.characters[mutation.characterIndex] = mutation.beforeCharacter;
    account.profileItems = mutation.beforeProfileItems;
    account.profileItemCount = mutation.beforeProfileItemCount;
}

} // namespace

/** Produces the full account after-image while a prepared character pull remains current. */
bool preview_item_acquisition(const PendingItemAcquisition& mutation,
                              AccountState& after) noexcept {
    after = {};
    return materialize_item_acquisition(account_snapshot(), mutation, after);
}

/** Produces the full account after-image while a prepared package remains current. */
bool preview_direct_item_bundle(const PendingDirectItemBundle& mutation,
                                AccountState& after) noexcept {
    after = {};
    return materialize_direct_item_bundle(account_snapshot(), mutation, after);
}

/** Commits one prepared insertion only while its prepare-time loadout remains current. */
bool commit_item_acquisition(PendingItemAcquisition& mutation) noexcept {
    const PendingItemAcquisition& prepared = mutation;
    const PendingConsumption consume{mutation};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate{};
    const bool ready =
        materialize_item_acquisition(runtime::storage::g_state.account, prepared, candidate);
    if (ready) {
        runtime::storage::g_state.account = candidate;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ready;
}

/** Commits a reward and its claim together after every outbound byte has been staged. */
bool commit_season_pass_reward(PendingSeasonPassReward& mutation) noexcept {
    const PendingConsumption consume{mutation};
    const auto* reward = progression::season_pass::find(mutation.rewardIndex);
    if (reward == nullptr || !reward_matches(*reward, mutation)) {
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState after{};
    bool ready = false;
    if (const auto* item = std::get_if<PendingItemAcquisition>(&mutation.grant)) {
        ready = materialize_item_acquisition(runtime::storage::g_state.account, *item, after);
    } else if (const auto* profile = std::get_if<PendingProfileItemAcquisition>(&mutation.grant)) {
        ready = materialize_profile_acquisition(runtime::storage::g_state.account, *profile, after);
    } else if (const auto* bundle = std::get_if<PendingDirectItemBundle>(&mutation.grant)) {
        ready = materialize_direct_item_bundle(runtime::storage::g_state.account, *bundle, after);
    } else if (const auto* resources = std::get_if<PendingRecordRewardGrant>(&mutation.grant)) {
        ready = materialize_record_reward(runtime::storage::g_state.account, *resources, after);
    }
    if (ready) {
        runtime::storage::g_state.account = after;
        if (!progression::seasonal_experience::claim_reward(mutation.rewardIndex)) {
            AccountState& account = runtime::storage::g_state.account;
            std::visit(
                [&account](const auto& grant) noexcept { restore_reward_grant(account, grant); },
                mutation.grant);
            ready = false;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ready;
}

namespace {

[[nodiscard]] bool resolve_profile_item(std::uint16_t itemDefinitionIndex,
                                        build_data::items::Definition& item,
                                        item_details::Definition& detail) noexcept {
    inventory_buckets::Descriptor bucket{};
    return build_data::find_item_definition_index(itemDefinitionIndex, item)
           && item.definitionHash != authored_inventory::kNoDefinitionHash
           && build_data::find_configured_item_detail(itemDefinitionIndex, detail)
           && detail.definitionIndex == item.definitionIndex
           && detail.definitionHash == item.definitionHash && detail.bucketId == item.bucketId
           && detail.instancedDefinitionState == item_details::InstancedDefinitionState::stackable
           && detail.maxStackSize > 0
           && build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)
           && bucket.arraySelector == inventory_buckets::ArraySelector::profile;
}

/** Stages the common profile-stack insertion path. */
[[nodiscard]] bool
finalize_profile_item_acquisition(const AccountState& account,
                                  const AccountState& chargedAccount,
                                  std::uint32_t definitionHash,
                                  const item_details::Definition& detail,
                                  bool actionSource,
                                  std::int32_t quantity,
                                  const GrantSource& source,
                                  PendingProfileItemAcquisition& mutation) noexcept {
    if (quantity <= 0 || quantity > detail.maxStackSize) {
        return false;
    }
    std::size_t profileIndex = chargedAccount.profileItemCount;
    std::int32_t previousQuantity = 0;
    std::int32_t previousMutationSerial = 0;
    std::int32_t greatestMutationSerial = 0;
    bool appended = true;
    for (std::size_t index = 0; index < account.profileItemCount; ++index) {
        greatestMutationSerial =
            (std::max)(greatestMutationSerial, account.profileItems[index].mutationSerial);
    }
    for (std::size_t index = 0; index < chargedAccount.profileItemCount; ++index) {
        const authored_inventory::ProfileItem& existing = chargedAccount.profileItems[index];
        greatestMutationSerial = (std::max)(greatestMutationSerial, existing.mutationSerial);
        if (existing.definitionHash != definitionHash) {
            continue;
        }
        if (existing.quantity > detail.maxStackSize) {
            return false;
        }
        if (appended && existing.quantity <= detail.maxStackSize - quantity) {
            profileIndex = index;
            previousQuantity = existing.quantity;
            previousMutationSerial = existing.mutationSerial;
            appended = false;
        }
    }
    if (greatestMutationSerial == (std::numeric_limits<std::int32_t>::max)()
        || (appended && chargedAccount.profileItemCount >= chargedAccount.profileItems.size())
        || quantity > detail.maxStackSize - previousQuantity) {
        return false;
    }

    std::uint64_t acquiredInstanceSoid =
        appended ? 0 : chargedAccount.profileItems[profileIndex].instanceSoid;
    if (!appended && actionSource != (acquiredInstanceSoid != 0)) {
        return false;
    }
    if (appended && actionSource
        && !next_profile_item_instance_soid(chargedAccount, acquiredInstanceSoid)) {
        return false;
    }

    AccountState after = chargedAccount;
    const std::int32_t acquiredMutationSerial = greatestMutationSerial + 1;
    if (appended) {
        after.profileItems[profileIndex] = {
            acquiredInstanceSoid, definitionHash, quantity, acquiredMutationSerial};
        ++after.profileItemCount;
    } else {
        after.profileItems[profileIndex].quantity += quantity;
        after.profileItems[profileIndex].mutationSerial = acquiredMutationSerial;
    }
    const std::int32_t acquiredQuantity = after.profileItems[profileIndex].quantity;
    if (after.profileItems[profileIndex].instanceSoid != acquiredInstanceSoid
        || acquiredQuantity <= previousQuantity || acquiredQuantity > detail.maxStackSize
        || !account::valid(after) || !valid_profile_inventory(after)) {
        return false;
    }

    mutation.beforeItems = account.profileItems;
    mutation.afterItems = after.profileItems;
    mutation.accountSoid = account.primarySoid;
    mutation.acquiredInstanceSoid = acquiredInstanceSoid;
    mutation.acquiredDefinitionHash = definitionHash;
    mutation.materialRequirementSetHash = source.materialRequirementSetHash;
    mutation.expectedItemCount = account.profileItemCount;
    mutation.afterItemCount = after.profileItemCount;
    mutation.profileIndex = profileIndex;
    mutation.previousQuantity = previousQuantity;
    mutation.acquiredQuantity = acquiredQuantity;
    mutation.previousMutationSerial = previousMutationSerial;
    mutation.acquiredMutationSerial = acquiredMutationSerial;
    mutation.collectibleIndex = source.collectibleIndex;
    mutation.bucketId = detail.bucketId;
    mutation.materialRequirementCount = source.materialRequirementCount;
    mutation.actionSource = actionSource;
    mutation.appended = appended;
    mutation.directGrant = source.direct;
    mutation.prepared = true;
    if (valid_profile_mutation_shape(mutation)) {
        return true;
    }
    mutation = {};
    return false;
}

} // namespace

/** Prepares one checked profile-stack increment or append for a Collections pull. */
bool prepare_profile_item_acquisition(std::uint16_t collectibleIndex,
                                      std::uint32_t definitionHash,
                                      PendingProfileItemAcquisition& mutation) noexcept {
    mutation = {};
    const AccountState account = account_snapshot();
    build_data::collectibles::Definition collectible{};
    build_data::items::Definition item{};
    item_details::Definition detail{};
    if (definitionHash == authored_inventory::kNoDefinitionHash || !account::valid(account)
        || !valid_profile_inventory(account)
        || !build_data::find_item_definition_hash(definitionHash, item)
        // The item resolves first, because the collectible cross-check reads it. With no
        // collectible the item's own hash is the whole check.
        || (collectibleIndex != build_data::collectibles::kNoCollectibleIndex
            && (!build_data::find_collectible_definition(collectibleIndex, collectible)
                || collectible.itemDefinitionIndex
                       == build_data::collectibles::kUnavailableItemDefinitionIndex
                || item.definitionIndex != collectible.itemDefinitionIndex))
        || !resolve_profile_item(item.definitionIndex, item, detail)
        || item.definitionHash != definitionHash) {
        return false;
    }
    AccountState chargedAccount = account;
    bool materialsChanged = false;
    // Nothing is charged without a collectible: the cost lives on the collectible's material
    // requirements, and a sale row's own cost fields are still role-open.
    if (collectibleIndex != build_data::collectibles::kNoCollectibleIndex
        && !apply_collection_materials(account, collectible, chargedAccount, materialsChanged)) {
        return false;
    }
    (void)materialsChanged;
    const bool actionSource =
        build_data::is_profile_action_source(item.definitionIndex, item.bucketId);

    return finalize_profile_item_acquisition(
        account,
        chargedAccount,
        definitionHash,
        detail,
        actionSource,
        1,
        {.materialRequirementSetHash = collectible.materialRequirementSetHash,
         .collectibleIndex = collectibleIndex,
         .materialRequirementCount = collectible.materialRequirementCount},
        mutation);
}

/** Prepares one direct profile-stack grant, with no Collections row or material charge. */
bool prepare_profile_item_acquisition_for_item(std::uint16_t itemDefinitionIndex,
                                               std::int32_t quantity,
                                               PendingProfileItemAcquisition& mutation) noexcept {
    mutation = {};
    const AccountState account = account_snapshot();
    build_data::items::Definition item{};
    item_details::Definition detail{};
    if (quantity <= 0 || !account::valid(account) || !valid_profile_inventory(account)
        || !resolve_profile_item(itemDefinitionIndex, item, detail)) {
        return false;
    }
    const bool actionSource =
        build_data::is_profile_action_source(item.definitionIndex, item.bucketId);

    return finalize_profile_item_acquisition(account,
                                             account,
                                             item.definitionHash,
                                             detail,
                                             actionSource,
                                             quantity,
                                             {.direct = true},
                                             mutation);
}

/** Produces the exact account after-image while the captured profile view is still current. */
bool preview_profile_item_acquisition(const PendingProfileItemAcquisition& mutation,
                                      AccountState& after) noexcept {
    after = {};
    const AccountState current = account_snapshot();
    return materialize_profile_acquisition(current, mutation, after);
}

/** Commits one profile-stack after-image only while its exact prepare-time view remains current. */
bool commit_profile_item_acquisition(PendingProfileItemAcquisition& mutation) noexcept {
    const PendingProfileItemAcquisition& prepared = mutation;
    const PendingConsumption consume{mutation};
    if (!valid_profile_mutation_shape(prepared)) {
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate{};
    const bool ready =
        materialize_profile_acquisition(runtime::storage::g_state.account, prepared, candidate);
    if (ready) {
        runtime::storage::g_state.account = candidate;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ready;
}

namespace {

[[nodiscard]] bool materialize_record_reward(const AccountState& current,
                                             const PendingRecordRewardGrant& mutation,
                                             AccountState& after) noexcept {
    if (!mutation.prepared || mutation.rewardCount == 0
        || mutation.rewardCount > mutation.rewards.size() || mutation.accountSoid == 0
        || mutation.characterSoid == 0 || mutation.characterIndex >= current.characterCount
        || current.primarySoid != mutation.accountSoid
        || !same_character(current.characters[mutation.characterIndex], mutation.beforeCharacter)
        || !same_profile_inventory(
            current, mutation.beforeProfileItems, mutation.beforeProfileItemCount)
        || !mutation.beforeCharacter.selected
        || mutation.beforeCharacter.soid != mutation.characterSoid) {
        return false;
    }

    after = current;
    after.characters[mutation.characterIndex] = mutation.afterCharacter;
    after.profileItems = mutation.afterProfileItems;
    after.profileItemCount = mutation.afterProfileItemCount;
    family4_loadout::ResolvedLoadout loadout{};
    if (!account::valid(after) || !valid_profile_inventory(after)
        || !family4_loadout::resolve(after, mutation.characterIndex, loadout)) {
        return false;
    }

    for (std::size_t index = 0; index < mutation.rewardCount; ++index) {
        const PreparedRecordReward& reward = mutation.rewards[index];
        build_data::items::Definition item{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (reward.definitionHash == authored_inventory::kNoDefinitionHash || reward.quantity <= 0
            || reward.afterQuantity < reward.quantity || reward.mutationSerial < 0
            || !build_data::find_item_definition_hash(reward.definitionHash, item)
            || !build_data::find_configured_item_detail(item.definitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)) {
            return false;
        }
        if (reward.kind == RecordRewardKind::characterInstance) {
            if (reward.quantity != 1 || reward.afterQuantity != 1 || reward.instanceSoid == 0
                || reward.appendedProfileResident || !detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::instanced
                || bucket.arraySelector != inventory_buckets::ArraySelector::character
                || reward.stateIndex >= mutation.afterCharacter.inventory.count) {
                return false;
            }
            const auto& granted = mutation.afterCharacter.inventory.values[reward.stateIndex];
            std::uint16_t row = 0;
            std::uint8_t slot = 0;
            if (granted.instanceSoid != reward.instanceSoid
                || granted.definitionHash != reward.definitionHash || granted.quantity != 1
                || granted.mutationSerial != reward.mutationSerial
                || !find_unequipped_row(loadout, reward.instanceSoid, row, slot)
                || row != reward.inventoryRow) {
                return false;
            }
        } else if (reward.kind == RecordRewardKind::characterStack) {
            if (reward.instanceSoid != 0 || reward.appendedProfileResident
                || detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::stackable
                || bucket.arraySelector != inventory_buckets::ArraySelector::character
                || reward.afterQuantity > detail.maxStackSize
                || reward.stateIndex >= mutation.afterCharacter.stacks.count) {
                return false;
            }
            const auto& granted = mutation.afterCharacter.stacks.values[reward.stateIndex];
            if (granted.definitionHash != reward.definitionHash
                || granted.quantity != reward.afterQuantity
                || granted.mutationSerial != reward.mutationSerial) {
                return false;
            }
        } else {
            if (reward.kind != RecordRewardKind::profileStack
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::stackable
                || bucket.arraySelector != inventory_buckets::ArraySelector::profile
                || reward.afterQuantity > detail.maxStackSize
                || reward.stateIndex >= mutation.afterProfileItemCount) {
                return false;
            }
            const auto& granted = mutation.afterProfileItems[reward.stateIndex];
            const bool actionSource =
                build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
            if (granted.instanceSoid != reward.instanceSoid
                || granted.definitionHash != reward.definitionHash
                || granted.quantity != reward.afterQuantity
                || granted.mutationSerial != reward.mutationSerial
                || actionSource != (reward.instanceSoid != 0)
                || reward.appendedProfileResident
                       != (actionSource && reward.stateIndex >= mutation.beforeProfileItemCount)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

/** Prepares every reward over one cumulative account view. */
bool prepare_record_reward_grant(std::span<const DirectRecordReward> rewards,
                                 const record_claims::PendingClaim& claim,
                                 PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    if (rewards.empty() || rewards.size() > mutation.rewards.size()) {
        return false;
    }
    const AccountState account = account_snapshot();
    const std::size_t characterIndex = selected_character_index(account);
    if (!account::valid(account) || !valid_profile_inventory(account)
        || characterIndex >= account.characterCount) {
        return false;
    }

    AccountState working = account;
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        const DirectRecordReward& requested = rewards[index];
        build_data::items::Definition item{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (requested.quantity <= 0
            || !build_data::find_item_definition_index(requested.itemDefinitionIndex, item)
            || !build_data::find_configured_item_detail(requested.itemDefinitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)) {
            return false;
        }
        if (detail.instancedDefinitionState == item_details::InstancedDefinitionState::stackable) {
            for (std::size_t prior = 0; prior < index; ++prior) {
                if (mutation.rewards[prior].definitionHash == item.definitionHash) {
                    return false;
                }
            }
        }

        PreparedRecordReward prepared{};
        prepared.definitionHash = item.definitionHash;
        prepared.quantity = requested.quantity;
        if (bucket.arraySelector == inventory_buckets::ArraySelector::profile) {
            if (detail.instancedDefinitionState
                != item_details::InstancedDefinitionState::stackable) {
                return false;
            }
            PendingProfileItemAcquisition staged{};
            const bool actionSource =
                build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
            if (!finalize_profile_item_acquisition(working,
                                                   working,
                                                   item.definitionHash,
                                                   detail,
                                                   actionSource,
                                                   requested.quantity,
                                                   {.direct = true},
                                                   staged)) {
                return false;
            }
            working.profileItems = staged.afterItems;
            working.profileItemCount = staged.afterItemCount;
            prepared.instanceSoid = staged.acquiredInstanceSoid;
            prepared.stateIndex = staged.profileIndex;
            prepared.afterQuantity = staged.acquiredQuantity;
            prepared.mutationSerial = staged.acquiredMutationSerial;
            prepared.kind = RecordRewardKind::profileStack;
            prepared.appendedProfileResident = staged.appended && staged.actionSource;
        } else if (bucket.arraySelector == inventory_buckets::ArraySelector::character
                   && detail.instancedDefinitionState
                          == item_details::InstancedDefinitionState::instanced) {
            if (requested.quantity != 1 || !detail.equipmentSlot.has_value()) {
                return false;
            }
            PendingItemAcquisition staged{};
            if (!finalize_item_acquisition(
                    working, working, item.definitionHash, false, {.direct = true}, staged)) {
                return false;
            }
            working.characters[characterIndex] = staged.afterCharacter;
            prepared.instanceSoid = staged.acquiredInstanceSoid;
            prepared.stateIndex = staged.inventoryIndex;
            prepared.afterQuantity = 1;
            prepared.mutationSerial =
                staged.afterCharacter.inventory.values[staged.inventoryIndex].mutationSerial;
            prepared.inventoryRow = staged.inventoryRow;
            prepared.kind = RecordRewardKind::characterInstance;
        } else if (bucket.arraySelector == inventory_buckets::ArraySelector::character
                   && detail.instancedDefinitionState
                          == item_details::InstancedDefinitionState::stackable
                   && !detail.equipmentSlot.has_value()) {
            CharacterState& character = working.characters[characterIndex];
            if (requested.quantity > detail.maxStackSize
                || character.nextInventorySerial
                       >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
                return false;
            }
            std::size_t stackIndex = character.stacks.count;
            for (std::size_t candidate = 0; candidate < character.stacks.count; ++candidate) {
                if (character.stacks.values[candidate].definitionHash == item.definitionHash) {
                    stackIndex = candidate;
                    break;
                }
            }
            const bool appended = stackIndex == character.stacks.count;
            if ((appended && stackIndex >= character.stacks.values.size())
                || (!appended
                    && character.stacks.values[stackIndex].quantity
                           > detail.maxStackSize - requested.quantity)) {
                return false;
            }
            auto& stack = character.stacks.values[stackIndex];
            if (appended) {
                stack.definitionHash = item.definitionHash;
                ++character.stacks.count;
            }
            stack.quantity += requested.quantity;
            stack.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
            prepared.stateIndex = stackIndex;
            prepared.afterQuantity = stack.quantity;
            prepared.mutationSerial = stack.mutationSerial;
            prepared.kind = RecordRewardKind::characterStack;
        } else {
            return false;
        }
        mutation.rewards[index] = prepared;
    }

    family4_loadout::ResolvedLoadout loadout{};
    if (!account::valid(working) || !valid_profile_inventory(working)
        || !family4_loadout::resolve(working, characterIndex, loadout)) {
        return false;
    }
    mutation.beforeCharacter = account.characters[characterIndex];
    mutation.afterCharacter = working.characters[characterIndex];
    mutation.beforeProfileItems = account.profileItems;
    mutation.afterProfileItems = working.profileItems;
    mutation.claim = claim;
    mutation.accountSoid = account.primarySoid;
    mutation.characterSoid = account.characters[characterIndex].soid;
    mutation.characterIndex = characterIndex;
    mutation.beforeProfileItemCount = account.profileItemCount;
    mutation.afterProfileItemCount = working.profileItemCount;
    mutation.rewardCount = rewards.size();
    mutation.prepared = true;
    return true;
}

bool preview_record_reward_grant(const PendingRecordRewardGrant& mutation,
                                 AccountState& after) noexcept {
    after = {};
    return materialize_record_reward(account_snapshot(), mutation, after);
}

/** Commits the shared reward after-image and claim together. */
bool commit_record_reward(PendingRecordRewardGrant& mutation) noexcept {
    const PendingConsumption consume{mutation};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState after{};
    bool ready = materialize_record_reward(runtime::storage::g_state.account, mutation, after);
    if (ready) {
        runtime::storage::g_state.account = after;
        if (!record_claims::claim(mutation.claim.flagIndex, mutation.claim.scoreValue)) {
            AccountState& account = runtime::storage::g_state.account;
            account.characters[mutation.characterIndex] = mutation.beforeCharacter;
            account.profileItems = mutation.beforeProfileItems;
            account.profileItemCount = mutation.beforeProfileItemCount;
            ready = false;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ready;
}

namespace {

/**
 * Default plug hashes the wheel seeds when granting or repairing the collection item's sockets.
 * All four are universal Common emotes from Bright Engrams, with no class or race restriction:
 * "Yes" (3184938442), "Nope" (48790291), "Casual Sit" (383973261), "Cheer" (2834933816).
 * Lane order follows the client's own wheel layout, confirmed empirically in-game:
 * lane 0 = top, lane 1 = bottom, lane 2 = left, lane 3 = right.
 */
constexpr std::uint32_t kYesEmoteDefinitionHash = 3184938442U;
constexpr std::uint32_t kNopeEmoteDefinitionHash = 48790291U;
constexpr std::uint32_t kCasualSitEmoteDefinitionHash = 383973261U;
constexpr std::uint32_t kCheerEmoteDefinitionHash = 2834933816U;

constexpr std::array<std::uint32_t, authored_inventory::kEmoteCollectionSocketLaneCount>
    kEmoteCollectionDefaultPlugHashes{
        kCheerEmoteDefinitionHash,     // lane 0 -- top
        kCasualSitEmoteDefinitionHash, // lane 1 -- bottom
        kYesEmoteDefinitionHash,       // lane 2 -- left
        kNopeEmoteDefinitionHash,      // lane 3 -- right
    };

/**
 * Resolves and cross-checks the "Emotes" collection item's own configured content. The detail row
 * is only read to validate the definition, so it stays local rather than reaching the caller.
 * @param definition Receives the matching native item-definition row.
 * @return True only when both rows agree with each other, carry no native equipment slot (the one
 *         trait that singles this item out among every character-scoped item), and declare exactly
 *         the expected 4 ordinary socket lanes.
 */
[[nodiscard]] bool
resolve_emote_collection_definition(build_data::items::Definition& definition) noexcept {
    item_details::Definition detail{};
    return build_data::find_item_definition_hash(authored_inventory::kEmoteCollectionDefinitionHash,
                                                 definition)
           && definition.definitionHash == authored_inventory::kEmoteCollectionDefinitionHash
           && build_data::find_configured_item_detail(definition.definitionIndex, detail)
           && detail.definitionIndex == definition.definitionIndex
           && detail.definitionHash == authored_inventory::kEmoteCollectionDefinitionHash
           && detail.bucketId == definition.bucketId && !detail.equipmentSlot.has_value()
           && detail.ordinarySocketState == item_details::OrdinarySocketState::present
           && detail.ordinarySocketCount == authored_inventory::kEmoteCollectionSocketLaneCount;
}

/**
 * Checks that every one of the collection item's real plug pool candidates is actually installed
 * and allowed in its intended lane, so a granted item can never carry a plug the client rejects.
 */
[[nodiscard]] bool default_plugs_valid(std::uint16_t collectionDefinitionIndex) noexcept {
    for (std::size_t lane = 0; lane < kEmoteCollectionDefaultPlugHashes.size(); ++lane) {
        build_data::items::Definition plugDefinition{};
        if (!build_data::find_item_definition_hash(kEmoteCollectionDefaultPlugHashes[lane],
                                                   plugDefinition)
            || !build_data::is_socket_plug_allowed(collectionDefinitionIndex,
                                                   static_cast<std::uint8_t>(lane),
                                                   plugDefinition.definitionIndex)) {
            return false;
        }
    }
    return true;
}

/**
 * Checks an already-equipped collection item's own socket state, so a corrupted or stale set of
 * plugs is repaired instead of trusted just because the definition hash already matches.
 */
[[nodiscard]] bool socket_state_sound(const authored_inventory::Item& item,
                                      std::uint16_t collectionDefinitionIndex) noexcept {
    if (item.sockets.policy != authored_inventory::SocketPolicy::authored
        || item.sockets.plugCount != authored_inventory::kEmoteCollectionSocketLaneCount) {
        return false;
    }
    for (std::size_t lane = 0; lane < authored_inventory::kEmoteCollectionSocketLaneCount; ++lane) {
        const std::optional<std::uint32_t>& plugHash = item.sockets.plugs[lane];
        build_data::items::Definition plugDefinition{};
        if (!plugHash.has_value()
            || !build_data::find_item_definition_hash(*plugHash, plugDefinition)
            || !build_data::is_socket_plug_allowed(collectionDefinitionIndex,
                                                   static_cast<std::uint8_t>(lane),
                                                   plugDefinition.definitionIndex)) {
            return false;
        }
    }
    return true;
}

} // namespace

/**
 * Equips each character with the "Emotes" collection item in the real emote slot, in place of an
 * individual emote. Unlike every other character-scoped item, its real content carries no native
 * equipment-slot mapping at all, so the resolvers this depends on (loadout resolution, light,
 * appearance refresh) fall back to authored_inventory::kEmoteCollectionNativeEquipmentSlot for it
 * specifically, gated to its exact definition hash (state::account::inventory::
 * resolve_native_equipment_slot). Its 4 ordinary sockets carry no native default plug, so 4 hashes
 * from its real reusable plug pool seed a default wheel; the client's own generic socket-plug
 * request (opcode 1901) lets the player reassign them afterward, the same mechanism it already uses
 * for weapon mods and shaders.
 */
EmoteCollectionOutcome ensure_character_emote_collection() noexcept {
    constexpr std::size_t kEmoteCollectionSlot =
        static_cast<std::size_t>(authored_inventory::EquipmentSlot::emote);

    // The domains every check below reads have to be published first. Until they are, nothing can
    // be concluded about the installed content, so this is a retry rather than a verdict.
    if (!build_data::item_definitions_ready() || !build_data::configured_item_details_ready()
        || !build_data::socket_plug_rules_ready()) {
        return EmoteCollectionOutcome::notReady;
    }
    // With those published, an item that still does not resolve this way is a build that cannot
    // carry the wheel at all. Retrying that within this process would never change the answer.
    build_data::items::Definition collectionDefinition{};
    if (!resolve_emote_collection_definition(collectionDefinition)
        || !default_plugs_valid(collectionDefinition.definitionIndex)) {
        return EmoteCollectionOutcome::unsupported;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    AccountState candidate = runtime::storage::g_state.account;
    if (!account::valid(candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return EmoteCollectionOutcome::notReady;
    }
    bool changed = false;
    bool failed = false;
    for (std::size_t characterIndex = 0; characterIndex < candidate.characterCount && !failed;
         ++characterIndex) {
        CharacterState& character = candidate.characters[characterIndex];
        auto& collectionSlot = character.equipment.slots[kEmoteCollectionSlot];
        const bool present =
            collectionSlot.has_value()
            && collectionSlot->definitionHash == authored_inventory::kEmoteCollectionDefinitionHash;
        if (present && socket_state_sound(*collectionSlot, collectionDefinition.definitionIndex)) {
            continue;
        }
        if (character.nextInventorySerial
            >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
            failed = true;
            break;
        }
        // A repair owns only the definition, the sockets and the serial. Everything else the item
        // already carries, the accumulated item-state flags above all, belongs to the player and
        // survives. The account was checked whole on entry, so a present item's remaining scalars
        // are already known good and need no normalizing here.
        authored_inventory::Item granted = present ? *collectionSlot : authored_inventory::Item{};
        if (!present) {
            std::uint64_t instanceSoid = 0;
            if (!next_item_instance_soid(candidate, instanceSoid)) {
                failed = true;
                break;
            }
            granted.instanceSoid = instanceSoid;
            granted.level = 0;
            granted.quantity = 1;
        }
        granted.definitionHash = authored_inventory::kEmoteCollectionDefinitionHash;
        granted.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
        // Replaced whole rather than edited: the lanes past the used prefix have to be empty for
        // the socket block to validate, whatever the malformed copy left behind.
        granted.sockets = authored_inventory::Sockets{};
        granted.sockets.policy = authored_inventory::SocketPolicy::authored;
        granted.sockets.plugCount = kEmoteCollectionDefaultPlugHashes.size();
        for (std::size_t lane = 0; lane < kEmoteCollectionDefaultPlugHashes.size(); ++lane) {
            granted.sockets.plugs[lane] = kEmoteCollectionDefaultPlugHashes[lane];
        }
        collectionSlot = granted;
        changed = true;
    }
    if (failed) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return EmoteCollectionOutcome::failed;
    }
    if (!changed) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return EmoteCollectionOutcome::ready;
    }
    if (!account::valid(candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return EmoteCollectionOutcome::failed;
    }
    runtime::storage::g_state.account = candidate;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return EmoteCollectionOutcome::ready;
}

} // namespace sunrise::state
