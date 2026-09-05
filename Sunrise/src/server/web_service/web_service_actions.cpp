#include "web_service_actions.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "../../core/settings/rule_text.h"
#include "../../middleware/crypto/random_bytes.h"
#include "../../middleware/encoding/byte_order.h"
#include "../../middleware/web_service/messages/opcode1801.h"
#include "../../middleware/web_service/messages/opcode1820.h"
#include "../../middleware/web_service/messages/opcode1821.h"
#include "../../middleware/web_service/messages/opcode1901.h"
#include "../../middleware/web_service/messages/opcode2400.h"
#include "../../middleware/web_service/messages/opcode402.h"
#include "../../middleware/web_service/messages/opcode403.h"
#include "../../middleware/web_service/messages/opcode406.h"
#include "../../middleware/web_service/messages/opcode504.h"
#include "../../middleware/web_service/messages/opcode701/opcode701_codec.h"
#include "../../middleware/web_service/messages/opcode801.h"
#include "../../middleware/web_service/messages/opcode901/opcode901_codec.h"
#include "../../middleware/web_service/messages/opcode903.h"
#include "../../middleware/web_service/messages/opcode904/opcode904_codec.h"
#include "../../state/account/account_state.h"
#include "../../state/account/pursuit_hold.h"
#include "../../state/build_data/items/item_catalog.h"
#include "../../state/build_data/runtime.h"
#include "../../state/build_data/vendors/vendor_catalog.h"
#include "../../state/progression/season_pass_reward_catalog.h"
#include "../../state/progression/seasonal_experience.h"
#include "../../state/record_claims/record_claims.h"
#include "../../state/runtime/runtime.h"
#include "../../state/vendors/answered_interactions.h"

namespace sunrise::server::web_service {

namespace {

/** Socket kind the shader model occupies, which is the only kind a shader swap may target. */
constexpr std::uint8_t kEquippedShaderModelSocketKind = 0;
/** Index stored when no definition resolves. The catalog is u16-indexed, so this cannot be one. */
constexpr std::uint32_t kUnavailableDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();
/** Repeatable bounties a character may hold from one vendor at once, as retail allows. */
constexpr std::uint32_t kRepeatableHoldLimit = 5;
/** Authored repeatable pool ceiling. The largest set in the manifest is Eva's Dawning, at 22. */
constexpr std::size_t kRepeatablePoolCapacity = 64;
/** Stacks one exchange row may credit. Shader recycling pays two: Glimmer and Legendary Shards. */
constexpr std::size_t kExchangePayoutCapacity = 4;
// Every credited stack is announced to the account's change ring, so a rule that named more
// payouts than the mutation can announce would pay out silently. Raising one raises the other.
static_assert(kExchangePayoutCapacity <= state::kProfileStackChangeCapacity);

/**
 * Storage every rule reader in this file parses from.
 *
 * The three readers run one after another on the request thread, each reading its file and
 * finishing with it before the next starts, so they share one buffer rather than holding one
 * each. A reader must not keep a cursor into it across a call to another reader.
 */
std::array<char, core::rule_text::kRuleTextCapacity> g_ruleText{};

template <std::size_t Size>
void write_warning(const std::array<char, Size>& line, int count) noexcept {
    if (count <= 0) {
        return;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     {line.data(), (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
}

/** Prepares one rank-one class package without changing account State. */
[[nodiscard]] bool
prepare_premium_class_package(const state::progression::season_pass::PremiumClassPackage& package,
                              state::PendingDirectItemBundle& mutation) noexcept {
    std::array<std::uint16_t, state::progression::season_pass::kPremiumPackageItemCount>
        itemIndices{};
    for (std::size_t index = 0; index < package.items.size(); ++index) {
        state::build_data::items::Definition definition{};
        if (!state::build_data::find_item_definition_hash(package.items[index], definition)) {
            return false;
        }
        itemIndices[index] = definition.definitionIndex;
    }
    return state::prepare_direct_item_bundle(package.hash, itemIndices, mutation);
}

/** Expands the manifest-authored destination package into all nine material stacks. */
[[nodiscard]] bool
prepare_destination_resource_bundle(state::PendingSeasonPassReward& grant) noexcept {
    namespace pass = state::progression::season_pass;
    std::array<state::DirectRecordReward, pass::kDestinationResourceHashes.size()> rewards{};
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        state::build_data::items::Definition definition{};
        if (!state::build_data::find_item_definition_hash(pass::kDestinationResourceHashes[index],
                                                          definition)) {
            return false;
        }
        rewards[index] = {definition.definitionIndex, pass::kDestinationResourceQuantity};
    }
    auto& mutation = grant.grant.emplace<state::PendingRecordRewardGrant>();
    return state::prepare_record_reward_grant(rewards, {}, mutation);
}

/** Chooses one installed weapon or selected-class armour item from an auto-decrypting engram. */
template <std::size_t Size>
[[nodiscard]] std::span<const std::uint32_t>
class_armour_pool(state::CharacterClass characterClass,
                  const std::array<std::uint32_t, Size>& titan,
                  const std::array<std::uint32_t, Size>& hunter,
                  const std::array<std::uint32_t, Size>& warlock) noexcept {
    switch (characterClass) {
    case state::CharacterClass::hunter:
        return hunter;
    case state::CharacterClass::warlock:
        return warlock;
    case state::CharacterClass::titan:
    default:
        return titan;
    }
}

[[nodiscard]] bool choose_engram_reward(std::uint32_t engramHash,
                                        std::uint16_t& itemIndex) noexcept {
    namespace pass = state::progression::season_pass;
    std::span<const std::uint32_t> weapons;
    std::span<const std::uint32_t> armour;
    const state::AccountState account = state::account_snapshot();
    const state::CharacterState* character = nullptr;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            character = &account.characters[index];
            break;
        }
    }
    if (character == nullptr) {
        return false;
    }

    if (engramHash == pass::kLegendaryEngramHash) {
        weapons = pass::kLegendaryEngramWeapons;
        armour = class_armour_pool(character->characterClass,
                                   pass::kLegendaryTitanArmour,
                                   pass::kLegendaryHunterArmour,
                                   pass::kLegendaryWarlockArmour);
    } else if (engramHash == pass::kExoticEngramHash) {
        weapons = pass::kExoticEngramWeapons;
        armour = class_armour_pool(character->characterClass,
                                   pass::kExoticTitanArmour,
                                   pass::kExoticHunterArmour,
                                   pass::kExoticWarlockArmour);
    } else {
        return false;
    }

    std::array<std::byte, sizeof(std::uint32_t)> randomBytes{};
    if (!middleware::crypto::random::fill(randomBytes)) {
        return false;
    }
    const std::size_t count = weapons.size() + armour.size();
    const std::size_t first = middleware::encoding::read_u32_le(randomBytes) % count;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t selected = (first + offset) % count;
        const std::uint32_t hash = selected < weapons.size()
                                       ? weapons[selected]
                                       : armour[selected - weapons.size()];
        state::build_data::items::Definition definition{};
        if (state::build_data::find_item_definition_hash(hash, definition)) {
            itemIndex = definition.definitionIndex;
            return true;
        }
    }
    return false;
}

struct SeasonArmourRoll {
    std::uint16_t rewardIndex{};
    std::uint32_t itemHash{};
    std::array<std::uint32_t, 4> statPlugs{};
};

/** Exact rolls shown by the free, early premium, and high-stat pass tiles. */
constexpr std::array<SeasonArmourRoll, 36> kSeasonArmourRolls{{
    // Hunter
    {29, 3097544525U, {1232924472U, 3898228147U, 1177742666U, 1322519294U}},
    {106, 3097544525U, {2191854732U, 3198072315U, 1322519294U, 597281635U}},
    {139, 3097544525U, {3072193746U, 772656086U, 2692705068U, 1300719726U}},
    {3, 3750210364U, {1232924472U, 3898228147U, 1177742666U, 757147114U}},
    {78, 3750210364U, {2120002858U, 1526511067U, 1277668557U, 3014984195U}},
    {111, 3750210364U, {2600679334U, 3955940376U, 3026009912U, 115819390U}},
    {24, 2930001572U, {1232924472U, 2518640481U, 1177742666U, 4237052128U}},
    {97, 2930001572U, {2191854732U, 2440285137U, 1322519294U, 597281635U}},
    {134, 2930001572U, {4286256569U, 2148570570U, 594234536U, 2664898188U}},
    {10, 3136019014U, {2191854732U, 2236091344U, 1177742666U, 2596709069U}},
    {83, 3136019014U, {3407231789U, 1798836563U, 1322519294U, 3203727595U}},
    {120, 3136019014U, {3346214146U, 2399358832U, 594234536U, 2701881372U}},
    // Titan
    {30, 1214477175U, {1232924472U, 3898228147U, 1177742666U, 1322519294U}},
    {107, 1214477175U, {479726201U, 176934377U, 1322519294U, 597281635U}},
    {140, 1214477175U, {2868325782U, 2387165386U, 2692705068U, 1300719726U}},
    {4, 287888126U, {1232924472U, 3898228147U, 1177742666U, 757147114U}},
    {79, 287888126U, {2120002858U, 1369119565U, 1277668557U, 3014984195U}},
    {112, 287888126U, {2148570570U, 2148570570U, 3026009912U, 115819390U}},
    {25, 1585947570U, {1232924472U, 2518640481U, 1177742666U, 4237052128U}},
    {98, 1585947570U, {1232924472U, 1533918171U, 1322519294U, 597281635U}},
    {135, 1585947570U, {2868325782U, 1087628722U, 594234536U, 2664898188U}},
    {11, 1131831128U, {2191854732U, 2236091344U, 1177742666U, 2596709069U}},
    {84, 1131831128U, {1232924472U, 4002613733U, 1322519294U, 3203727595U}},
    {121, 1131831128U, {2387165386U, 2148570570U, 594234536U, 2701881372U}},
    // Warlock
    {31, 1173249516U, {1232924472U, 3898228147U, 1177742666U, 1322519294U}},
    {108, 1173249516U, {2191854732U, 2433764085U, 1322519294U, 597281635U}},
    {141, 1173249516U, {2868325782U, 3072193746U, 2692705068U, 1300719726U}},
    {5, 327547301U, {1232924472U, 3898228147U, 1177742666U, 757147114U}},
    {80, 327547301U, {1232924472U, 3554741641U, 1277668557U, 3014984195U}},
    {113, 327547301U, {4248662490U, 1281324436U, 3026009912U, 115819390U}},
    {26, 119457531U, {1232924472U, 2518640481U, 1177742666U, 4237052128U}},
    {99, 119457531U, {1232924472U, 1858911761U, 1322519294U, 597281635U}},
    {136, 119457531U, {3198618292U, 643846500U, 594234536U, 2664898188U}},
    {12, 674876967U, {2191854732U, 2236091344U, 1177742666U, 2596709069U}},
    {85, 674876967U, {2576224072U, 1534361879U, 1322519294U, 3203727595U}},
    {122, 674876967U, {643846500U, 4248662490U, 594234536U, 2701881372U}},
}};

[[nodiscard]] constexpr bool season_armour_rolls_valid() noexcept {
    for (const SeasonArmourRoll& roll : kSeasonArmourRolls) {
        const auto* reward = state::progression::season_pass::find(roll.rewardIndex);
        if (reward == nullptr || reward->itemHash != roll.itemHash) {
            return false;
        }
    }
    return true;
}

static_assert(season_armour_rolls_valid());

/** Replaces native stat plugs with the fixed roll represented by one known pass tile. */
[[nodiscard]] bool apply_season_armour_roll(std::uint16_t rewardIndex,
                                            state::PendingItemAcquisition& mutation) noexcept {
    const auto roll = std::find_if(kSeasonArmourRolls.begin(),
                                   kSeasonArmourRolls.end(),
                                   [rewardIndex](const SeasonArmourRoll& candidate) {
                                       return candidate.rewardIndex == rewardIndex;
                                   });
    if (roll == kSeasonArmourRolls.end()) {
        return true;
    }
    if (mutation.inventoryIndex >= mutation.afterCharacter.inventory.count) {
        return false;
    }
    auto& acquired = mutation.afterCharacter.inventory.values[mutation.inventoryIndex];
    state::build_data::items::Definition item{};
    state::build_data::items::details::Definition detail{};
    if (acquired.definitionHash != mutation.acquiredDefinitionHash
        || acquired.definitionHash != roll->itemHash
        || !state::build_data::find_item_definition_hash(acquired.definitionHash, item)
        || !state::build_data::find_configured_item_detail(item.definitionIndex, detail)
        || detail.definitionIndex != item.definitionIndex || detail.ordinarySocketCount <= 9U
        || detail.ordinarySocketCount > acquired.sockets.plugs.size()) {
        return false;
    }

    state::account::inventory::Sockets sockets{};
    sockets.policy = state::account::inventory::SocketPolicy::authored;
    sockets.plugCount = detail.ordinarySocketCount;
    for (std::size_t lane = 0; lane < sockets.plugCount; ++lane) {
        const std::uint16_t plugIndex = detail.initialPlugIndices[lane];
        if (plugIndex == state::build_data::items::details::kUnavailableItemIndex) {
            continue;
        }
        state::build_data::items::Definition plug{};
        if (!state::build_data::find_item_definition_index(plugIndex, plug)) {
            return false;
        }
        sockets.plugs[lane] = plug.definitionHash;
    }
    for (std::size_t index = 0; index < roll->statPlugs.size(); ++index) {
        sockets.plugs[6U + index] = roll->statPlugs[index];
    }
    acquired.sockets = sockets;
    return state::account::inventory::valid(acquired.sockets);
}

} // namespace

/** Decodes and prepares one sparse account-settings writeback without publishing State. */
state::SettingsUpdateDisposition mutate_settings(const middleware::web_service::Message& message,
                                                 Outcome& outcome) noexcept {
    namespace opcode701 = middleware::web_service::messages::opcode701;

    opcode701::Request request{};
    if (!opcode701::parse_request(message, request)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws701 stage=prepare result=rejected reason=parse");
        return state::SettingsUpdateDisposition::rejected;
    }

    state::PendingSettingsUpdate mutation{};
    const state::SettingsUpdateDisposition disposition =
        state::prepare_settings_update(request.settings, mutation);
    if (disposition == state::SettingsUpdateDisposition::preparedMutation) {
        if (emplace_mutation<state::PendingSettingsUpdate>(outcome, mutation) == nullptr) {
            return state::SettingsUpdateDisposition::rejected;
        }
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         "ev=ws701 stage=prepare result=ready");
        return disposition;
    }
    if (disposition == state::SettingsUpdateDisposition::acceptedNoChange) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         "ev=ws701 stage=prepare result=no_change");
        return disposition;
    }

    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     "ev=ws701 stage=prepare result=rejected reason=validation");
    return state::SettingsUpdateDisposition::rejected;
}

/**
 * Records the player's character pick, which arrives nowhere else.
 * A bad or unknown id leaves the selection alone. The reply is the status pair either way. The
 * Family-4 object move follows this call, and the family-zero pair after it.
 * @param message Parsed select-character request.
 * @param outcome Gets the picked key once the selection has moved in State.
 */
void select_character(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode504::Request picked;
    if (!middleware::web_service::messages::opcode504::parse_request(message, picked)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws504 stage=parse result=fail");
        return;
    }
    bool changed = false;
    if (!state::set_selected_character(picked.characterSoid, changed)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws504 stage=select result=unknown");
        return;
    }
    outcome.hasSelectedCharacter = true;
    outcome.selectedCharacterChanged = changed;
    outcome.selectedCharacterSoid = picked.characterSoid;
}

/** Reads the shared opcode-403/404 SOID descriptor through its codec. */
[[nodiscard]] bool parse_equipment_instance(const middleware::web_service::Message& message,
                                            std::uint64_t& instanceSoid) noexcept {
    middleware::web_service::messages::opcode403::Request request{};
    const bool parsed =
        middleware::web_service::messages::opcode403::parse_request(message, request);
    instanceSoid = request.instanceSoid;
    return parsed;
}

/** Prepares one opcode-403/404 equipment mutation without publishing State early. */
void mutate_equipment(const middleware::web_service::Message& message,
                      bool unequip,
                      Outcome& outcome) noexcept {
    std::uint64_t requestedInstanceSoid = 0;
    if (!parse_equipment_instance(message, requestedInstanceSoid)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=equipment stage=parse result=fail");
        return;
    }

    auto* mutation = emplace_mutation<state::PendingEquipmentSwap>(outcome);
    if (mutation == nullptr) {
        return;
    }
    const bool prepared = unequip
                              ? state::prepare_equipment_unequip(requestedInstanceSoid, *mutation)
                              : state::prepare_equipment_swap(requestedInstanceSoid, *mutation);
    if (!prepared) {
        clear_mutation(outcome);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=equipment stage=prepare result=fail");
        return;
    }
}

/** Parses and prepares one exact selected-character opcode-801 subclass node selection. */
void mutate_subclass_selection(const middleware::web_service::Message& message,
                               Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode801::Request request{};
    if (!middleware::web_service::messages::opcode801::parse_request(message, request)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws801 stage=parse result=fail");
        return;
    }

    auto* mutation = emplace_mutation<state::PendingSubclassSelection>(outcome);
    if (mutation == nullptr) {
        return;
    }
    if (!state::prepare_subclass_selection(
            request.subclassInstanceSoid, request.socketEntry, *mutation)) {
        clear_mutation(outcome);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws801 stage=prepare result=fail");
        return;
    }
}

/** Parses and prepares one exact selected-character opcode-903 socket selection. */
void mutate_socket_plug(const middleware::web_service::Message& message,
                        Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode903::Request request{};
    if (!middleware::web_service::messages::opcode903::parse_request(message, request)
        || !request.hasInstance || request.instanceSoid == 0 || request.hasTargetDefinition
        || !request.hasPlugDefinition
        || request.socketIndex >= state::account::inventory::kPlugCapacity) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws903 stage=parse result=fail");
        return;
    }

    auto* mutation = emplace_mutation<state::PendingSocketPlug>(outcome);
    if (mutation == nullptr) {
        return;
    }
    if (!state::prepare_socket_plug(request.instanceSoid,
                                    static_cast<std::uint8_t>(request.socketIndex),
                                    request.plugDefinitionIndex,
                                    *mutation)) {
        clear_mutation(outcome);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws903 stage=prepare result=fail");
        return;
    }
}

/** Parses and prepares one character-location opcode-1901 socket selection. */
void mutate_equipped_socket_plug(const middleware::web_service::Message& message,
                                 Outcome& outcome) noexcept {
    namespace opcode1901 = middleware::web_service::messages::opcode1901;
    opcode1901::Request request{};
    const bool parsed = opcode1901::parse_request(message, request);
    // One transaction can stage one socket mutation, so understood batch requests are refused.
    const opcode1901::Replacement& replacement = request.replacements.front();
    if (!parsed || request.replacementCount != 1
        || replacement.modelSocketKind != kEquippedShaderModelSocketKind
        || replacement.auxiliary != 0
        || replacement.socketIndex >= state::account::inventory::kPlugCapacity
        || request.instanceIdentityToken == 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws1901 stage=parse result=fail");
        return;
    }

    auto* mutation = emplace_mutation<state::PendingSocketPlug>(outcome);
    if (mutation == nullptr) {
        return;
    }
    if (!state::prepare_character_selector_socket_plug(
            request.instanceIdentityToken,
            static_cast<std::uint8_t>(replacement.socketIndex),
            replacement.plugDefinitionIndex,
            *mutation)) {
        clear_mutation(outcome);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=ws1901 stage=prepare result=fail");
        return;
    }
}

/** Parses and prepares one complete accumulated item-state value from opcode 406. */
void mutate_item_state(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode406::Request request{};
    if (!middleware::web_service::messages::opcode406::parse_request(message, request)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::warn, "ev=ws406 stage=parse result=fail");
        return;
    }

    auto* mutation = emplace_mutation<state::PendingItemState>(outcome);
    if (mutation == nullptr) {
        return;
    }
    if (!state::prepare_item_state(
            request.instanceSoid, request.definitionIndex, request.flags, *mutation)) {
        clear_mutation(outcome);
        return;
    }
}

/** Reports an opcode-402 validation failure. */
void report_item_dismantle(const middleware::web_service::Message& message,
                           std::string_view reason,
                           std::uint64_t instanceSoid,
                           std::uint32_t definitionIndex,
                           std::uint32_t definitionHash,
                           std::uint32_t quantity) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws402 stage=prepare result=fail reason=%.*s transaction=%u payload_bytes=%zu "
        "instance=0x%llX definition_index=%u definition_hash=0x%08X quantity=%u",
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        static_cast<unsigned long long>(instanceSoid),
        definitionIndex,
        definitionHash,
        quantity);
    write_warning(line, count);
}

/** Prepares the exact fixed-width opcode-402 Character-inventory removal request. */
void dismantle_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode402::Request request{};
    if (!middleware::web_service::messages::opcode402::parse_request(message, request)) {
        report_item_dismantle(
            message, "payload_bits", request.instanceSoid, request.definitionIndex, 0, 0);
        return;
    }
    const std::uint64_t instanceSoid = request.instanceSoid;
    const std::uint16_t definitionIndex = request.definitionIndex;
    // The codec owns the value; this alias keeps the dismantle checks below readable.
    constexpr std::uint32_t kSingleQuantity =
        middleware::web_service::messages::opcode402::kSingleQuantity;

    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(definitionIndex, definition)) {
        report_item_dismantle(
            message, "definition", instanceSoid, definitionIndex, 0, kSingleQuantity);
        return;
    }
    auto* mutation = emplace_mutation<state::PendingItemDismantle>(outcome);
    if (mutation == nullptr) {
        report_item_dismantle(message,
                              "storage",
                              instanceSoid,
                              definitionIndex,
                              definition.definitionHash,
                              kSingleQuantity);
        return;
    }
    if (!state::prepare_item_dismantle(instanceSoid, *mutation)) {
        clear_mutation(outcome);
        report_item_dismantle(message,
                              "state",
                              instanceSoid,
                              definitionIndex,
                              definition.definitionHash,
                              kSingleQuantity);
        return;
    }
    if (mutation->dismantledItem.definitionHash != definition.definitionHash
        || mutation->dismantledItem.quantity != static_cast<std::int32_t>(kSingleQuantity)) {
        clear_mutation(outcome);
        report_item_dismantle(message,
                              "identity",
                              instanceSoid,
                              definitionIndex,
                              definition.definitionHash,
                              kSingleQuantity);
        return;
    }
}

/** Reports an opcode-1801 claim failure. */
void report_record_claim(const middleware::web_service::Message& message,
                         std::string_view reason,
                         std::uint32_t recordIndex,
                         std::uint32_t completionFlagIndex,
                         std::uint32_t scoreValue) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws1801 stage=claim result=fail reason=%.*s transaction=%u payload_bytes=%zu "
        "record_index=%u completion_flag_index=%u score=%u total_score=%u claims=%zu",
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        recordIndex,
        completionFlagIndex,
        scoreValue,
        state::record_claims::total_score(),
        state::record_claims::count());
    write_warning(line, count);
}

/** Reports an opcode-1820 validation failure. */
void report_item_acquisition(const middleware::web_service::Message& message,
                             std::string_view reason,
                             std::uint32_t collectibleIndex,
                             std::uint32_t itemDefinitionIndex,
                             std::uint32_t definitionHash,
                             std::uint64_t instanceSoid) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ws1820 stage=prepare result=fail reason=%.*s transaction=%u payload_bytes=%zu "
        "collectible_index=%u item_definition_index=%u definition_hash=0x%08X instance=0x%llX",
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned>(message.transactionId),
        message.payload.size(),
        collectibleIndex,
        itemDefinitionIndex,
        definitionHash,
        static_cast<unsigned long long>(instanceSoid));
    write_warning(line, count);
}

/**
 * Writes one purchase line.
 *
 * The opcode is carried rather than hard-coded: 901 and 904 share this line, and a quest acquire
 * reporting itself as `ws901` sends anyone reading the log to the wrong decoder.
 *
 * @param opcode Request opcode the line belongs to, 901 or 904.
 * @param result `ok` or `fail`.
 * @param reason Step that decided it.
 * @param vendorIndex Vendor row the request named.
 * @param saleIndex Sale row the request named.
 * @param itemDefinitionIndex Item resolved, when the row resolved.
 */
void report_purchase(std::uint16_t opcode,
                     const char* result,
                     const char* reason,
                     std::int32_t vendorIndex,
                     std::int32_t saleIndex,
                     std::uint16_t itemDefinitionIndex) noexcept {
    core::log::writef(core::log::Channel::server,
                      std::strcmp(result, "ok") == 0 ? core::log::Level::info
                                                     : core::log::Level::warn,
                      "ev=ws%u stage=purchase result=%s reason=%s vendor=%d sale=%d item=%u",
                      static_cast<unsigned>(opcode),
                      result,
                      reason,
                      static_cast<int>(vendorIndex),
                      static_cast<int>(saleIndex),
                      static_cast<unsigned>(itemDefinitionIndex));
}

/**
 * Resolves the vendor a request names to its index row and held definition.
 *
 * Every vendor behaviour starts here, and five of them spelled it out by hand. A negative index is
 * the client's own absent marker and never a row.
 *
 * @param vendorIndex Vendor row the request named.
 * @param entry Receives the index row.
 * @param definition Receives the held definition.
 * @return True when the row exists and its definition is published.
 */
[[nodiscard]] bool find_vendor(std::int32_t vendorIndex,
                               state::build_data::vendors::IndexEntry& entry,
                               state::build_data::vendors::Definition& definition) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    entry = {};
    definition = {};
    return vendorIndex >= 0 && vendorIndex <= (std::numeric_limits<std::uint16_t>::max)()
           && vendor_domain::find_index(static_cast<std::uint16_t>(vendorIndex), entry)
           && vendor_domain::find(entry.definitionHash, definition);
}

/** What a substitution rule said about one sale row's item. */
enum class Substitution : std::uint8_t {
    /** No rule names this item; the row grants what it names. */
    none,
    /** A rule names it and its replacement resolved; the row grants the replacement. */
    replaced,
    /** A rule names it but its replacement is not in this build; the row must grant nothing. */
    broken,
};

/**
 * Answers what a placeholder sale row is really selling.
 *
 * Several rows name a DestinyItemType 20 Dummy - a UI placeholder for something the row does not
 * name, as Amanda Holliday's Legacy Content rows stand for a campaign's first quest step. Granting
 * the placeholder puts an item in the Quests tab the client will not draw, and the row never
 * settles. `vendor_item_substitute.txt` maps sold hash to granted hash, keyed by item so one rule
 * covers every seller. A rule whose replacement is absent from this build answers `broken` rather
 * than `none`: the rule proves the row's item is a placeholder, and granting it would be the exact
 * wrong grant this file exists to prevent.
 *
 * @param itemDefinitionIndex Item the row resolved to.
 * @param substituteIndex Receives what should be granted in its place.
 * @return What the rule file said about this item.
 */
[[nodiscard]] Substitution substitute_for_item(std::uint16_t itemDefinitionIndex,
                                               std::uint16_t& substituteIndex) noexcept {
    substituteIndex = kUnavailableDefinitionIndex;
    state::build_data::items::Definition sold{};
    if (!state::build_data::find_item_definition_index(itemDefinitionIndex, sold)) {
        return Substitution::none;
    }
    if (!core::path::read_artifact_text(L"vendor_item_substitute.txt", g_ruleText)) {
        return Substitution::none;
    }
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (rules.seek_field()) {
        const std::uint32_t soldHash = rules.read_hex();
        const std::uint32_t grantHash = rules.read_hex();
        if (soldHash != sold.definitionHash) {
            continue;
        }
        state::build_data::items::Definition replacement{};
        const bool resolved =
            state::build_data::find_item_definition_hash(grantHash, replacement);
        if (resolved) {
            substituteIndex = replacement.definitionIndex;
        }
        if (resolved) {
            core::log::writef(core::log::Channel::server,
                              core::log::Level::info,
                              "ev=vendor stage=substitute sold=0x%08X granted=0x%08X item=%u",
                              sold.definitionHash,
                              replacement.definitionHash,
                              static_cast<unsigned>(replacement.definitionIndex));
            return Substitution::replaced;
        }
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=vendor stage=substitute result=fail reason=missing sold=0x%08X "
                          "named=0x%08X",
                          sold.definitionHash,
                          grantHash);
        return Substitution::broken;
    }
    return Substitution::none;
}

/**
 * Rolls one random unheld repeatable bounty, for a row that offers "Additional Bounties".
 *
 * The row sells a Dummy placeholder; what it owes is a REPEATABLE bounty, a distinct kind a
 * character may hold five of. The pool is authored by hash in `vendor_bounty_roll.txt`, because a
 * repeatable is not a sale row - no vendor in the manifest lists one - so nothing on the vendor can
 * be discovered or picked from. Rules are keyed by vendor definition hash and trigger category,
 * since one vendor can own several such rows (Eva Levante has one per event), and lines sharing a
 * key accumulate. A hash this build does not carry is skipped, so a pool authored from a newer
 * manifest degrades to what exists rather than failing whole.
 *
 * @param vendorIndex Vendor the purchase names.
 * @param categoryIndex Category of the purchased row, from sale row +100.
 * @param rolledItemIndex Receives the bounty to grant.
 * @return True when this row is a bounty roll and its own item must NOT be granted.
 */
[[nodiscard]] bool roll_vendor_bounty(std::int32_t vendorIndex,
                                      std::int32_t categoryIndex,
                                      std::uint16_t& rolledItemIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    rolledItemIndex = kUnavailableDefinitionIndex;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (categoryIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    if (!core::path::read_artifact_text(L"vendor_bounty_roll.txt", g_ruleText)) {
        return false;
    }
    // Every hash authored for this exact key. Lines carrying the same key accumulate, so the pool
    // is gathered from the whole file rather than from the first line that matches.
    std::array<std::uint32_t, kRepeatablePoolCapacity> pool{};
    std::size_t poolCount = 0;
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (rules.seek_field()) {
        const std::uint32_t ruleHash = rules.read_hex();
        const std::int32_t ruleCategory = rules.read_decimal();
        const bool wanted = ruleHash == entry.definitionHash && ruleCategory == categoryIndex;
        // The rest of the line is item hashes. A newline is not a rule field, so this stops at the
        // end of the line without needing to look for one.
        while (rules.at_field()) {
            const std::uint32_t itemHash = rules.read_hex();
            if (wanted && poolCount < pool.size()) {
                pool[poolCount++] = itemHash;
            }
        }
    }
    if (poolCount == 0) {
        return false;
    }
    // Reservoir pick over what this build actually carries and the character does not already hold,
    // so the pool is walked once and no count is needed up front.
    std::uint32_t resolved = 0;
    std::uint32_t held = 0;
    std::uint32_t candidates = 0;
    std::uint64_t seed =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    // One account view for the whole pool. Reading it copies the whole account, and the pool is
    // walked candidate by candidate, so taking it per candidate would copy it dozens of times to
    // answer dozens of questions about the same unchanging view.
    const state::AccountState account = state::account_snapshot();
    for (std::size_t at = 0; at < poolCount; ++at) {
        state::build_data::items::Definition item{};
        if (!state::build_data::items::find_hash(pool[at], item)) {
            continue;
        }
        ++resolved;
        if (state::account::holds_pursuit(account, item.definitionIndex)) {
            ++held;
            continue;
        }
        ++candidates;
        seed = (seed * 6364136223846793005ULL) + 1442695040888963407ULL;
        if ((seed >> 33) % candidates == 0) {
            rolledItemIndex = item.definitionIndex;
        }
    }
    // Retail lets a character keep five of a vendor's repeatables at once. Refusing here rather
    // than at the grant keeps the roll from consuming a pick it would only have to throw away.
    if (held >= kRepeatableHoldLimit) {
        rolledItemIndex = kUnavailableDefinitionIndex;
    }
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=bounty_roll stage=pick vendor=%d hash=0x%08X category=%d authored=%u "
                      "resolved=%u held=%u pool=%u item=%d",
                      vendorIndex,
                      entry.definitionHash,
                      categoryIndex,
                      static_cast<unsigned>(poolCount),
                      resolved,
                      held,
                      candidates,
                      rolledItemIndex == kUnavailableDefinitionIndex
                          ? -1
                          : static_cast<int>(rolledItemIndex));
    return true;
}

/**
 * Runs a vendor's recycle row: charges the stack it names and credits what it pays out.
 *
 * The Drifter's four Synth Recycling rows take five synths each; Master Rahool's Recycle Shaders
 * category has one row per shader, 277 of them. The cost is authored in `vendor_exchange.txt`
 * rather than read off the row, because the sale row's cost-bearing fields are still role-open on
 * this build; the manifest's row order is this build's (304 rows checked against Lord Shaxx). A
 * rule is `<vendor> <row> <costItem> <costQuantity>` then `<payoutItem> <payoutQuantity>` pairs.
 *
 * @param vendorIndex Vendor the purchase names.
 * @param rowIndex Sale row the purchase names.
 * @param mutation Receives the prepared profile-stack change.
 * @return True when this row was an exchange and its own item must NOT be granted.
 */
[[nodiscard]] bool
exchange_vendor_row(std::int32_t vendorIndex,
                    std::int32_t rowIndex,
                    state::PendingProfileItemAcquisition& mutation) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (rowIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    if (!core::path::read_artifact_text(L"vendor_exchange.txt", g_ruleText)) {
        return false;
    }
    std::uint32_t costHash = 0;
    std::int32_t costQuantity = 0;
    std::array<state::ProfileExchangePayout, kExchangePayoutCapacity> payouts{};
    std::size_t payoutCount = 0;
    bool matched = false;
    bool overflowed = false;
    core::rule_text::Cursor rules{g_ruleText.data()};
    while (!matched && rules.seek_field()) {
        const std::uint32_t ruleVendor = rules.read_hex();
        const std::int32_t ruleRow = rules.read_decimal();
        const std::uint32_t ruleCost = rules.read_hex();
        const std::int32_t ruleCostQuantity = rules.read_decimal();
        // The rest of the line is payout pairs, and every one of them is consumed even past what
        // can be held. Stopping mid-line would leave the fields that did not fit to be read as the
        // start of the next rule, turning one over-long rule into a second, invented one.
        std::array<state::ProfileExchangePayout, kExchangePayoutCapacity> rulePayouts{};
        std::size_t rulePayoutCount = 0;
        bool ruleOverflowed = false;
        while (rules.at_field()) {
            const std::uint32_t payoutHash = rules.read_hex();
            const std::int32_t payoutQuantity = rules.read_decimal();
            if (rulePayoutCount < rulePayouts.size()) {
                rulePayouts[rulePayoutCount++] = {payoutHash, payoutQuantity};
            } else {
                ruleOverflowed = true;
            }
        }
        matched = ruleVendor == entry.definitionHash && ruleRow == rowIndex;
        if (matched) {
            overflowed = ruleOverflowed;
            costHash = ruleCost;
            costQuantity = ruleCostQuantity;
            payouts = rulePayouts;
            payoutCount = rulePayoutCount;
        }
    }
    if (!matched) {
        return false;
    }
    // A matched rule owns the row whatever else it got wrong, because the rule proves the row's
    // own item is a placeholder and falling through would grant it. A rule naming more payouts
    // than the change ring can announce, or none at all, is refused whole rather than paid in
    // part - and the refusal is logged, because a rule that silently does nothing reads exactly
    // like a rule that was never written.
    if (overflowed || payoutCount == 0) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=vendor_exchange stage=apply result=fail reason=%s vendor=%d "
                          "hash=0x%08X row=%d payouts=%zu limit=%zu",
                          overflowed ? "payout_overflow" : "payout_missing",
                          vendorIndex,
                          entry.definitionHash,
                          rowIndex,
                          payoutCount,
                          kExchangePayoutCapacity);
        return true;
    }
    const bool applied = state::prepare_vendor_exchange(
        costHash, costQuantity,
        std::span<const state::ProfileExchangePayout>{payouts.data(), payoutCount}, mutation);
    core::log::writef(core::log::Channel::server,
                      applied ? core::log::Level::info : core::log::Level::warn,
                      "ev=vendor_exchange stage=apply result=%s vendor=%d hash=0x%08X row=%d "
                      "cost=0x%08X quantity=%d payouts=%zu",
                      applied ? "ok" : "fail",
                      vendorIndex,
                      entry.definitionHash,
                      rowIndex,
                      costHash,
                      costQuantity,
                      payoutCount);
    // Even a refused exchange owns the row. Falling through would grant the Dummy placeholder,
    // which is the failure this whole path exists to avoid.
    return true;
}

/** How one grant ended, so a caller can tell a settled row from a row still owed its item. */
enum class GrantResult : std::uint8_t {
    /** The item is prepared for the inventory; the row's offer is answered. */
    granted,
    /** The character already holds this pursuit, so the offer was answered some time ago. */
    alreadyHeld,
    /** Nothing was granted and nothing was held; the offer still stands. */
    refused,
};

/**
 * Grants one item, given the collectible that owns it and its definition index.
 *
 * Split out of `acquire_item` so a vendor purchase reaches the same grant instead of growing a
 * second acquisition path. The acquisition state is keyed by collectible, so a caller has to arrive
 * with one; `find_collectible_for_item` is how a purchase gets there.
 *
 * @param message Request being answered, for the log line.
 * @param collectibleIndex Collectible that owns the item.
 * @param itemDefinitionIndex Item to grant.
 * @param outcome Receives the prepared mutation on success.
 * @return How the grant ended, which is what decides whether the row's offer was answered.
 */
GrantResult grant_item_definition(const middleware::web_service::Message& message,
                                  std::uint16_t collectibleIndex,
                                  std::uint16_t itemDefinitionIndex,
                                  Outcome& outcome) noexcept {
    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(itemDefinitionIndex, definition)) {
        report_item_acquisition(
            message, "item_definition", collectibleIndex, itemDefinitionIndex, 0, 0);
        return GrantResult::refused;
    }
    // The same rule the client's native vendor-row gate applies locally, so a row that is still
    // offered can never be one this grant would refuse.
    if (state::account::holds_pursuit(itemDefinitionIndex)) {
        report_item_acquisition(message,
                                "already_held",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return GrantResult::alreadyHeld;
    }

    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (!state::build_data::find_configured_item_detail(itemDefinitionIndex, detail)
        || detail.definitionIndex != itemDefinitionIndex
        || detail.definitionHash != definition.definitionHash
        || detail.bucketId != definition.bucketId
        || !state::build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)) {
        report_item_acquisition(message,
                                "item_detail_or_bucket",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return GrantResult::refused;
    }

    namespace bucket_domain = state::build_data::inventory::buckets;
    namespace detail_domain = state::build_data::items::details;
    if (bucket.arraySelector == bucket_domain::ArraySelector::profile) {
        if (detail.instancedDefinitionState != detail_domain::InstancedDefinitionState::stackable) {
            report_item_acquisition(message,
                                    "profile_item_instanced",
                                    collectibleIndex,
                                    itemDefinitionIndex,
                                    definition.definitionHash,
                                    0);
            return GrantResult::refused;
        }
        auto* mutation = emplace_mutation<state::PendingProfileItemAcquisition>(outcome);
        if (mutation == nullptr) {
            report_item_acquisition(message,
                                    "storage",
                                    collectibleIndex,
                                    itemDefinitionIndex,
                                    definition.definitionHash,
                                    0);
            return GrantResult::refused;
        }
        if (!state::prepare_profile_item_acquisition(
                collectibleIndex, definition.definitionHash, *mutation)) {
            clear_mutation(outcome);
            report_item_acquisition(message,
                                    "profile_state",
                                    collectibleIndex,
                                    itemDefinitionIndex,
                                    definition.definitionHash,
                                    0);
            return GrantResult::refused;
        }
        return GrantResult::granted;
    }
    if (bucket.arraySelector != bucket_domain::ArraySelector::character) {
        report_item_acquisition(message,
                                "unsupported_inventory_array",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return GrantResult::refused;
    }

    auto* mutation = emplace_mutation<state::PendingItemAcquisition>(outcome);
    if (mutation == nullptr) {
        report_item_acquisition(message,
                                "storage",
                                collectibleIndex,
                                itemDefinitionIndex,
                                definition.definitionHash,
                                0);
        return GrantResult::refused;
    }
    if (!state::prepare_item_acquisition(collectibleIndex, definition.definitionHash, *mutation)) {
        clear_mutation(outcome);
        report_item_acquisition(
            message, "state", collectibleIndex, itemDefinitionIndex, definition.definitionHash, 0);
        return GrantResult::refused;
    }
    return GrantResult::granted;
}

/**
 * Finds the collectible that owns one item definition.
 *
 * A sale row names an item, never a collectible, while the acquisition state is keyed by
 * collectible. Bounties, tokens and quest steps have none at all; those are granted by hash under
 * `kNoCollectibleIndex`, which is why the caller's sentinel is left in place when nothing matches.
 *
 * @param itemDefinitionIndex Item to look up.
 * @param collectibleIndex Receives the owning collectible row; untouched when none does.
 * @return True when a collectible names this item.
 */
[[nodiscard]] bool find_collectible_for_item(std::uint16_t itemDefinitionIndex,
                                             std::uint16_t& collectibleIndex) noexcept {
    return state::build_data::collectibles::find_granting(itemDefinitionIndex, collectibleIndex);
}

/** Prepares the exact three-byte opcode-1820 Collections item request. */
void acquire_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    middleware::web_service::messages::opcode1820::Request request{};
    if (!middleware::web_service::messages::opcode1820::parse_request(message, request)) {
        report_item_acquisition(message,
                                "payload_bits",
                                kUnavailableDefinitionIndex,
                                kUnavailableDefinitionIndex,
                                0,
                                0);
        return;
    }
    const std::uint16_t collectibleIndex = request.collectibleIndex;
    std::uint16_t itemDefinitionIndex = 0;
    if (!state::build_data::find_collectible_item_definition_index(collectibleIndex,
                                                                   itemDefinitionIndex)) {
        report_item_acquisition(
            message, "collectible_definition", collectibleIndex, kUnavailableDefinitionIndex, 0, 0);
        return;
    }
    (void)grant_item_definition(message, collectibleIndex, itemDefinitionIndex, outcome);
}

/**
 * Resolves one vendor row to the item it sells.
 *
 * Shared by the purchase (901) and the quest acquire (904), which name a row the same way, so the
 * two cannot drift apart.
 *
 * @param vendorIndex Vendor table row.
 * @param rowIndex Sale row within that vendor.
 * @param itemDefinitionIndex Receives the item the row sells.
 * @param reason Receives the step that failed, when one does.
 * @return True when the row resolved.
 */
[[nodiscard]] bool resolve_vendor_row(std::int32_t vendorIndex,
                                      std::int32_t rowIndex,
                                      std::uint16_t& itemDefinitionIndex,
                                      std::int32_t& categoryIndex,
                                      const char*& reason) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    if (vendorIndex < 0 || rowIndex < 0) {
        reason = "negative_index";
        return false;
    }
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (!find_vendor(vendorIndex, entry, definition)) {
        reason = "vendor";
        return false;
    }
    vendor_domain::SaleRow row{};
    if (!vendor_domain::sale_row(definition, static_cast<std::size_t>(rowIndex), row)) {
        reason = "sale_row";
        return false;
    }
    itemDefinitionIndex = row.itemIndex;
    categoryIndex = row.categoryIndex;
    return true;
}

/** Pursuit rows written out when a vendor is asked what it actually sells. */
constexpr std::size_t kPursuitListCap = 64;

/**
 * Lists the sale rows of one vendor whose item is a pursuit, when a rowless tile fails to resolve.
 *
 * It says what this vendor does offer that would land in the Quests tab, which is the difference
 * between "this click is broken" and "this click was never a quest". Items rather than rows, because
 * one placeholder repeats across dozens of rows. The classification is the shared pursuit rule.
 *
 * @param vendorIndex Vendor to list.
 */
void report_pursuit_rows(std::int32_t vendorIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    namespace detail_domain = state::build_data::items::details;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (!find_vendor(vendorIndex, entry, definition)) {
        return;
    }
    const std::size_t count = definition.saleCount;
    // One item repeats across dozens of rows - Amanda declares 38 consecutive rows of a single
    // placeholder - so listing rows rather than items buries everything interesting under filler.
    static std::array<std::uint16_t, kPursuitListCap> seen{};
    std::size_t listed = 0;
    std::size_t pursuits = 0;
    for (std::size_t row = 0; row < count; ++row) {
        vendor_domain::SaleRow sale{};
        if (!vendor_domain::sale_row(definition, row, sale)) {
            break;
        }
        const std::uint16_t itemIndex = sale.itemIndex;
        detail_domain::Definition detail{};
        if (!state::build_data::find_configured_item_detail(itemIndex, detail)
            || detail.equipmentSlot.has_value() || detail.maxStackSize > 1) {
            continue;
        }
        ++pursuits;
        bool duplicate = false;
        for (std::size_t index = 0; index < listed; ++index) {
            duplicate = duplicate || seen[index] == itemIndex;
        }
        if (duplicate || listed >= kPursuitListCap) {
            continue;
        }
        seen[listed] = itemIndex;
        ++listed;
        // One line per distinct row, so this is the detail behind the summary rather than
        // something worth putting in front of everything else that reports at info.
        core::log::writef(core::log::Channel::server,
                          core::log::Level::debug,
                          "ev=vendor stage=pursuit vendor=%d sale=%zu item=%u hash=0x%08X "
                          "bucket=%u",
                          static_cast<int>(vendorIndex),
                          row,
                          static_cast<unsigned>(itemIndex),
                          detail.definitionHash,
                          static_cast<unsigned>(detail.bucketId));
    }
    core::log::writef(core::log::Channel::server,
                      core::log::Level::info,
                      "ev=vendor stage=pursuits vendor=%d sale_rows=%zu pursuits=%zu "
                      "distinct_listed=%zu",
                      static_cast<int>(vendorIndex),
                      count,
                      pursuits,
                      listed);
}

/** An installed row names its item by definition hash at this offset. */
constexpr std::size_t kInstalledRowHashOffset = 0;
/** FNV-1's basis, which this engine also uses as its absent-hash sentinel. */
constexpr std::uint32_t kAbsentNameHash = 0x811C9DC5U;

/**
 * Resolves the item behind a 904 that names no sale row.
 *
 * Amanda Holliday's Legacy Content tiles send `slot=1, row=-1`, so the slot is all that identifies
 * them - and it indexes the installed array: the Red War tile's vendor declares 220 sale rows but
 * 22 installed rows, and its slot is 1. That installed row carries the item's definition hash at
 * `+0`, where a sale row names its item by index. The resolution is logged either way, because a
 * wrong item that commits cleanly is harder to spot than a refusal.
 *
 * @param vendorIndex Vendor the request named.
 * @param slotIndex The 16-bit slot field, which is all the request carries.
 * @param itemDefinitionIndex Receives the item, or the unavailable sentinel.
 * @return True when the row's hash resolved to an installed item definition.
 */
[[nodiscard]] bool resolve_rowless_quest(std::int32_t vendorIndex,
                                         std::int32_t slotIndex,
                                         std::uint16_t& itemDefinitionIndex) noexcept {
    namespace vendor_domain = state::build_data::vendors;
    itemDefinitionIndex = kUnavailableDefinitionIndex;
    vendor_domain::IndexEntry entry{};
    vendor_domain::Definition definition{};
    if (slotIndex < 0 || !find_vendor(vendorIndex, entry, definition)) {
        return false;
    }
    vendor_domain::InstalledRow installed{};
    if (!vendor_domain::installed_row(definition, static_cast<std::size_t>(slotIndex), installed)) {
        return false;
    }
    const auto& raw = installed.raw;
    std::uint32_t definitionHash = 0;
    std::memcpy(&definitionHash, raw.data() + kInstalledRowHashOffset, sizeof definitionHash);

    state::build_data::items::Definition item{};
    const bool resolved = definitionHash != kAbsentNameHash
                          && state::build_data::find_item_definition_hash(definitionHash, item);
    if (resolved) {
        itemDefinitionIndex = item.definitionIndex;
    }
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=ws904 stage=rowless vendor=%d slot=%d installed=%u sale=%u "
                                "third=%u hash=0x%08X item=%u resolved=%u hex=",
                                static_cast<int>(vendorIndex),
                                static_cast<int>(slotIndex),
                                static_cast<unsigned>(definition.installedCount),
                                static_cast<unsigned>(definition.saleCount),
                                static_cast<unsigned>(definition.thirdCount),
                                definitionHash,
                                static_cast<unsigned>(itemDefinitionIndex),
                                resolved ? 1U : 0U);
    if (written > 0 && static_cast<std::size_t>(written) < line.size()) {
        std::size_t length = static_cast<std::size_t>(written);
        const auto* const bytes = reinterpret_cast<const std::byte*>(raw.data());
        (void)core::log::append_hex(line, length, {bytes, raw.size()});
        if (length != 0) {
            core::log::write(core::log::Channel::server,
                             resolved ? core::log::Level::info : core::log::Level::warn,
                             {line.data(), length});
        }
    }
    return resolved;
}

/** What one resolved vendor row turned out to be, once it was settled. */
enum class RowOutcome : std::uint8_t {
    /** The row rolled a bounty from an authored pool. */
    bountyRoll,
    /** The row charged one stack and credited others. */
    exchange,
    /** The row's item is prepared for the inventory; its offer is answered once that commits. */
    granted,
    /** The character already holds the row's pursuit, so its offer was answered some time ago. */
    alreadyHeld,
    /** The row should have granted and could not, so its offer still stands. */
    grantRefused,
};

/**
 * Settles one resolved vendor row, in the order a row's behaviours are tried.
 *
 * Both vendor opcodes end here. A row is a bounty roll, an exchange, or a grant, and which cannot
 * be read off the row itself: each is recognised by an authored rule keyed to the vendor, tried in
 * turn, and the first that claims the row owns it. One ordered chain is what keeps 901 and 904 from
 * drifting apart.
 *
 * @param message Request being answered.
 * @param opcode Opcode to report under.
 * @param vendorIndex Vendor the request names.
 * @param rowIndex Sale row the request names.
 * @param categoryIndex Category of that row, from sale row +100.
 * @param itemDefinitionIndex Item the row names.
 * @param outcome Receives whatever mutation the row prepared.
 * @return What the row turned out to be.
 */
RowOutcome settle_vendor_row(const middleware::web_service::Message& message,
                             std::uint16_t opcode,
                             std::int32_t vendorIndex,
                             std::int32_t rowIndex,
                             std::int32_t categoryIndex,
                             std::uint16_t itemDefinitionIndex,
                             Outcome& outcome) noexcept {
    std::uint16_t rolledBounty = kUnavailableDefinitionIndex;
    if (roll_vendor_bounty(vendorIndex, categoryIndex, rolledBounty)) {
        report_purchase(opcode,
                        "ok",
                        rolledBounty == kUnavailableDefinitionIndex ? "bounty_pool_empty"
                                                                   : "bounty_roll",
                        vendorIndex,
                        rowIndex,
                        itemDefinitionIndex);
        if (rolledBounty != kUnavailableDefinitionIndex) {
            std::uint16_t rolledCollectible = state::build_data::collectibles::kNoCollectibleIndex;
            (void)find_collectible_for_item(rolledBounty, rolledCollectible);
            (void)grant_item_definition(message, rolledCollectible, rolledBounty, outcome);
        }
        return RowOutcome::bountyRoll;
    }
    // The exchange is prepared in place: the payload is allocated only for this row, and a row
    // that turns out not to be an exchange gives it back before the grant path runs.
    auto* exchange = emplace_mutation<state::PendingProfileItemAcquisition>(outcome);
    if (exchange == nullptr) {
        report_purchase(opcode, "fail", "storage", vendorIndex, rowIndex, itemDefinitionIndex);
        return RowOutcome::grantRefused;
    }
    if (exchange_vendor_row(vendorIndex, rowIndex, *exchange)) {
        report_purchase(opcode, "ok", "exchange", vendorIndex, rowIndex, itemDefinitionIndex);
        if (!exchange->prepared) {
            clear_mutation(outcome);
        }
        return RowOutcome::exchange;
    }
    clear_mutation(outcome);
    // A placeholder row grants what it stands for, not the placeholder: a Dummy item put in the
    // Quests bucket is one the client will not draw, and the row never settles because the player
    // never receives what it offered.
    std::uint16_t granted = itemDefinitionIndex;
    std::uint16_t substituteIndex = kUnavailableDefinitionIndex;
    switch (substitute_for_item(granted, substituteIndex)) {
    case Substitution::replaced:
        granted = substituteIndex;
        break;
    case Substitution::broken:
        // The rule proves the row's item is a placeholder, so granting it would be the wrong
        // grant this path exists to prevent. The rule itself already logged what is missing.
        report_purchase(opcode, "fail", "substitute_missing", vendorIndex, rowIndex, granted);
        return RowOutcome::grantRefused;
    case Substitution::none:
        break;
    }
    std::uint16_t collectibleIndex = state::build_data::collectibles::kNoCollectibleIndex;
    const bool collected = find_collectible_for_item(granted, collectibleIndex);
    report_purchase(opcode,
                    "ok",
                    collected ? "resolved" : "resolved_no_collectible",
                    vendorIndex,
                    rowIndex,
                    granted);
    // A grant that failed for a transient reason - the loadout would not resolve, the bucket was
    // full - leaves the row's offer standing, and the caller must not treat it as answered.
    switch (grant_item_definition(message, collectibleIndex, granted, outcome)) {
    case GrantResult::granted:
        return RowOutcome::granted;
    case GrantResult::alreadyHeld:
        return RowOutcome::alreadyHeld;
    case GrantResult::refused:
        break;
    }
    return RowOutcome::grantRefused;
}

/**
 * Prepares one opcode-904 quest acquire.
 *
 * A quest names a vendor row exactly as a purchase does, and the item behind it is granted through
 * the same path, so a quest lands in the inventory the way a bounty now does.
 */
void acquire_quest(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace quest = middleware::web_service::messages::opcode904;
    quest::Request request{};
    if (!quest::parse_request(message, request)) {
        report_purchase(quest::kOpcode, "fail", "payload", -1, -1, kUnavailableDefinitionIndex);
        return;
    }
    // The 16-bit slot field is where the click landed, and indexing sale rows with it granted
    // armour mods. The 32-bit field is the real row; a body without one has never been captured,
    // and guessing the slot in as a sale row would reproduce that exact wrong grant - so it is
    // refused, and the refusal names the shape so a real capture can settle it.
    if (!request.hasSaleIndex) {
        report_purchase(quest::kOpcode,
                        "fail",
                        "sale_field_missing",
                        request.vendorIndex,
                        request.slotIndex,
                        kUnavailableDefinitionIndex);
        return;
    }
    const std::int32_t row = request.saleIndex;
    std::uint16_t itemDefinitionIndex = 0;
    const char* reason = "unknown";
    // A row of -1 is the client saying this tile is not a sale row at all, rather than a row that
    // failed to resolve, so it takes the installed array instead. Falling back to the slot as a
    // sale row would grant whatever sits there, which is the wrong-item bug that made quests hand
    // out armour mods.
    const bool rowless = row < 0;
    // A rowless 904 is an interaction reply rather than a purchase, and the rank-up reward tile is
    // one: its reply names no sale row, so the slot field is the interaction it answered.
    std::int32_t questCategoryIndex = -1;
    const bool located =
        rowless ? resolve_rowless_quest(request.vendorIndex, request.slotIndex, itemDefinitionIndex)
                : resolve_vendor_row(request.vendorIndex, row, itemDefinitionIndex,
                                    questCategoryIndex, reason);
    if (!located) {
        report_purchase(quest::kOpcode,
                        "fail",
                        rowless ? "rowless_unresolved" : reason,
                        request.vendorIndex,
                        row,
                        kUnavailableDefinitionIndex);
        // A tile that names no row grants nothing, so say what this vendor does offer that would
        // land in the Quests tab. That is the difference between "this click is broken" and "this
        // click was never a quest".
        if (rowless) {
            report_pursuit_rows(request.vendorIndex);
        }
        return;
    }
    const RowOutcome settled = settle_vendor_row(message,
                                                 quest::kOpcode,
                                                 request.vendorIndex,
                                                 row,
                                                 questCategoryIndex,
                                                 itemDefinitionIndex,
                                                 outcome);
    // The banner that offered this quest is answered only by a row whose offer is answered, and
    // nothing else tells the client so: its picker keeps choosing the same interaction for as long
    // as the quest is offerable. A bounty roll and an exchange leave the banner's own question
    // unanswered, and a refused grant still owes the player its quest.
    if (request.vendorIndex < 0
        || request.vendorIndex >= static_cast<std::int32_t>(state::vendors::kVendorCapacity)) {
        return;
    }
    const auto vendor = static_cast<std::uint16_t>(request.vendorIndex);
    switch (settled) {
    case RowOutcome::alreadyHeld:
        // Answered some time ago, and nothing is left to commit, so the banner retires now. This
        // is the re-click on a quest already in the tab.
        (void)state::vendors::answer_shown(vendor);
        break;
    case RowOutcome::granted:
        // Prepared, not committed. The answer rides the transaction and is written where the
        // grant commits, so a mutation dropped on the way never buries a quest still owed.
        outcome.answeredVendor = vendor;
        break;
    case RowOutcome::bountyRoll:
    case RowOutcome::exchange:
    case RowOutcome::grantRefused:
        break;
    }
}

/**
 * Prepares one opcode-901 vendor purchase, for any Tower vendor.
 *
 * The request names a vendor row and a sale row. The sale row names an item-definition index, which
 * is the same thing a Collections pull resolves its collectible to, so this resolves the row and
 * hands over to the very same grant.
 *
 * Cost is deliberately not charged: the sale row's cost-bearing fields are still role-open, and the
 * domain header warns against naming one a cost without its mutation reader.
 */
void purchase_item(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace purchase = middleware::web_service::messages::opcode901;
    purchase::Request request{};
    if (!purchase::parse_request(message, request)) {
        report_purchase(purchase::kOpcode, "fail", "payload", -1, -1, kUnavailableDefinitionIndex);
        return;
    }
    std::uint16_t itemDefinitionIndex = 0;
    const char* reason = "unknown";
    std::int32_t categoryIndex = -1;
    if (!resolve_vendor_row(
            request.vendorIndex, request.saleIndex, itemDefinitionIndex, categoryIndex, reason)) {
        report_purchase(purchase::kOpcode,
                        "fail",
                        reason,
                        request.vendorIndex,
                        request.saleIndex,
                        kUnavailableDefinitionIndex);
        return;
    }
    // Bounties, quest steps and tokens carry no collectible. The acquisition takes the sentinel
    // rather than a made-up row, and both prepare and commit skip the collectible steps for it.
    (void)settle_vendor_row(message,
                            purchase::kOpcode,
                            request.vendorIndex,
                            request.saleIndex,
                            categoryIndex,
                            itemDefinitionIndex,
                            outcome);
}
/** Reports a record-reward preparation failure. */
void report_record_reward(const middleware::web_service::Message& message,
                          std::string_view reason,
                          std::uint32_t recordIndex,
                          std::uint32_t itemIndex,
                          std::int32_t quantity) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=ws1801 stage=reward result=fail reason=%.*s transaction=%u "
                                    "record_index=%u item_index=%u quantity=%d",
                                    static_cast<int>(reason.size()),
                                    reason.data(),
                                    static_cast<unsigned>(message.transactionId),
                                    recordIndex,
                                    itemIndex,
                                    quantity);
    write_warning(line, count);
}

/** Prepares one direct Season-pass item, returning a failure reason or null. */
[[nodiscard]] const char* prepare_direct_reward(std::uint16_t itemIndex,
                                                std::int32_t quantity,
                                                state::PendingSeasonPassReward& grant) noexcept {
    if (quantity <= 0) {
        return "quantity";
    }
    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_index(itemIndex, definition)) {
        return "item_definition";
    }

    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (!state::build_data::find_configured_item_detail(itemIndex, detail)
        || detail.definitionIndex != itemIndex || detail.definitionHash != definition.definitionHash
        || detail.bucketId != definition.bucketId
        || !state::build_data::find_inventory_bucket_descriptor(detail.bucketId, bucket)) {
        return "item_detail_or_bucket";
    }

    namespace bucket_domain = state::build_data::inventory::buckets;
    namespace detail_domain = state::build_data::items::details;
    if (bucket.arraySelector == bucket_domain::ArraySelector::profile) {
        if (detail.instancedDefinitionState != detail_domain::InstancedDefinitionState::stackable) {
            return "profile_item_instanced";
        }
        auto& mutation = grant.grant.emplace<state::PendingProfileItemAcquisition>();
        return state::prepare_profile_item_acquisition_for_item(itemIndex, quantity, mutation)
                   ? nullptr
                   : "profile_state";
    }
    if (bucket.arraySelector == bucket_domain::ArraySelector::character) {
        if (quantity != 1) {
            return "character_item_quantity";
        }
        auto& mutation = grant.grant.emplace<state::PendingItemAcquisition>();
        if (!state::prepare_item_acquisition_for_item(itemIndex, mutation)) {
            return "state";
        }
        mutation.profileChanged = false;
        return nullptr;
    }
    return "unsupported_inventory_array";
}

enum class RecordRewardPreparation : std::uint8_t {
    absent,
    prepared,
    failed,
};

/** Prepares a settings override or, when absent, the generated manifest reward. */
[[nodiscard]] RecordRewardPreparation
prepare_record_reward(const middleware::web_service::Message& message,
                      std::uint16_t recordIndex,
                      std::uint32_t recordHash,
                      const state::record_claims::PendingClaim& claim,
                      Outcome& outcome) noexcept {
    std::array<state::DirectRecordReward, state::kRecordRewardGrantCapacity> rewards{};
    std::size_t rewardCount = 0;
    state::RecordRewardPolicy override{};
    if (state::account::find_record_reward(state::account_snapshot(), recordIndex, override)) {
        rewards[0] = {override.itemIndex, override.quantity};
        rewardCount = 1;
    } else {
        std::array<state::build_data::records::rewards::ResolvedReward,
                   state::build_data::records::rewards::kRewardPerRecordCapacity>
            generated{};
        if (!state::build_data::find_generated_record_rewards(recordHash, generated, rewardCount)) {
            report_record_reward(message, "reward_table", recordIndex, 0, 0);
            return RecordRewardPreparation::failed;
        }
        if (rewardCount == 0) {
            return RecordRewardPreparation::absent;
        }
        for (std::size_t index = 0; index < rewardCount; ++index) {
            rewards[index] = {generated[index].itemDefinitionIndex, generated[index].quantity};
        }
    }

    auto* grant = emplace_mutation<state::PendingRecordRewardGrant>(outcome);
    if (grant == nullptr) {
        report_record_reward(
            message, "storage", recordIndex, rewards[0].itemDefinitionIndex, rewards[0].quantity);
        return RecordRewardPreparation::failed;
    }
    if (!state::prepare_record_reward_grant(std::span(rewards).first(rewardCount), claim, *grant)) {
        clear_mutation(outcome);
        report_record_reward(
            message, "state", recordIndex, rewards[0].itemDefinitionIndex, rewards[0].quantity);
        return RecordRewardPreparation::failed;
    }
    return RecordRewardPreparation::prepared;
}

/** Claims one exact reward-array row from the active Season of Arrivals pass. */
void claim_season_pass_reward(const middleware::web_service::Message& message,
                              Outcome& outcome) noexcept {
    namespace pass = state::progression::season_pass;
    namespace experience = state::progression::seasonal_experience;
    middleware::web_service::messages::opcode2400::Request request{};
    const pass::Reward* reward = nullptr;
    state::build_data::items::Definition item{};
    const std::uint16_t rank = experience::rank();
    const auto fail = [&](std::string_view reason) noexcept {
        std::array<char, core::log::kLineCapacity> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=ws2400 stage=claim result=fail reason=%.*s transaction=%u progression=%u "
            "reward=%u rank=%u item_hash=0x%08X item_index=%u",
            static_cast<int>(reason.size()),
            reason.data(),
            static_cast<unsigned>(message.transactionId),
            static_cast<unsigned>(request.progressionIndex),
            static_cast<unsigned>(request.rewardIndex),
            static_cast<unsigned>(rank),
            reward == nullptr ? 0U : reward->itemHash,
            static_cast<unsigned>(item.definitionIndex));
        write_warning(line, count);
    };

    if (!middleware::web_service::messages::opcode2400::parse_request(message, request)) {
        return fail("payload_bits");
    }
    if (request.progressionIndex != pass::kProgressionDefinitionIndex) {
        return fail("progression");
    }
    reward = pass::find(request.rewardIndex);
    if (reward == nullptr) {
        return fail("reward_index");
    }
    if (reward->requiredRank > rank) {
        return fail("rank");
    }
    if (experience::reward_claimed(request.rewardIndex)) {
        return fail("already_claimed");
    }
    if (!state::build_data::find_item_definition_hash(reward->itemHash, item)) {
        return fail("item_hash");
    }
    auto* grant = emplace_mutation<state::PendingSeasonPassReward>(outcome);
    if (grant == nullptr) {
        return fail("storage");
    }
    if (const auto* package = pass::find_premium_class_package(reward->itemHash)) {
        auto& bundle = grant->grant.emplace<state::PendingDirectItemBundle>();
        if (!prepare_premium_class_package(*package, bundle)) {
            clear_mutation(outcome);
            return fail("package_grant");
        }
    } else if (reward->itemHash == pass::kDestinationResourceBundleHash) {
        if (!prepare_destination_resource_bundle(*grant)) {
            clear_mutation(outcome);
            return fail("resource_bundle");
        }
    } else if (reward->itemHash == pass::kLegendaryEngramHash
               || reward->itemHash == pass::kExoticEngramHash) {
        std::uint16_t decryptedItemIndex = 0;
        if (!choose_engram_reward(reward->itemHash, decryptedItemIndex)) {
            clear_mutation(outcome);
            return fail("engram_pool");
        }
        if (const char* reason = prepare_direct_reward(decryptedItemIndex, 1, *grant)) {
            clear_mutation(outcome);
            return fail(reason);
        }
    } else {
        if (const char* reason =
                prepare_direct_reward(item.definitionIndex, reward->quantity, *grant)) {
            clear_mutation(outcome);
            return fail(reason);
        }
        if (auto* armour = std::get_if<state::PendingItemAcquisition>(&grant->grant);
            armour != nullptr && !apply_season_armour_roll(request.rewardIndex, *armour)) {
            clear_mutation(outcome);
            return fail("armour_roll");
        }
    }
    grant->sourceDefinitionHash = reward->itemHash;
    grant->rewardIndex = request.rewardIndex;
    grant->prepared = true;
}

/** Decodes one opcode-1801 Triumphs claim and reports the record it names. */
void claim_record(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace records = state::build_data::records;
    middleware::web_service::messages::opcode1801::Request request{};
    if (!middleware::web_service::messages::opcode1801::parse_request(message, request)) {
        report_record_claim(message, "payload_bits", 0, records::kUnavailableFlagIndex, 0);
        return;
    }
    records::Definition definition{};
    if (!state::build_data::find_record_definition(request.recordIndex, definition)) {
        report_record_claim(
            message, "record_definition", request.recordIndex, records::kUnavailableFlagIndex, 0);
        return;
    }
    if (definition.completionFlagIndex == records::kUnavailableFlagIndex) {
        // Interval Triumphs intentionally carry no completion flag. Their repeated redemption
        // count is projected through the second reserved objective-value slot instead.
        if (state::record_claims::claim_interval(request.recordIndex,
                                                 definition.definitionHash)) {
            outcome.hasRecordClaim = true;
            return;
        }
        report_record_claim(message,
                            "interval_or_flag_unavailable",
                            request.recordIndex,
                            records::kUnavailableFlagIndex,
                            definition.scoreValue);
        return;
    }
    if (state::record_claims::claimed(definition.completionFlagIndex)) {
        report_record_claim(message,
                            "claim_rejected",
                            request.recordIndex,
                            definition.completionFlagIndex,
                            definition.scoreValue);
        return;
    }

    const state::record_claims::PendingClaim pendingClaim{definition.completionFlagIndex,
                                                          definition.scoreValue};
    const RecordRewardPreparation reward = prepare_record_reward(
        message, request.recordIndex, definition.definitionHash, pendingClaim, outcome);
    if (reward == RecordRewardPreparation::failed) {
        return;
    }
    if (reward == RecordRewardPreparation::prepared) {
        return;
    }
    if (!state::record_claims::claim(definition.completionFlagIndex, definition.scoreValue)) {
        report_record_claim(message,
                            "claim_rejected",
                            request.recordIndex,
                            definition.completionFlagIndex,
                            definition.scoreValue);
        return;
    }
    outcome.hasRecordClaim = true;
}

/** Equips an earned title record on the selected character. */
void equip_title(const middleware::web_service::Message& message, Outcome& outcome) noexcept {
    namespace records = state::build_data::records;
    middleware::web_service::messages::opcode1821::Request request{};
    records::Definition definition{};
    std::uint64_t characterSoid = 0;
    bool changed = false;
    const char* reason = "payload_bits";
    if (middleware::web_service::messages::opcode1821::parse_request(message, request)) {
        if (request.recordIndex
            == middleware::web_service::messages::opcode1821::kUnequippedRecordIndex) {
            reason = "selected_character";
            if (state::set_selected_title(
                    state::kUnequippedTitleRecordIndex, characterSoid, changed)) {
                outcome.hasTitleEquip = true;
                return;
            }
        } else {
            reason = "record_definition";
            if (state::build_data::find_record_definition(request.recordIndex, definition)) {
                reason = "not_title";
                if (definition.hasTitle) {
                    reason = "not_claimed";
                    if (definition.completionFlagIndex != records::kUnavailableFlagIndex
                        && state::record_claims::claimed(definition.completionFlagIndex)) {
                        reason = "selected_character";
                        if (state::set_selected_title(
                                request.recordIndex, characterSoid, changed)) {
                            outcome.hasTitleEquip = true;
                            return;
                        }
                    }
                }
            }
        }
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=title_equip result=fail reason=%s opcode=%u transaction=%u record=%u "
                      "definition_hash=0x%08X completion_flag=%u character=0x%llX",
                      reason,
                      static_cast<unsigned>(message.opcode),
                      static_cast<unsigned>(message.transactionId),
                      static_cast<unsigned>(request.recordIndex),
                      definition.definitionHash,
                      static_cast<unsigned>(definition.completionFlagIndex),
                      static_cast<unsigned long long>(characterSoid));
    write_warning(line, count);
}

} // namespace sunrise::server::web_service
