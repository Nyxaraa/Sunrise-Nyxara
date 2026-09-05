/**
 * Framing handlers for every activity message that changes no State.
 * Each one reads as much of its body as the known grammar reaches, and reports what it saw.
 * It returns how completely the body was read, so the caller can record one arrival receipt.
 * None of them acts on what it read.
 */

#include "activity_message_receipts.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../../../client/player/player_position.h"
#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/bap/activity_message/activity_client_keepalive_validator.h"
#include "../../../../../middleware/bap/activity_message/entity_authority.h"
#include "../../../../../middleware/bap/activity_message/incident.h"
#include "../../../../../middleware/bap/activity_message/peer_ledger.h"
#include "../../../../../middleware/bap/activity_message/sense_update.h"
#include "../../../../../middleware/bap/activity_message/start_activity.h"
#include "../../../../../middleware/bap/activity_message/telemetry.h"
#include "../../../../../middleware/crypto/random_bytes.h"
#include "../../../../../middleware/encoding/byte_order.h"
#include "../../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/build_data/sobjects/sobject_catalog.h"
#include "../../../../../state/lore/lore_grant.h"
#include "../../../../../state/progression/seasonal_experience.h"
#include "../../../../../state/record_claims/record_claims.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../bap/internal.h"

namespace sunrise::server::bap::encrypted::activity_message::receipts {
namespace {

namespace store = state::activity::receipts;
namespace authority = message::entity_authority;
namespace ledger = message::peer_ledger;
namespace telemetry = message::telemetry;
namespace sense = message::sense_update;

using store::Verdict;

/** Type-4 Sense has seven fixed prefix values when it carries no predicate replies. */
constexpr std::uint32_t kObjectSenseSchema = 0x8080992EU;
constexpr std::uint32_t kObjectSenseValueCount = 7;

/** Type-2 Sense reports the atom runner's progress, so its value count varies with the body. */
constexpr std::uint32_t kCombatantSenseSchema = 0x80807DA2U;
constexpr std::uint32_t kCombatantAtomsSchema = 0x80807F6EU;

/** Type-1 Sense carries the alive count and the eight per-slot member counts. */
constexpr std::uint32_t kSquadSenseSchema = 0x80807ECCU;
constexpr std::uint32_t kSquadSlotsSchema = 0x80807ECFU;
constexpr std::uint32_t kSquadCountsSchema = 0x80809491U;
constexpr std::uint32_t kSquadSlotCapacity = 8;

/**
 * Finds one decoded field of an object by the schema, ordinal and occurrence the decoder recorded.
 * @return The value, or null when this body did not carry that field at all.
 */
[[nodiscard]] const sense::DecodedValue* sense_field(const sense::DecodedValue* values,
                                                     std::uint32_t count,
                                                     std::uint32_t schema,
                                                     std::uint16_t ordinal,
                                                     std::uint32_t occurrence = 0) noexcept {
    for (std::uint32_t index = 0; index < count; ++index) {
        if (values[index].schemaRow == schema && values[index].fieldOrdinal == ordinal
            && values[index].occurrence == occurrence) {
            return &values[index];
        }
    }
    return nullptr;
}

/** Room for one printed delta field, its sign and its terminator. */
using FieldText = std::array<char, 24>;

/**
 * Formats one field of a Sense delta. Sense reports only what changed, so a field this body did
 * not carry is unchanged, not zero, and prints as a dash.
 * @param value Decoded field, or null when the body did not carry it.
 * @param buffer Caller storage for the printed value.
 * @return Pointer into buffer, always terminated.
 */
[[nodiscard]] const char* sense_text(const sense::DecodedValue* value, FieldText& buffer) noexcept {
    buffer.fill('\0');
    if (value == nullptr || !value->present) {
        buffer[0] = '-';
        return buffer.data();
    }
    const long long printed = value->kind == sense::ValueKind::signedInteger
                                  ? static_cast<long long>(value->signedValue)
                                  : static_cast<long long>(value->unsignedValue);
    if (std::snprintf(buffer.data(), buffer.size(), "%lld", printed) <= 0) {
        buffer.fill('\0');
        buffer[0] = '?';
    }
    return buffer.data();
}

/**
 * Writes one bounded key-value event on the server channel.
 * @param level Severity, checked against the channel threshold before formatting.
 * @param format Printf-style format holding one event line.
 */
void report(core::log::Level level, const char* format, ...) noexcept {
    if (!core::log::accepts(core::log::Channel::server, level)) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (written <= 0) {
        return;
    }
    // vsnprintf reports the length it wanted, so a truncated line reports past the buffer.
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::server, level, {line.data(), length});
}

/** @return The whole payload's bit count, which is the bar a fully framed body reaches. */
[[nodiscard]] std::size_t payload_bits(const message::Request& request) noexcept {
    return request.payload.size() * middleware::encoding::kBitsPerByte;
}

/**
 * Reports one body whose declared framing did not hold.
 * @param stage Short stable stage name for the log line.
 * @param request Validated envelope.
 * @return Always malformed, so the caller can return it directly.
 */
[[nodiscard]] Verdict report_malformed(const char* stage,
                                       const message::Request& request) noexcept {
    report(core::log::Level::warn,
           "ev=activity stage=%s result=malformed type=%u bytes=%zu",
           stage,
           request.messageType,
           request.payload.size());
    return Verdict::malformed;
}

struct PositionGrant {
    std::array<float, 3> position;
    std::uint16_t record;
};

struct LoreOrdinalRange {
    std::uint16_t firstOrdinal;
    std::uint16_t lastOrdinal;
    std::uint16_t firstRecord;
};

constexpr std::string_view kDerelictPackage = "pandora_freeroam";
constexpr std::string_view kMenageriePackage = "caluseum_experience";
constexpr std::string_view kTributeHallPackage = "trophy_hall_freeroam";
constexpr std::string_view kDreamingCityPackage = "dreaming_city_freeroam";
constexpr std::string_view kMoonPackage = "luna_freeroam";
constexpr std::uint32_t kGenericInteractionTarget = 3539U;
constexpr std::array<PositionGrant, 9> kDerelictGrants{{
    {{-40.861F, 147.410F, -2312.313F}, 1571U},   // The Bone
    {{-681.393F, -859.831F, -8.590F}, 1575U},    // The Declaration
    {{2.871F, 236.277F, -2306.442F}, 1569U},     // The Red Box
    {{7.362F, 237.020F, -2306.704F}, 1572U},     // The Kell
    {{9.792F, 149.124F, -2319.657F}, 1570U},     // The Stacks
    {{-205.802F, -80.132F, -11.900F}, 1574U},    // The Gate
    {{-249.759F, 6.664F, -17.853F}, 1573U},      // The Leviathan
    {{-876.144F, -874.570F, 11.781F}, 1576U},    // The Nine
    {{-1323.416F, -534.063F, -298.928F}, 1577U}, // The Witch
}};
constexpr std::array<PositionGrant, 8> kMenagerieGrants{{
    {{30.559F, 31.233F, -2.185F}, 1708U},
    {{57.683F, 8.844F, -43.121F}, 1709U},
    {{61.939F, 220.947F, 2.439F}, 1710U},
    {{143.082F, -23.093F, 11.629F}, 1711U},
    {{109.207F, 211.327F, -144.309F}, 1712U},
    {{403.643F, -5.111F, 6.669F}, 1713U},
    {{947.203F, 2.456F, 131.591F}, 1714U},
    {{1138.881F, 89.454F, 94.136F}, 1715U},
}};
constexpr std::array<PositionGrant, 1> kTributeHallGrants{{
    {{25.642F, 0.012F, 5.922F}, 1716U},
}};

constexpr std::uint16_t kDroneFirstOrdinal = 2455U;
constexpr std::array<std::uint16_t, 16> kDroneRecords{
    740U,
    741U,
    742U,
    744U,
    746U,
    747U,
    748U,
    749U,
    750U,
    751U,
    752U,
    753U,
    754U,
    755U,
    756U,
    757U,
};
constexpr std::array<LoreOrdinalRange, 4> kLoreOrdinalRanges{{
    {2471U, 2493U, 802U},  // Dead Ghosts
    {2494U, 2516U, 778U},  // Awoken crystals
    {2517U, 2532U, 759U},  // Ahamkara bones
    {3310U, 3319U, 1841U}, // Luna's Lost ghosts
}};
constexpr std::uint16_t kMoonGhostFirstOrdinal = 3310U;
constexpr std::uint16_t kMoonGhostLastOrdinal = 3319U;
constexpr std::uint16_t kMoonDestinationLastOrdinal = 3318U;
constexpr std::uint16_t kLunasLostAreFoundFlag = 10698U;

[[nodiscard]] constexpr bool grant_changed(state::lore::GrantOutcome outcome) noexcept {
    return outcome == state::lore::GrantOutcome::granted
           || outcome == state::lore::GrantOutcome::progressed;
}

[[nodiscard]] constexpr bool
progress_changed(state::record_claims::ObjectiveAdvance outcome) noexcept {
    return outcome == state::record_claims::ObjectiveAdvance::advanced
           || outcome == state::record_claims::ObjectiveAdvance::completed;
}

[[nodiscard]] constexpr bool record_resolved(state::lore::GrantOutcome outcome) noexcept {
    return outcome != state::lore::GrantOutcome::recordNotFound
           && outcome != state::lore::GrantOutcome::notAChapter;
}

[[nodiscard]] bool lore_record_for_ordinal(std::uint16_t ordinal, std::uint16_t& record) noexcept {
    if (ordinal >= kDroneFirstOrdinal) {
        const auto index = static_cast<std::size_t>(ordinal - kDroneFirstOrdinal);
        if (index < kDroneRecords.size()) {
            record = kDroneRecords[index];
            return true;
        }
    }
    for (const LoreOrdinalRange& range : kLoreOrdinalRanges) {
        if (ordinal >= range.firstOrdinal && ordinal <= range.lastOrdinal) {
            record = static_cast<std::uint16_t>(range.firstRecord + ordinal - range.firstOrdinal);
            return true;
        }
    }
    return false;
}

/** Resolves the shared generic interaction target by package and measured position. */
void resolve_position_grant(const client::player::position::Snapshot& player,
                            std::string_view package) noexcept {
    constexpr float kRadiusSquared = 36.0F;
    if (!player.present) {
        return;
    }
    std::span<const PositionGrant> grants;
    if (package == kDerelictPackage) {
        grants = kDerelictGrants;
    } else if (package == kMenageriePackage) {
        grants = kMenagerieGrants;
    } else if (package == kTributeHallPackage) {
        grants = kTributeHallGrants;
    } else {
        return;
    }
    for (const PositionGrant& grant : grants) {
        const float dx = player.position[0] - grant.position[0];
        const float dy = player.position[1] - grant.position[1];
        const float dz = player.position[2] - grant.position[2];
        if (dx * dx + dy * dy + dz * dz > kRadiusSquared) {
            continue;
        }
        const auto outcome = state::lore::grant_record(grant.record);
        if (grant_changed(outcome)) {
            bap::arm_account_resync_everywhere();
        }
        return;
    }
}

/** Grants the nine Phantasmal Fragments paid by one completed Lost Ghost search. */
void grant_lost_ghost_reward() noexcept {
    constexpr std::uint32_t kPhantasmalFragmentHash = 443031982U;
    constexpr std::int32_t kRewardQuantity = 9;
    state::build_data::items::Definition definition{};
    if (!state::build_data::find_item_definition_hash(kPhantasmalFragmentHash, definition)
        || !bap::arm_world_profile_item_acquisition(definition.definitionIndex, kRewardQuantity)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=world_reward kind=lost_ghost result=fail");
    }
}

/** Grants one installed world weapon or active-class armour piece from the supplied pool. */
void grant_random_world_loot(std::span<const std::uint32_t> weapons,
                             std::span<const std::uint32_t> titanArmour,
                             std::span<const std::uint32_t> hunterArmour,
                             std::span<const std::uint32_t> warlockArmour) noexcept {
    if (weapons.empty() || titanArmour.size() != hunterArmour.size()
        || titanArmour.size() != warlockArmour.size()) {
        return;
    }
    const state::AccountState account = state::account_snapshot();
    std::span<const std::uint32_t> armour;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (!account.characters[index].selected) {
            continue;
        }
        switch (account.characters[index].characterClass) {
        case state::CharacterClass::hunter:
            armour = hunterArmour;
            break;
        case state::CharacterClass::warlock:
            armour = warlockArmour;
            break;
        case state::CharacterClass::titan:
        default:
            armour = titanArmour;
            break;
        }
        break;
    }
    const std::size_t hashCount = weapons.size() + armour.size();
    std::array<std::byte, sizeof(std::uint32_t)> randomBytes{};
    if (!middleware::crypto::random::fill(randomBytes)) {
        return;
    }
    const std::uint32_t randomValue = middleware::encoding::read_u32_le(randomBytes);
    const std::size_t first = randomValue % hashCount;
    for (std::size_t offset = 0; offset < hashCount; ++offset) {
        const std::size_t index = (first + offset) % hashCount;
        const std::uint32_t hash =
            index < weapons.size() ? weapons[index] : armour[index - weapons.size()];
        state::build_data::items::Definition definition{};
        if (!state::build_data::find_item_definition_hash(hash, definition)) {
            continue;
        }
        if (!bap::arm_world_item_acquisition(definition.definitionIndex)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=world_reward kind=item result=fail");
        }
        return;
    }
}

/** Grants one installed Dreaming City weapon or active-class Reverie Dawn armour piece. */
void grant_random_dreaming_city_loot() noexcept {
    static constexpr std::array<std::uint32_t, 7> kWeapons{
        640114618U,
        334171687U,
        346136302U,
        3242168339U,
        3297863558U,
        3740842661U,
        1644160541U,
    };
    static constexpr std::array<std::uint32_t, 5> kTitanArmour{
        4097166900U,
        2503434573U,
        4070309619U,
        3174233615U,
        1980768298U,
    };
    static constexpr std::array<std::uint32_t, 5> kHunterArmour{
        2824453288U,
        1705856569U,
        1593474975U,
        344548395U,
        3306564654U,
    };
    static constexpr std::array<std::uint32_t, 5> kWarlockArmour{
        185695659U,
        2761343386U,
        2859583726U,
        188778964U,
        3602032567U,
    };
    grant_random_world_loot(kWeapons, kTitanArmour, kHunterArmour, kWarlockArmour);
}

/** Grants one installed Moon weapon or active-class Dreambane armour piece. */
void grant_random_moon_loot() noexcept {
    static constexpr std::array<std::uint32_t, 9> kWeapons{
        2723909519U,
        2931957300U,
        3924212056U,
        1016668089U,
        1645386487U,
        3325778512U,
        4277547616U,
        3870811754U,
        3690523502U,
    };
    static constexpr std::array<std::uint32_t, 5> kTitanArmour{
        925079356U,
        2568538788U,
        3312368889U,
        272413517U,
        310888006U,
    };
    static constexpr std::array<std::uint32_t, 5> kHunterArmour{
        3571441640U,
        883769696U,
        193805725U,
        659922705U,
        377813570U,
    };
    static constexpr std::array<std::uint32_t, 5> kWarlockArmour{
        682780965U,
        3692187003U,
        2048903186U,
        1528483180U,
        1030110631U,
    };
    grant_random_world_loot(kWeapons, kTitanArmour, kHunterArmour, kWarlockArmour);
}

/** Identifies the shared generic target as a Dreaming City cat-statue interaction. */
[[nodiscard]] bool is_dreaming_city_cat(bool definitionFound,
                                        const state::build_data::sobjects::Definition& definition,
                                        std::string_view packageName) noexcept {
    constexpr std::uint32_t kCatNameHash = 0x7A0FD954U;
    constexpr std::uint32_t kCatLane4 = 0x0011FFFFU;
    if (!definitionFound || definition.typeCode != 2 || definition.nameHash != kCatNameHash
        || definition.lane4 != kCatLane4) {
        return false;
    }
    return packageName == kDreamingCityPackage;
}

/** Identifies a Jade Rabbit interaction by its generic target, statue ordinal, and Moon package. */
[[nodiscard]] bool is_moon_rabbit(const message::incident::Incident& incident,
                                  bool primaryFound,
                                  const state::build_data::sobjects::Definition& primary,
                                  std::string_view packageName) noexcept {
    constexpr std::uint32_t kGenericNameHash = 0x7A0FD954U;
    constexpr std::uint32_t kGenericLane4 = 0x0011FFFFU;
    // The nine statues have different target indices and name hashes, but their type-code-2 world
    // object ordinals form one dense run immediately before Luna's Lost ghosts.
    constexpr std::uint16_t kFirstRabbitOrdinal = 3297U;
    constexpr std::uint16_t kLastRabbitOrdinal = 3305U;
    if (!primaryFound || primary.typeCode != 2 || primary.nameHash != kGenericNameHash
        || primary.lane4 != kGenericLane4) {
        return false;
    }

    bool hasRabbitTarget = false;
    for (std::uint32_t index = 0; index < incident.extraTargetCount; ++index) {
        const std::uint32_t target = incident.extraTargets[index];
        state::build_data::sobjects::Definition rabbit{};
        if (!state::build_data::sobjects::find(static_cast<std::uint16_t>(target), rabbit)
            || rabbit.typeCode != 2 || rabbit.recordRow() != 0xFFFFU) {
            continue;
        }
        const std::uint16_t ordinal = rabbit.loreObjectOrdinal();
        hasRabbitTarget = ordinal >= kFirstRabbitOrdinal && ordinal <= kLastRabbitOrdinal;
        if (hasRabbitTarget) {
            break;
        }
    }
    if (!hasRabbitTarget) {
        return false;
    }

    return packageName == kMoonPackage;
}

/** Resolves the egg whose incident carries no per-object identity. */
[[nodiscard]] state::lore::GrantOutcome
resolve_egg_context(const client::player::position::Snapshot& player,
                    std::string_view packageName) noexcept {
    state::build_data::scenarios::Definition layout{};
    const bool hasLayout = state::build_data::find_scenario_layout(packageName, layout);
    const std::string_view stem{layout.spawnStem.data(), layout.spawnStemLength};
    state::build_data::spawn_sets::Point point{};
    float distance = 0.0F;
    const bool hasSpawn =
        player.present && hasLayout
        && state::build_data::find_nearest_spawn_point(stem, player.position, point, distance);

    // This egg reports no identity; its stable nearest spawn distinguishes it across loads.
    constexpr std::uint32_t kDivalianSpawnHash = 0xE3D5F2D5U;
    constexpr float kDivalianSpawnRadius = 16.0F;
    constexpr std::uint16_t kImponentTwoRecord = 40;
    if (player.present && packageName == kDreamingCityPackage && hasSpawn
        && point.nameHash == kDivalianSpawnHash && distance <= kDivalianSpawnRadius) {
        return state::lore::advance_record(kImponentTwoRecord);
    }
    return state::lore::GrantOutcome::recordNotFound;
}

/** Resolves an authored lore target and applies Moon ghost side effects once. */
[[nodiscard]] state::lore::GrantOutcome resolve_lore_target(std::uint32_t target) noexcept {
    state::build_data::sobjects::Definition definition{};
    if (!state::build_data::sobjects::find(static_cast<std::uint16_t>(target), definition)) {
        return state::lore::GrantOutcome::recordNotFound;
    }

    std::uint16_t record = 0;
    std::uint16_t ordinal = 0;
    if (definition.typeCode == 10) {
        record = definition.recordRow();
    } else if (definition.typeCode == 2) {
        ordinal = definition.loreObjectOrdinal();
        if (!lore_record_for_ordinal(ordinal, record)) {
            return state::lore::GrantOutcome::recordNotFound;
        }
    } else {
        return state::lore::GrantOutcome::recordNotFound;
    }

    const auto outcome = state::lore::grant_record(record);
    if (outcome == state::lore::GrantOutcome::granted && ordinal >= kMoonGhostFirstOrdinal
        && ordinal <= kMoonGhostLastOrdinal) {
        if (ordinal <= kMoonDestinationLastOrdinal) {
            (void)state::record_claims::advance_single_objective(kLunasLostAreFoundFlag);
        }
        grant_lost_ghost_reward();
        constexpr std::int32_t kBaseExperienceReward = 2500;
        const bool queued = bap::arm_seasonal_experience_presentation(kBaseExperienceReward);
        if (!queued) {
            (void)state::progression::seasonal_experience::grant(kBaseExperienceReward);
        }
    }
    return outcome;
}

/** Applies collectible and gift side effects only after the incident's complete outer body passed. */
void apply_incident_grants(const message::incident::Incident& incident) noexcept {
    // Preserve the common-header size gate before acting on the incident.
    if (incident.payloadLength < 13) {
        return;
    }

    // Resolve exact identity first; location is reserved for objects carrying no identity.
    constexpr std::uint32_t kCorruptedEggTarget = 693U;
    constexpr std::uint32_t kCorruptedEggNameHash = 0x179A5E15U;
    constexpr std::uint32_t kCorruptedEggLane4 = 0x0A06FFFFU;
    state::build_data::sobjects::Definition primary{};
    const bool primaryFound = state::build_data::sobjects::find(
        static_cast<std::uint16_t>(incident.primaryTarget), primary);
    const bool isCorruptedEgg =
        incident.primaryTarget == kCorruptedEggTarget && primaryFound && primary.typeCode == 3
        && primary.nameHash == kCorruptedEggNameHash && primary.lane4 == kCorruptedEggLane4;

    std::uint32_t loreTarget = incident.primaryTarget;
    state::lore::GrantOutcome lore = resolve_lore_target(loreTarget);
    if (!record_resolved(lore)) {
        for (std::uint32_t index = 0; index < incident.extraTargetCount; ++index) {
            loreTarget = incident.extraTargets[index];
            lore = resolve_lore_target(loreTarget);
            if (record_resolved(lore)) {
                break;
            }
        }
    }

    report(core::log::Level::info,
           "ev=activity stage=incident_lore target=%u selected=%u extra=%u first=%u second=%u "
           "third=%u fourth=%u outcome=%u",
           incident.primaryTarget,
           loreTarget,
           incident.extraTargetCount,
           incident.extraTargetCount > 0 ? incident.extraTargets[0] : 0U,
           incident.extraTargetCount > 1 ? incident.extraTargets[1] : 0U,
           incident.extraTargetCount > 2 ? incident.extraTargets[2] : 0U,
           incident.extraTargetCount > 3 ? incident.extraTargets[3] : 0U,
           static_cast<unsigned>(lore));

    if (grant_changed(lore)) {
        bap::arm_account_resync_everywhere();
    }
    if (record_resolved(lore)) {
        if (isCorruptedEgg) {
            grant_random_dreaming_city_loot();
        }
        return;
    }
    if (!isCorruptedEgg && incident.primaryTarget != kGenericInteractionTarget) {
        return;
    }

    const auto player = client::player::position::snapshot();
    // Incidents can arrive through the private activity while their object belongs to the public
    // region. The pre-merge grant path intentionally followed the instantiated region instead.
    const std::uint64_t destinationSession =
        state::activity::membership::live_region_session(state::activity::kAbsentSessionId);
    state::activity::destination::DestinationSelection destination{};
    static_cast<void>(state::activity::destination::snapshot(destinationSession, destination));
    const std::size_t packageLength =
        (std::min)(static_cast<std::size_t>(destination.packageNameLength),
                   destination.packageName.size());
    const std::string_view packageName(
        reinterpret_cast<const char*>(destination.packageName.data()), packageLength);

    report(core::log::Level::info,
           "ev=activity stage=incident_grant target=%u extra=%u lore=%u type=%d lane=0x%08X "
           "egg=%u destination=0x%016llX package=%.*s player=%u",
           incident.primaryTarget,
           incident.extraTargetCount,
           static_cast<unsigned>(lore),
           primaryFound ? primary.typeCode : state::build_data::sobjects::kAbsentTypeCode,
           primaryFound ? primary.lane4 : 0U,
           static_cast<unsigned>(isCorruptedEgg),
           static_cast<unsigned long long>(destinationSession),
           static_cast<int>(packageName.size()),
           packageName.data(),
           static_cast<unsigned>(player.present));

    if (isCorruptedEgg) {
        const auto egg = resolve_egg_context(player, packageName);
        grant_random_dreaming_city_loot();
        if (grant_changed(egg)) {
            bap::arm_account_resync_everywhere();
        }
    } else if (is_dreaming_city_cat(primaryFound, primary, packageName)) {
        grant_random_dreaming_city_loot();
        constexpr std::uint16_t kRememberYourMannersFlag = 9448U;
        if (progress_changed(
                state::record_claims::advance_single_objective(kRememberYourMannersFlag))) {
            bap::arm_account_resync_everywhere();
        }
    } else if (is_moon_rabbit(incident, primaryFound, primary, packageName)) {
        grant_random_moon_loot();
        constexpr std::uint16_t kLetThemEatRiceCakesFlag = 10696U;
        if (progress_changed(
                state::record_claims::advance_single_objective(kLetThemEatRiceCakesFlag))) {
            bap::arm_account_resync_everywhere();
        }
    } else if (incident.primaryTarget == kGenericInteractionTarget) {
        resolve_position_grant(player, packageName);
    }
}

} // namespace

/** Records the synchronous schema-selected sensor decode. */
Framed frame_sense_update(const message::Request& request,
                          const message::sense_update::SenseUpdate& update) noexcept {
    if (update.decoded.status == sense::DecodeStatus::malformed) {
        return {report_malformed("sense", request), update.decoded.bitsConsumed};
    }
    report(core::log::Level::debug,
           "ev=activity stage=sense result=%s epoch=0x%016llX%016llX groups=%u "
           "decoded_groups=%u skipped_groups=%u objects=%u decoded_objects=%u",
           sense::decode_status_name(update.decoded.status),
           static_cast<unsigned long long>(update.epoch.first),
           static_cast<unsigned long long>(update.epoch.second),
           update.decoded.groupsSeen,
           update.decoded.groupsDecoded,
           update.decoded.groupsSkipped,
           update.decoded.objectsSeen,
           update.decoded.objectsDecoded);
    for (std::size_t index = 0; index < update.decoded.objectCount; ++index) {
        const sense::DecodedObject& object = update.decoded.objects[index];
        report(core::log::Level::debug,
               "ev=activity stage=sense_object result=%s row=%zu key=0x%08X object=0x%08X "
               "object_row=%u slot_row=%u slot_type=%u slot_index=%u schema=0x%08X "
               "schema_row=%u generation=%u values=%u delta_bits=%u",
               sense::object_status_name(object.status),
               index,
               object.registryKey,
               object.objectTag,
               object.objectRow,
               object.slotRow,
               static_cast<unsigned>(object.slotType),
               static_cast<unsigned>(object.slotIndex),
               object.senseSchema,
               object.schemaRow,
               object.generationPlusOne,
               object.valueCount,
               object.deltaBits);
        if (object.status == sense::ObjectStatus::decoded && object.senseSchema == kSquadSenseSchema
            && object.firstValue <= update.decoded.valueCount) {
            const sense::DecodedValue* const own = update.decoded.values.data() + object.firstValue;
            const std::uint32_t count = object.valueCount;
            std::array<char, 64> members{};
            int offset = 0;
            for (std::uint32_t slot = 0; slot < kSquadSlotCapacity; ++slot) {
                const sense::DecodedValue* const per =
                    sense_field(own, count, kSquadCountsSchema, 0, slot);
                if (per == nullptr) {
                    break;
                }
                FieldText slotText{};
                const int wrote = std::snprintf(members.data() + offset,
                                                members.size() - static_cast<std::size_t>(offset),
                                                offset == 0 ? "%s" : ",%s",
                                                sense_text(per, slotText));
                if (wrote <= 0 || static_cast<std::size_t>(offset + wrote) >= members.size()) {
                    break;
                }
                offset += wrote;
            }
            FieldText spawnText{};
            FieldText aliveText{};
            FieldText stateText{};
            FieldText flag0Text{};
            FieldText flag3Text{};
            FieldText slotsText{};
            report(core::log::Level::debug,
                   "ev=activity stage=squad_sense result=decoded key=0x%08X index=%u "
                   "spawn_gen=%s alive=%s state=%s flag0=%s flag3=%s slots=%s members=%s",
                   object.registryKey,
                   static_cast<unsigned>(object.slotIndex),
                   sense_text(sense_field(own, count, kSquadSenseSchema, 0), spawnText),
                   sense_text(sense_field(own, count, kSquadSenseSchema, 3), aliveText),
                   sense_text(sense_field(own, count, kSquadSenseSchema, 7), stateText),
                   sense_text(sense_field(own, count, kSquadSenseSchema, 8), flag0Text),
                   sense_text(sense_field(own, count, kSquadSenseSchema, 9), flag3Text),
                   sense_text(sense_field(own, count, kSquadSlotsSchema, 0), slotsText),
                   members.data());
        }
        if (object.status == sense::ObjectStatus::decoded
            && object.senseSchema == kCombatantSenseSchema
            && object.firstValue <= update.decoded.valueCount) {
            const sense::DecodedValue* const own = update.decoded.values.data() + object.firstValue;
            const std::uint32_t count = object.valueCount;
            FieldText spawnText{};
            FieldText laneText{};
            FieldText generationText{};
            FieldText tickText{};
            FieldText busyText{};
            FieldText subscriptionText{};
            FieldText dependencyText{};
            FieldText detachedText{};
            FieldText snapshotText{};
            report(core::log::Level::debug,
                   "ev=activity stage=combatant_sense result=decoded key=0x%08X index=%u "
                   "spawn_rev=%s atom_lane=%s atom_gen=%s atom_tick=%s busy=%s "
                   "subscription=%s dependency=%s detached=%s snapshot=%s",
                   object.registryKey,
                   static_cast<unsigned>(object.slotIndex),
                   sense_text(sense_field(own, count, kCombatantSenseSchema, 0), spawnText),
                   sense_text(sense_field(own, count, kCombatantAtomsSchema, 0), laneText),
                   sense_text(sense_field(own, count, kCombatantAtomsSchema, 1), generationText),
                   sense_text(sense_field(own, count, kCombatantAtomsSchema, 2), tickText),
                   sense_text(sense_field(own, count, kCombatantAtomsSchema, 3), busyText),
                   sense_text(sense_field(own, count, kCombatantSenseSchema, 5), subscriptionText),
                   sense_text(sense_field(own, count, kCombatantSenseSchema, 6), dependencyText),
                   sense_text(sense_field(own, count, kCombatantSenseSchema, 10), detachedText),
                   sense_text(sense_field(own, count, kCombatantSenseSchema, 11), snapshotText));
        }
        if (object.status != sense::ObjectStatus::decoded
            || object.senseSchema != kObjectSenseSchema
            || object.valueCount != kObjectSenseValueCount
            || object.firstValue > update.decoded.valueCount
            || kObjectSenseValueCount > update.decoded.valueCount - object.firstValue) {
            continue;
        }
        const sense::DecodedValue* const values = update.decoded.values.data() + object.firstValue;
        report(core::log::Level::debug,
               "ev=activity stage=object_sense result=decoded key=0x%08X index=%u "
               "auth_gen=%lld live=%llu spawned=%llu entry=%lld mask0=0x%08llX "
               "mask1=0x%08llX replies=%llu",
               object.registryKey,
               static_cast<unsigned>(object.slotIndex),
               static_cast<long long>(values[0].signedValue),
               static_cast<unsigned long long>(values[1].unsignedValue),
               static_cast<unsigned long long>(values[2].unsignedValue),
               static_cast<long long>(values[3].signedValue),
               static_cast<unsigned long long>(values[4].unsignedValue),
               static_cast<unsigned long long>(values[5].unsignedValue),
               static_cast<unsigned long long>(values[6].unsignedValue));
    }
    const Verdict verdict =
        update.decoded.status == sense::DecodeStatus::complete ? Verdict::framed : Verdict::partial;
    return {verdict, update.decoded.bitsConsumed};
}

/** Records a service-8 envelope carrying the local-only activity-host request type. */
Framed frame_route_misuse(const message::Request& request) noexcept {
    // This type is a client-local message the transport turns into its own service. Arriving here
    // it is an authenticated but invalid route use, and answering it would allocate a second
    // session for one the client already has.
    report(core::log::Level::warn,
           "ev=activity stage=route result=misuse type=%u bytes=%zu",
           request.messageType,
           request.payload.size());
    return {Verdict::quarantined, 0};
}

/** Frames a start-new-activity request without applying any transition policy to it. */
Framed frame_start_activity(const message::Request& request) noexcept {
    namespace start = message::start_activity;
    start::StartActivity parsed{};
    std::size_t consumed = 0;
    if (!start::parse_start_activity(request.payload, parsed, consumed)) {
        return {report_malformed("start_activity", request), consumed};
    }
    return {parsed.tailBits == 0 ? Verdict::framed : Verdict::partial, consumed};
}

/** Frames a complete peer-reservation request without reserving a row. */
Framed frame_reservation_request(const message::Request& request) noexcept {
    telemetry::ReservationRequest parsed{};
    std::size_t consumed = 0;
    if (!telemetry::parse_reservation_request(request.payload, parsed, consumed)) {
        return {report_malformed("reservation", request), consumed};
    }
    report(core::log::Level::debug,
           "ev=activity stage=reservation result=read revision=%u records=%u",
           parsed.peerTableEpoch,
           static_cast<unsigned>(parsed.recordCount));
    return {Verdict::framed, consumed};
}

/** Frames a reservation release. */
Framed frame_reservation_release(const message::Request& request) noexcept {
    ledger::ReservationRelease release{};
    std::size_t consumed = 0;
    if (!ledger::parse_release(request.payload, release, consumed)) {
        return {report_malformed("reservation_release", request), consumed};
    }
    return {Verdict::framed, consumed};
}

/** Frames a peer leave notice. */
Framed frame_peer_leave(const message::Request& request) noexcept {
    ledger::PeerLeave leave{};
    std::size_t consumed = 0;
    if (!ledger::parse_leave(request.payload, leave, consumed)) {
        return {report_malformed("peer_leave", request), consumed};
    }
    report(core::log::Level::info,
           "ev=activity stage=peer_leave result=read peer=0x%016llX",
           static_cast<unsigned long long>(leave.ownMemberKey));
    return {Verdict::framed, consumed};
}

/** Records a debug command without reading or running it. */
Framed frame_debug_command(const message::Request& request) noexcept {
    // The nested command definition is runtime selected, so the body cannot be walked from the
    // outer root alone. It is never executed, dispatched, or sent on to another client.
    report(core::log::Level::warn,
           "ev=activity stage=debug_command result=quarantined bytes=%zu",
           request.payload.size());
    return {Verdict::quarantined, 0};
}

/** Frames a connectivity failure report. */
Framed frame_connectivity_failure(const message::Request& request) noexcept {
    ledger::ConnectivityFailure failure{};
    std::size_t consumed = 0;
    if (!ledger::parse_connectivity_failure(request.payload, failure, consumed)) {
        return {report_malformed("connectivity", request), consumed};
    }
    report(core::log::Level::info,
           "ev=activity stage=connectivity result=read peer=0x%016llX reason=%d",
           static_cast<unsigned long long>(failure.peerKey),
           static_cast<int>(failure.failureReason));
    // The two bits are the last schema field; the rest of the ninth byte is padding.
    return {Verdict::framed, consumed};
}

/** Records a client heartbeat as a bounded body. */
Framed frame_heartbeat(const message::Request&) noexcept {
    // One runtime-selected nested definition, so the declared service length is the only bound.
    return {Verdict::partial, 0};
}

/** Frames a complete lag-switch report without applying network policy. */
Framed frame_lag_switch(const message::Request& request) noexcept {
    telemetry::LagSwitchReport parsed{};
    std::size_t consumed = 0;
    if (!telemetry::parse_lag_switch(request.payload, parsed, consumed)) {
        return {report_malformed("lag_switch", request), consumed};
    }
    report(core::log::Level::debug,
           "ev=activity stage=lag_switch result=read records=%u",
           static_cast<unsigned>(parsed.recordCount));
    return {Verdict::framed, consumed};
}

/** Frames a complete connection-quality report without applying network policy. */
Framed frame_connection_quality(const message::Request& request) noexcept {
    telemetry::ConnectionQualityReport parsed{};
    std::size_t consumed = 0;
    if (!telemetry::parse_connection_quality(request.payload, parsed, consumed)) {
        return {report_malformed("connection_quality", request), consumed};
    }
    report(core::log::Level::debug,
           "ev=activity stage=connection_quality result=read records=%u",
           static_cast<unsigned>(parsed.recordCount));
    return {Verdict::framed, consumed};
}

/** Frames a speculative migration proposal without acting on it. */
Framed frame_migration(const message::Request& request) noexcept {
    ledger::MigrationProposal proposal{};
    std::size_t consumed = 0;
    if (!ledger::parse_migration(request.payload, proposal, consumed)) {
        return {report_malformed("migration", request), consumed};
    }
    // Host ownership never moves from a proposal. Acting on one needs the group migration state
    // machine, and a host that answers without it can split the session in two.
    report(core::log::Level::info,
           "ev=activity stage=migration result=noted peer=0x%016llX bubble=%d",
           static_cast<unsigned long long>(proposal.memberKey),
           proposal.bubbleIndex);
    return {Verdict::framed, consumed};
}

/** Frames the fixed high-water telemetry block. */
Framed frame_high_water(const message::Request& request) noexcept {
    telemetry::HighWater block{};
    std::size_t consumed = 0;
    if (!telemetry::parse_high_water(request.payload, block, consumed)) {
        return {report_malformed("high_water", request), consumed};
    }
    return {Verdict::framed, consumed};
}

/** Frames one of the two opaque scalar messages. */
Framed frame_opaque_scalar(const message::Request& request) noexcept {
    std::int32_t value = 0;
    std::size_t consumed = 0;
    if (!telemetry::parse_opaque_scalar(request.payload, value, consumed)) {
        return {report_malformed("scalar", request), consumed};
    }
    return {Verdict::framed, consumed};
}

/** Frames the one-byte activity keepalive. */
Framed frame_client_keepalive(const message::Request& request) noexcept {
    namespace keepalive = message::client_keepalive;
    if (!keepalive::validate_client_keepalive(request.payload)) {
        return {report_malformed("keepalive", request), 0};
    }
    // The single byte is uninitialized at the sender, so it carries no value to read.
    const std::size_t consumed = payload_bits(request);
    return {Verdict::framed, consumed};
}

/** Frames one incident, optionally returning its parsed outer fields. */
static Framed frame_incident_impl(const message::Request& request,
                                  message::incident::Incident* output) noexcept {
    namespace incident = message::incident;
    incident::Incident parsed{};
    const incident::Verdict verdict = incident::validate(request.payload, parsed);
    const bool accepted = verdict == incident::Verdict::accepted;
    if (!accepted) {
        report(core::log::Level::warn,
               "ev=activity stage=incident result=%s target=%u extra=%u selector=%u "
               "optional=%u payload=%u",
               incident::verdict_name(verdict),
               parsed.primaryTarget,
               parsed.extraTargetCount,
               parsed.selectorLength,
               static_cast<unsigned>(parsed.hasOptionalBlock),
               parsed.payloadLength);
        // A refused target index would index the consumer's table unbounded, so the body is kept
        // and never relayed.
        const Verdict outcome = verdict == incident::Verdict::targetPoisoned
                                        || verdict == incident::Verdict::targetOutOfRange
                                    ? Verdict::quarantined
                                    : Verdict::malformed;
        return {outcome, parsed.consumedBits};
    }
    if (output != nullptr) {
        *output = parsed;
    }
    apply_incident_grants(parsed);
    report(core::log::Level::debug,
           "ev=activity stage=incident result=accepted target=%u extra=%u selector=%u "
           "optional=%u payload=%u",
           parsed.primaryTarget,
           parsed.extraTargetCount,
           parsed.selectorLength,
           static_cast<unsigned>(parsed.hasOptionalBlock),
           parsed.payloadLength);
    return {Verdict::framed, parsed.consumedBits};
}

/** Frames one incident and quarantines a poison target. */
Framed frame_incident(const message::Request& request) noexcept {
    return frame_incident_impl(request, nullptr);
}

/** Frames one incident and returns its parsed outer fields. */
Framed frame_incident_copy(const message::Request& request,
                           message::incident::Incident& incident) noexcept {
    incident = {};
    return frame_incident_impl(request, &incident);
}

/** Frames one authority release, which records authority and returns no lease. */
Framed frame_authority_release(const message::Request& request, bool expectReason) noexcept {
    authority::Release decoded{};
    const bool parsed = expectReason ? authority::parse_abandon(request.payload, decoded)
                                     : authority::parse_abdicate(request.payload, decoded);
    if (!parsed) {
        return {report_malformed("authority", request), 0};
    }
    // The mask says how much lease the client believes it is handing back with the bubble. A
    // release Sunrise records as authority-only while the client counts it as slots returned is
    // how the two ledgers drift apart, and nothing else on this path reports the size.
    std::size_t returning = 0;
    for (const std::byte byte : decoded.mask) {
        returning += static_cast<std::size_t>(std::popcount(std::to_integer<unsigned char>(byte)));
    }
    report(core::log::Level::debug,
           "ev=activity stage=authority result=noted type=%u selector=%u reason=%d slots=%zu",
           request.messageType,
           static_cast<unsigned>(decoded.selector),
           decoded.hasReason ? decoded.reason : 0,
           returning);
    return {Verdict::framed, payload_bits(request)};
}

/** Frames one purge request. Nothing answers it. */
Framed frame_request_purge(const message::Request& request) noexcept {
    authority::PurgeRequest decoded{};
    if (!authority::parse_request_purge(request.payload, decoded)) {
        return {report_malformed("purge", request), 0};
    }
    // The answer would have to name the exact next authority generation, which nothing here
    // tracks, and the consumer asserts on any other value.
    report(core::log::Level::debug,
           "ev=activity stage=purge result=noted reason=%d mask_bytes=%zu",
           decoded.reason,
           decoded.mask.size());
    return {Verdict::framed, payload_bits(request)};
}

/** Frames one authority query answer. */
Framed frame_query_answer(const message::Request& request) noexcept {
    authority::QueryAnswer answer{};
    if (!authority::parse_query_answer(request.messageType, request.payload, answer)) {
        return {report_malformed("authority_answer", request), 0};
    }
    report(core::log::Level::debug,
           "ev=activity stage=authority result=answer type=%u corr=0x%08X selector=%d",
           request.messageType,
           static_cast<std::uint32_t>(answer.correlation),
           answer.hasSelector ? static_cast<int>(answer.selector) : -1);
    return {Verdict::framed, payload_bits(request)};
}

/** Records an envelope whose message type has no known body grammar. */
Framed frame_unknown(const message::Request& request) noexcept {
    report(core::log::Level::warn,
           "ev=activity stage=unknown result=bounded type=%u bytes=%zu",
           request.messageType,
           request.payload.size());
    return {Verdict::partial, 0};
}

} // namespace sunrise::server::bap::encrypted::activity_message::receipts
