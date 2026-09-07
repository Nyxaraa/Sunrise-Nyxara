#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace sunrise::middleware::bap::activity_message::auth_schema_catalog {

/** Facts read from one complete ClientRef Auth table. */
struct Type final {
    std::string_view name{};
    std::uint32_t schema{};
    std::uint16_t structBytes{};
    std::uint16_t minimumBits{};
    std::uint16_t maximumBits{};
    std::uint16_t componentOffset{};
    std::uint8_t slotType{};
    bool hasContiguousMirror{};
    bool hasDynamicBody{};
    bool writable{};
};

/** Sentinel used when an applier does not keep one contiguous Auth mirror. */
inline constexpr std::uint16_t kNoComponentOffset = 0xFFFFU;

/**
 * The complete Auth type census. `writable` excludes only type 67, whose applier replaces the
 * process-wide faction-allegiance matrix rather than the selected slot.
 */
inline constexpr std::array<Type, 39> kTypes = {{
    {"squad_sensor", 0x80807EC9U, 196, 24, 1313, 384, 1, true, false, true},
    {"combatant_sensor", 0x80807DA1U, 2392, 11, 8228, 384, 2, true, true, true},
    {"objective_sensor", 0x80807F0CU, 28, 2, 225, kNoComponentOffset, 3, false, false, true},
    {"object_sensor", 0x8080992FU, 368, 252, 525, 384, 4, true, true, true},
    {"sequence_sensor", 0x80804F04U, 1072, 7359, 7359, 384, 5, true, false, true},
    {"cinematic_sensor", 0x80804F08U, 224, 263, 1671, 384, 6, true, false, true},
    {"hud_sensor", 0x80804F10U, 464, 35, 3582, 384, 8, true, false, true},
    {"music_sensor", 0x80804F58U, 1048, 7223, 7223, 384, 11, true, false, true},
    {"player_spasaha_sensor", 0x80804F30U, 760, 128, 4800, 496, 13, true, false, true},
    {"scoreboard_sensor", 0x808099F9U, 5904, 7, 47054, 384, 16, true, false, true},
    {"activity_lifetime_sensor", 0x8080991AU, 1300, 423, 2265, 384, 17, true, false, true},
    {"activity_timer_sensor", 0x80809919U, 64, 386, 386, 384, 18, true, false, true},
    {"player_navigation_sensor", 0x80809527U, 7308, 62, 53150, 384, 19, true, false, true},
    {"damage_component_sensor", 0x80809563U, 12, 87, 87, 384, 20, true, false, true},
    {"distance_sensor", 0x80809502U, 276, 98, 1868, 384, 21, true, false, true},
    {"device_sensor", 0x80804F48U, 24, 147, 147, 448, 23, true, false, true},
    {"channel_sensor", 0x80804F40U, 52, 3, 387, 384, 24, true, false, true},
    {"look_trigger_sensor", 0x80804ED3U, 16, 65, 129, 400, 25, true, false, true},
    {"hop_on_sensor", 0x8080954BU, 112, 186, 276, 384, 26, true, true, true},
    {"object_monitor_sensor", 0x808094EAU, 12, 87, 87, 384, 29, true, false, true},
    {"player_monitor_sensor", 0x80809532U, 12, 87, 87, 384, 30, true, false, true},
    {"player_trigger_sensor", 0x80809524U, 24, 129, 129, 392, 31, true, false, true},
    {"toggle_sensor", 0x8080955AU, 12, 57, 57, 384, 32, true, false, true},
    {"object_filter_sensor", 0x8080956AU, 400, 4, 732, 384, 34, true, true, true},
    {"activity_hard_wipe_globals_sensor", 0x808099BFU, 64, 359, 359, 384, 35, true, false, true},
    {"loot_sensor", 0x80804EFEU, 16, 88, 88, 528, 36, true, false, true},
    {"map_generator_sensor", 0x80805007U, 1460, 1750, 11350, 384, 37, true, false, true},
    {"task_sensor", 0x80807D89U, 4, 32, 32, 384, 38, true, false, true},
    {"passenger_sensor", 0x80804EE5U, 12, 86, 86, kNoComponentOffset, 39, false, false, true},
    {"target_sensor", 0x8080955FU, 8, 55, 55, 384, 40, true, false, true},
    {"new_user_experience_sensor", 0x80804EE9U, 2120, 12, 9948, 388, 41, true, false, true},
    {"performance_sensor", 0x80809586U, 48, 2, 357, 384, 42, true, false, true},
    {"scene_sensor", 0x8080626BU, 212, 74, 1538, kNoComponentOffset, 43, false, false, true},
    {"dialog_sensor", 0x80804F77U, 4104, 19767, 27959, 384, 53, true, false, true},
    {"ghost_link_sensor", 0x80804D3FU, 12, 65, 65, 448, 65, true, false, true},
    {"team_side_sensor", 0x808090AAU, 136, 1057, 1057, 384, 67, true, false, false},
    {"directive_sensor", 0x80804F67U, 768, 4802, 4802, 384, 68, true, false, true},
    {"encounter_engagement_sensor", 0x808094F1U, 208, 23, 1496, 384, 70, true, false, true},
    {"public_event_sensor", 0x80804F57U, 24, 183, 183, 384, 71, true, false, true},
}};

/** Resolves only the exact slot-type and schema pair. */
[[nodiscard]] constexpr const Type* find(std::uint8_t slotType, std::uint32_t schema) noexcept {
    for (const Type& type : kTypes) {
        if (type.slotType == slotType && type.schema == schema) {
            return &type;
        }
    }
    return nullptr;
}

} // namespace sunrise::middleware::bap::activity_message::auth_schema_catalog
