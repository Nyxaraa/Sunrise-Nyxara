#include "../../../middleware/bap/activity_message/music_section_auth.h"
#include "../../../middleware/bap/activity_message/mission_effect_auth.h"
#include "../../../middleware/bap/activity_message/darkness_zone_auth.h"
#include "../../../middleware/bap/activity_message/squad_objective_auth.h"
#include "../../../middleware/bap/activity_message/combatant_path_auth.h"
#include "../../../middleware/bap/activity_message/combatant_action_auth.h"
#include "../../../middleware/bap/activity_message/combatant_delivery_auth.h"
#include "../../../middleware/bap/activity_message/combatant_retire_auth.h"
#include "../../../middleware/bap/activity_message/ghost_link_auth.h"
#include "../../../middleware/bap/activity_message/interactable_object_auth.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../../middleware/bap/activity_message/auth_schema_catalog.h"
#include "../../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../../middleware/encoding/bit_writer.h"
#include "../../../state/activity_sdk/format.h"
#include "../../../state/activity_sdk/runtime.h"
#include "mission_script_lua_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

namespace format = state::activity_sdk::format;
namespace slot_transport = middleware::bap::activity_message::sensor_auth_update;
namespace scriptable_auth = middleware::bap::activity_message::scriptable_auth;
namespace auth_catalog = middleware::bap::activity_message::auth_schema_catalog;

namespace {
/** @return True when one live Slot row is an exact type-23 device. */
[[nodiscard]] bool exact_device_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kDeviceSlotType
           && definition.componentClass == format::kDeviceComponentClass
           && definition.senseSchema == format::kDeviceSenseSchema
           && definition.authSchema == format::kDeviceAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-4 authored object. */
[[nodiscard]] bool exact_object_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kObjectSlotType
           && definition.componentClass == format::kObjectComponentClass
           && definition.senseSchema == format::kObjectSenseSchema
           && definition.authSchema == format::kObjectAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-31 configured trigger. */
[[nodiscard]] bool exact_trigger_slot(const SlotDefinition& definition) noexcept {
    namespace auth = middleware::bap::activity_message::scriptable_auth;
    return definition.slotType == auth::kType31SlotType
           && definition.authSchema == auth::kType31Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_sequence_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kSequenceSlotType
           && definition.componentClass == format::kSequenceComponentClass
           && definition.authSchema == format::kSequenceAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_cinematic_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kCinematicSlotType
           && definition.componentClass == format::kCinematicComponentClass
           && definition.authSchema == format::kCinematicAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-3 objective reset control. */
[[nodiscard]] bool exact_objective_reset_slot(const SlotDefinition& definition) noexcept {
    namespace auth = middleware::bap::activity_message::scriptable_auth;
    return definition.slotType == auth::kType3SlotType
           && definition.authSchema == auth::kType3Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_task_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kTaskSlotType
           && definition.componentClass == format::kTaskComponentClass
           && definition.authSchema == format::kTaskAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

[[nodiscard]] bool exact_dialogue_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kDialogueSlotType
           && definition.componentClass == format::kDialogueComponentClass
           && definition.authSchema == format::kDialogueAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0
           && (definition.flags & format::kSlotDialogueCuesExact) != 0;
}

/** @return True when one live Slot row is an exact type-30 occupancy condition. */
[[nodiscard]] bool exact_occupancy_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == format::kOccupancySlotType
           && definition.componentClass == format::kOccupancyComponentClass
           && definition.senseSchema == format::kOccupancySenseSchema
           && definition.authSchema == format::kOccupancyAuthSchema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-68 HUD directive state. */
[[nodiscard]] bool exact_directive_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType68SlotType
           && definition.authSchema == scriptable_auth::kType68Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-70 engagement observer. */
[[nodiscard]] bool exact_engagement_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType70SlotType
           && definition.authSchema == scriptable_auth::kType70Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is the exact type-71 public-event sensor. */
[[nodiscard]] bool exact_public_event_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType71SlotType
           && definition.authSchema == scriptable_auth::kType71Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-42 performance sensor. */
[[nodiscard]] bool exact_performance_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType42SlotType
           && definition.componentClass == scriptable_auth::kType42ComponentClass
           && definition.authSchema == scriptable_auth::kType42Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** @return True when one live Slot row is an exact type-2 combatant. */
[[nodiscard]] bool exact_combatant_slot(const SlotDefinition& definition) noexcept {
    return definition.slotType == scriptable_auth::kType2SlotType
           && definition.componentClass == scriptable_auth::kType2ComponentClass
           && definition.senseSchema == scriptable_auth::kType2SenseSchema
           && definition.authSchema == scriptable_auth::kType2Schema
           && (definition.flags & format::kSlotSchemaJoinExact) != 0;
}

/** Stages one authored native Auth body on the guarded message-5 route. */
[[nodiscard]] int queue_slot_auth(lua_State* state,
                                  const SlotDefinition& slot,
                                  std::uint32_t schema,
                                  std::size_t bitCount,
                                  std::span<const std::byte> body) {
    Impl* const impl = impl_from_state(state);
    std::array<std::byte, 32> sdkBuildSha256{};
    if (!decode_sdk_build_sha256(std::string_view(impl->identity.sdkBuildId.data()),
                                 sdkBuildSha256)) {
        return luaL_error(state, "loaded SDK generation identity is invalid");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::applySlotAuth;
    intent.sdkBuildSha256 = sdkBuildSha256;
    intent.firstRow = slot.nativeRow;
    intent.objectTag = slot.objectTag;
    intent.registryKey = slot.registryKey;
    intent.authSchema = schema;
    intent.authBitCount = static_cast<std::uint16_t>(bitCount);
    intent.slotIndex = static_cast<std::uint16_t>(slot.slotIndex);
    intent.slotType = static_cast<std::uint8_t>(slot.slotType);
    intent.authByteCount = static_cast<std::uint16_t>(body.size());
    try {
        intent.authBody.assign(body.begin(), body.end());
    } catch (const std::bad_alloc&) {
        return luaL_error(state, "mission Auth body allocation failed");
    }
    return queue_intent(state, frame, intent);
}

/** The occupancy Auth body is a fixed 87 bits: a 55-bit client reference then one int32. */
constexpr std::size_t kOccupancyAuthBitCount = 87;
constexpr std::size_t kOccupancyAuthByteCount = 11;
} // namespace

/** Sets the authored object filter and caller value carried by one type-30 condition. */
[[nodiscard]] int slot_set_occupancy_condition(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 2> kDeclared{"filter", "value"};
    refuse_unknown_arguments(state, kDeclared);
    const SlotHandle reference = checked_argument<SlotHandle>(state, "filter", kSlotMetatable);
    const lua_Integer value = checked_integer_argument(state, "value");
    SlotDefinition slot{};
    SlotDefinition playerSet{};
    if (!current_slot(state, *handle, slot) || !current_slot(state, reference, playerSet)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_occupancy_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-30 occupancy condition");
    }
    // The Auth field is a full-range int32 and lua_Integer is wider, so the lane is still checked.
    if (value < (std::numeric_limits<std::int32_t>::min)()
        || value > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "value must be a 32-bit signed integer");
    }
    std::array<std::byte, kOccupancyAuthByteCount> body{};
    middleware::encoding::bits::Writer writer(body);
    const std::uint32_t encodedValue =
        std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(value)) + 0x80000000U;
    if (!writer.write(playerSet.registryKey, 32)
        || !writer.write(static_cast<std::uint32_t>(playerSet.slotType) + 1U, 7)
        || !writer.write(static_cast<std::uint32_t>(playerSet.slotIndex) + 32768U, 16)
        || !writer.write(encodedValue, 32) || writer.bit_count() != kOccupancyAuthBitCount) {
        return luaL_error(state, "occupancy condition native encoder failed");
    }
    return queue_slot_auth(state, slot, format::kOccupancyAuthSchema, kOccupancyAuthBitCount, body);
}

/** Reads one integer field from a generated directive declaration. */
[[nodiscard]] lua_Integer directive_integer(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    const lua_Integer value = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return value;
}

/** Shows one generated directive through the exact type-68 Auth schema. */
[[nodiscard]] int slot_set_directive(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 4> kDeclared{"directive", "state", "navpoint", "audience"};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_directive_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-68 directive state");
    }
    lua_getfield(state, 2, "directive");
    luaL_checktype(state, -1, LUA_TTABLE);
    const lua_Integer slotRow = directive_integer(state, -1, "slot_row");
    const lua_Integer nameHash = directive_integer(state, -1, "name_hash");
    const lua_Integer element = directive_integer(state, -1, "element");
    lua_pop(state, 1);
    const lua_Integer directiveState = optional_integer_argument(state, "state", 0);
    if (slotRow < 0 || slotRow > (std::numeric_limits<std::uint32_t>::max)() || nameHash < 0
        || nameHash > (std::numeric_limits<std::uint32_t>::max)() || element < 0
        || element > (std::numeric_limits<std::int32_t>::max)() || directiveState < 0
        || directiveState > 2) {
        return luaL_error(state, "directive declaration is outside its native field width");
    }
    Impl* const impl = impl_from_state(state);
    DirectiveElementDefinition resolved{};
    if (impl == nullptr || impl->definitions.resolveDirectiveElement == nullptr
        || !impl->definitions.resolveDirectiveElement(impl->definitions.context,
                                                      static_cast<std::uint32_t>(slotRow),
                                                      static_cast<std::uint32_t>(nameHash),
                                                      static_cast<std::int32_t>(element),
                                                      resolved)
        || resolved.slotRow != slot.nativeRow) {
        return luaL_error(state, "directive does not belong to this slot");
    }
    scriptable_auth::Type68Preset preset{.nameHash = resolved.nameHash,
                                         .elementIndex = resolved.elementIndex,
                                         .state = static_cast<std::int8_t>(directiveState),
                                         .visible = true};
    lua_getfield(state, 2, "audience");
    if (!lua_isnil(state, -1)) {
        const auto* ref = static_cast<const SlotHandle*>(luaL_checkudata(state, -1, kSlotMetatable));
        SlotDefinition filter{};
        if (!current_slot(state, *ref, filter) || filter.slotType != 70 || filter.slotIndex < 0
            || filter.slotIndex > INT16_MAX)
            return luaL_error(state, "directive audience requires an authored type-70 engagement sensor");
        preset.audience = {filter.registryKey, 70, static_cast<std::int16_t>(filter.slotIndex)};
    }
    lua_pop(state, 1);
    lua_getfield(state, 2, "navpoint");
    if (!lua_isnil(state, -1)) {
        const auto* targetHandle = static_cast<const SlotHandle*>(luaL_checkudata(state, -1, kSlotMetatable));
        SlotDefinition target{};
        if (!current_slot(state, *targetHandle, target) || target.slotType != 47
            || target.slotIndex < 0 || target.slotIndex > INT16_MAX)
            return luaL_error(state, "directive navpoint requires a current authored type-47 slot");
        preset.navpoint = {target.registryKey, 47, static_cast<std::int16_t>(target.slotIndex)};
    }
    lua_pop(state, 1);
    std::array<std::byte, scriptable_auth::kType68ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type68(preset, body, written) || written != body.size()) {
        return luaL_error(state, "directive native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType68Schema, scriptable_auth::kType68BitCount, body);
}

/** Hides the active directive without naming an authored element. */
[[nodiscard]] int slot_clear_directives(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_directive_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-68 directive state");
    }
    scriptable_auth::Type68Preset preset{};
    preset.visible = false;
    std::array<std::byte, scriptable_auth::kType68ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type68(preset, body, written) || written != body.size()) {
        return luaL_error(state, "directive native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType68Schema, scriptable_auth::kType68BitCount, body);
}

/** Restores one encounter engagement sensor with script-declared native initial values. */
[[nodiscard]] int slot_set_engagement_state(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_engagement_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-70 engagement state");
    }
    static constexpr std::array<std::string_view, 2> kDeclared{"flags", "revision"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer flags = optional_integer_argument(state, "flags", 1);
    const lua_Integer revision = optional_integer_argument(state, "revision", 1);
    if (flags < 0 || flags > 0x1F || revision < (std::numeric_limits<std::int16_t>::min)()
        || revision > (std::numeric_limits<std::int16_t>::max)()) {
        return luaL_error(state, "engagement state is outside its native field width");
    }
    const scriptable_auth::Type70Preset preset{
        .flags = static_cast<std::uint8_t>(flags),
        .revision = static_cast<std::int16_t>(revision),
    };
    std::array<std::byte, scriptable_auth::kType70ByteCount> body{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type70(preset, body, written) || written != body.size()) {
        return luaL_error(state, "engagement native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType70Schema, scriptable_auth::kType70BitCount, body);
}

/** Enables the native restriction; roster assembly supplies its matching bubble. */
[[nodiscard]] int slot_set_darkness_zone(lua_State* state) {
    namespace darkness=middleware::bap::activity_message::darkness_zone;
    const auto* handle=static_cast<const SlotHandle*>(luaL_checkudata(state,1,kSlotMetatable));
    static constexpr std::array<std::string_view,2> declared{"enabled","wipe_seconds"};
    refuse_unknown_arguments(state,declared);
    SlotDefinition slot{};
    if (!current_slot(state,*handle,slot)||slot.slotType!=darkness::kSlotType
        ||slot.componentClass!=darkness::kClass||slot.authSchema!=darkness::kSchema
        ||(slot.flags&format::kSlotSchemaJoinExact)==0)
        return luaL_error(state,"darkness zone requires the exact hard-wipe globals sensor");
    std::array<std::byte,darkness::kBytes> body{};
    const auto wipe=optional_integer_argument(state,"wipe_seconds",-1);
    if (wipe < -1 || wipe>3
        || !darkness::encode(optional_boolean_argument(state,"enabled",false),body,static_cast<int>(wipe)))
        return luaL_error(state,"darkness zone encoder failed");
    return queue_slot_auth(state,slot,darkness::kSchema,darkness::kBits,body);
}

/** Native typed object filters: players, one object, and volume intersection. */
[[nodiscard]] int slot_set_object_filter(lua_State* state) {
    namespace auth = middleware::bap::activity_message::scriptable_auth;
    const auto* handle=static_cast<const SlotHandle*>(luaL_checkudata(state,1,kSlotMetatable));
    static constexpr std::array<std::string_view,4> fields{"players","target","inside","inside_any"};
    refuse_unknown_arguments(state,fields);
    SlotDefinition slot{};
    if (!current_slot(state,*handle,slot) || slot.slotType!=34 || slot.authSchema!=auth::kType34Schema)
        return luaL_error(state,"object filter requires an authored type-34 sensor");
    auth::Type34Body body{};
    lua_getfield(state, 2, "inside_any");
    const bool volumes = !lua_isnil(state, -1);
    if (volumes) {
        luaL_checktype(state, -1, LUA_TTABLE);
        const auto count = lua_rawlen(state, -1);
        if (count == 0 || count > 5) return luaL_error(state, "inside_any requires 1..5 authored volumes");
        for (std::size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
            const auto* ref = static_cast<const SlotHandle*>(luaL_checkudata(state, -1, kSlotMetatable));
            SlotDefinition volume{};
            if (!current_slot(state, *ref, volume) || volume.slotType != 60)
                return luaL_error(state, "inside_any requires authored type-60 volumes");
            body.predicates[body.count++] = auth::Type34ModeFlagSlotRef{0, true,
                {volume.registryKey, 60, static_cast<std::int16_t>(volume.slotIndex)}};
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    if (optional_boolean_argument(state,"players",false))
        body.predicates[body.count++]=auth::Type34ModeOnlyB{static_cast<std::int8_t>(volumes ? 1 : 0)};
    for (const auto name : {"target", "inside"}) {
        lua_getfield(state,2,name);
        if (!lua_isnil(state,-1)) {
            const auto* ref=static_cast<const SlotHandle*>(luaL_checkudata(state,-1,kSlotMetatable));
            SlotDefinition target{};
            if (!current_slot(state,*ref,target) || target.slotType != (std::string_view(name)=="target" ? 4 : 60))
                return luaL_error(state,"filter target must be type 4; inside must be type 60");
            const auth::Type2LaneClientRef client{target.registryKey,static_cast<std::int8_t>(target.slotType),static_cast<std::int16_t>(target.slotIndex)};
            if (std::string_view(name)=="target") body.predicates[body.count++]=auth::Type34ModeSlotRefC{0,client};
            else body.predicates[body.count++]=auth::Type34ModeFlagSlotRef{1,false,client};
        }
        lua_pop(state,1);
    }
    std::array<std::byte,auth::kType34MaximumByteCount> bytes{};std::size_t written{},bits{};
    if (!auth::encode_type34(body,bytes,written,bits)) return luaL_error(state,"object filter encoder failed");
    return queue_slot_auth(state,slot,auth::kType34Schema,bits,std::span(bytes).first(written));
}
[[nodiscard]] int slot_set_mission_effect(lua_State* state) {
    namespace effect=middleware::bap::activity_message::mission_effect;
    namespace auth=middleware::bap::activity_message::scriptable_auth;
    const auto* handle=static_cast<const SlotHandle*>(luaL_checkudata(state,1,kSlotMetatable));
    static constexpr std::array<std::string_view,3> fields{"filter","enabled","revision"};
    refuse_unknown_arguments(state,fields);
    SlotDefinition slot{};
    if (!current_slot(state,*handle,slot) || slot.slotType!=26 || slot.authSchema!=0x8080954BU)
        return luaL_error(state,"mission effect requires an authored type-26 hop-on");
    const bool enabled=optional_boolean_argument(state,"enabled",true);
    auth::Type2LaneClientRef client{};
    if (enabled) {
        lua_getfield(state,2,"filter");
        const auto* ref=static_cast<const SlotHandle*>(luaL_checkudata(state,-1,kSlotMetatable));
        SlotDefinition filter{};
        if (!current_slot(state,*ref,filter) || filter.slotType!=34) return luaL_error(state,"mission effect requires a type-34 filter");
        client={filter.registryKey,34,static_cast<std::int16_t>(filter.slotIndex)};lua_pop(state,1);
    }
    const auto revision=optional_integer_argument(state,"revision",1);
    if (revision<=0 || revision>INT32_MAX) return luaL_error(state,"effect revision must be positive");
    std::array<std::byte,effect::kBytes> body{};std::size_t written{};
    if (!effect::encode(client,enabled,static_cast<std::int32_t>(revision),body,written)) return luaL_error(state,"mission effect encoder failed");
    return queue_slot_auth(state,slot,0x8080954BU,effect::kBits,body);
}

/** Binds an authored damage monitor to its exact object, with a retry revision. */
[[nodiscard]] int slot_watch_damage(lua_State* state) {
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state,1,kSlotMetatable));
    static constexpr std::array<std::string_view,2> fields{"target", "revision"};
    refuse_unknown_arguments(state,fields);
    SlotDefinition slot{}, target{};
    if (!current_slot(state,*handle,slot) || slot.slotType != 20 || slot.authSchema != 0x80809563U)
        return luaL_error(state,"damage watch requires an authored type-20 monitor");
    lua_getfield(state,2,"target");
    const auto* ref = static_cast<const SlotHandle*>(luaL_checkudata(state,-1,kSlotMetatable));
    if (!current_slot(state,*ref,target) || !exact_object_slot(target))
        return luaL_error(state,"damage target must be an authored type-4 object");
    lua_pop(state,1);
    const auto revision = optional_integer_argument(state,"revision",1);
    if (revision <= 0 || revision > INT32_MAX) return luaL_error(state,"damage revision must be positive");
    std::array<std::byte,11> body{}; std::size_t written{};
    middleware::encoding::bits::Writer writer(body);
    if (!writer.write(target.registryKey,32) || !writer.write(5,7)
        || !writer.write(static_cast<std::uint16_t>(target.slotIndex) + 32768U,16)
        || !writer.write(static_cast<std::uint32_t>(revision) + 0x80000000U,32)
        || !writer.finish(written)) return luaL_error(state,"damage monitor encoder failed");
    return queue_slot_auth(state,slot,0x80809563U,87,body);
}

/** Selects one authored section in a native music sensor's 128-bit mask. */
[[nodiscard]] int slot_set_music_section(lua_State* state) {
    namespace music = middleware::bap::activity_message::music_section;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 2> declared{"section", "enabled"};
    refuse_unknown_arguments(state, declared);
    SlotDefinition slot{};
    const auto section = checked_integer_argument(state, "section");
    if (!current_slot(state, *handle, slot) || slot.slotType != 11 || slot.componentClass != music::kClass
        || slot.authSchema != music::kSchema || !(slot.flags & format::kSlotSchemaJoinExact)
        || section < 0 || section >= 128)
        return luaL_error(state, "music requires an exact type-11 sensor and section index 0..127");
    std::array<std::byte, music::kBytes> body{};
    std::size_t written{};
    if (!music::encode(static_cast<std::uint8_t>(section), optional_boolean_argument(state, "enabled", true), body, written))
        return luaL_error(state, "music section encoder failed");
    return queue_slot_auth(state, slot, music::kSchema, music::kBits, body);
}

/** Spawns authored entry zero and subscribes to accepted native player use. */
[[nodiscard]] int slot_set_interactable_object(lua_State* state) {
    namespace object = middleware::bap::activity_message::interactable_object;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state,1,kSlotMetatable));
    static constexpr std::array<std::string_view,3> declared{"generation", "track_owner", "active"};
    refuse_unknown_arguments(state,declared);
    SlotDefinition slot{};
    if (!current_slot(state,*handle,slot) || !exact_object_slot(slot))
        return luaL_error(state,"interaction requires an exact authored object");
    const auto generation = optional_integer_argument(state,"generation",1);
    if (generation <= 0 || generation > 0x7FFFFFFF)
        return luaL_error(state,"object generation must be a positive int32");
    const bool trackOwner = optional_boolean_argument(state,"track_owner",false);
    std::array<std::byte,object::kOwnerBytes> body{};
    std::size_t written{};
    if (!object::encode(static_cast<std::int32_t>(generation),body,written,trackOwner,
        optional_boolean_argument(state,"active",true)))
        return luaL_error(state,"interactable object encoder failed");
    return queue_slot_auth(state,slot,object::kSchema,trackOwner ? object::kOwnerBits : object::kBits,
        std::span(body).first(written));
}

/** Enables the authored Ghost interaction without replacing its action hash. */
[[nodiscard]] int slot_set_ghost_link(lua_State* state) {
    namespace ghost = middleware::bap::activity_message::ghost_link;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 2> declared{"generation", "enabled"};
    refuse_unknown_arguments(state, declared);
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || slot.slotType != ghost::kSlotType
        || slot.componentClass != ghost::kComponentClass || slot.authSchema != ghost::kAuthSchema
        || (slot.flags & format::kSlotSchemaJoinExact) == 0) {
        return luaL_error(state, "activity slot is not an exact Ghost-link sensor");
    }
    const lua_Integer generation = checked_integer_argument(state, "generation");
    if (generation <= 0 || generation > 0x7FFFFFFF) {
        return luaL_error(state, "Ghost-link generation must be a positive int32");
    }
    const bool enabled = optional_boolean_argument(state, "enabled", true);
    std::array<std::byte, ghost::kByteCount> body{};
    std::size_t written = 0;
    if (!ghost::encode(static_cast<std::int32_t>(generation), enabled, body, written)) {
        return luaL_error(state, "Ghost-link native encoder failed");
    }
    return queue_slot_auth(state, slot, ghost::kAuthSchema, ghost::kBitCount, body);
}

/** Names the player, the event area and the leave timeout one public-event sensor watches. */
[[nodiscard]] int slot_set_public_event_state(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_public_event_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-71 public-event sensor");
    }
    static constexpr std::array<std::string_view, 4> kDeclared{
        "state", "player", "area", "leave_seconds"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer eventState = optional_integer_argument(state, "state", 0);
    if (eventState < (std::numeric_limits<std::int32_t>::min)()
        || eventState > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "state must be a 32-bit signed integer");
    }
    // Absent, the sensor watches this link's own player. Present, it is a decimal string, the
    // form every 64-bit key crosses into Lua in.
    std::uint64_t player = impl_from_state(state)->identity.playerKey;
    if (push_argument(state, "player") != LUA_TNIL) {
        lua_pop(state, 1);
        const std::string_view playerText = borrowed_string_argument(state, "player");
        const auto parsed =
            std::from_chars(playerText.data(), playerText.data() + playerText.size(), player);
        if (parsed.ec != std::errc{} || parsed.ptr != playerText.data() + playerText.size()) {
            return luaL_error(state, "player must be a decimal player key string");
        }
        lua_pop(state, 1);
    } else {
        lua_pop(state, 1);
    }
    if (player == 0) {
        return luaL_error(state, "no player key is known for this activity link");
    }
    const SlotHandle areaHandle = checked_argument<SlotHandle>(state, "area", kSlotMetatable);
    const lua_Number seconds = checked_number_argument(state, "leave_seconds");
    SlotDefinition area{};
    if (!current_slot(state, areaHandle, area)) {
        return luaL_error(state, "area slot is stale or invalid");
    }
    if (!std::isfinite(seconds) || seconds < 0.0
        || seconds > static_cast<lua_Number>((std::numeric_limits<float>::max)())) {
        return luaL_error(state, "leave_seconds must be a finite non-negative number");
    }
    const scriptable_auth::Type71Body body{
        .state = static_cast<std::int32_t>(eventState),
        .playerIdentity = player,
        .areaRegistryKey = area.registryKey,
        .areaSlotType = static_cast<std::uint8_t>(area.slotType),
        .areaSlotIndex = static_cast<std::uint16_t>(area.slotIndex),
        .leaveSeconds = static_cast<float>(seconds),
    };
    std::array<std::byte, scriptable_auth::kType71ByteCount> bytes{};
    std::size_t written = 0;
    if (!scriptable_auth::encode_type71(body, bytes, written) || written != bytes.size()) {
        return luaL_error(state, "public-event native encoder failed");
    }
    return queue_slot_auth(
        state, slot, scriptable_auth::kType71Schema, scriptable_auth::kType71BitCount, bytes);
}

/** Assigns a squad to a native combat objective and its selected task group. */
[[nodiscard]] int slot_assign_combat_objective(lua_State* state) {
    namespace objective = middleware::bap::activity_message::squad_objective;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 4> declared{"objective", "revision", "task_group", "reserved"};
    refuse_unknown_arguments(state, declared);
    const auto reference = checked_argument<SlotHandle>(state, "objective", kSlotMetatable);
    const auto revision = checked_integer_argument(state, "revision");
    const auto group = checked_integer_argument(state, "task_group");
    const bool reserved = optional_boolean_argument(state, "reserved", false);
    SlotDefinition squad{}, target{};
    if (!current_slot(state, *handle, squad) || squad.slotType != 1
        || squad.componentClass != format::kSquadComponentClass
        || squad.authSchema != objective::kSchema || !(squad.flags & format::kSlotSchemaJoinExact)
        || !current_slot(state, reference, target) || !exact_objective_reset_slot(target)
        || target.componentClass != 0x80808348 || squad.registryKey != target.registryKey)
        return luaL_error(state, "combat objective requires exact squad/objective slots in the same registry");
    if (revision <= 0 || revision > 0x7fffffff || group < -1 || group >= 24 || target.slotIndex > 32767)
        return luaL_error(state, "combat objective revision, group or index is outside its native range");
    std::array<std::byte, objective::kBytes> body{};
    if (!objective::encode({target.registryKey, static_cast<std::uint32_t>(revision),
                           static_cast<std::uint16_t>(target.slotIndex), static_cast<std::int32_t>(group), reserved}, body))
        return luaL_error(state, "combat objective encoder failed");
    return queue_slot_auth(state, squad, objective::kSchema, objective::kBits, body);
}

/** Creates a named actor and starts one package-authored movement path. */
[[nodiscard]] int slot_play_actor_path(lua_State* state) {
    namespace path = middleware::bap::activity_message::combatant_path;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 3> declared{"generation", "revision", "path"};
    refuse_unknown_arguments(state, declared);
    const auto generation = checked_integer_argument(state, "generation");
    const auto revision = checked_integer_argument(state, "revision");
    const auto reference = checked_argument<SlotHandle>(state, "path", kSlotMetatable);
    SlotDefinition actor{}, sequence{};
    if (!current_slot(state, *handle, actor) || !exact_combatant_slot(actor)
        || !current_slot(state, reference, sequence) || sequence.slotType != 58
        || sequence.componentClass != 0x80807D9B || actor.objectTag != sequence.objectTag
        || actor.registryKey != sequence.registryKey)
        return luaL_error(state, "actor path requires an exact member and same-registry type-58 path");
    if (generation <= 0 || generation > 0x7fffffff || revision <= 0 || revision > 0x7fffffff
        || sequence.slotIndex > 32767)
        return luaL_error(state, "actor path generation, revision or index is outside its native range");
    std::array<std::byte, path::kBytes> body{};
    if (!path::encode({static_cast<std::uint32_t>(generation), static_cast<std::uint32_t>(revision),
                      sequence.registryKey, static_cast<std::uint16_t>(sequence.slotIndex)}, body))
        return luaL_error(state, "actor path encoder failed");
    return queue_slot_auth(state, actor, path::kSchema, path::kBits, body);
}

/** Runs an authored native custom action without recreating its actor. */
[[nodiscard]] int slot_play_actor_action(lua_State* state) {
    namespace action = middleware::bap::activity_message::combatant_action;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 4> declared{"generation", "revision", "group", "action"};
    refuse_unknown_arguments(state, declared);
    const auto generation = checked_integer_argument(state, "generation");
    const auto revision = checked_integer_argument(state, "revision");
    const auto group = checked_integer_argument(state, "group");
    const auto name = checked_integer_argument(state, "action");
    SlotDefinition actor{};
    if (!current_slot(state, *handle, actor) || !exact_combatant_slot(actor)
        || generation <= 0 || generation > 0x7fffffff || revision <= 0 || revision > 0x7fffffff
        || group < 0 || group > 0xffffffffLL || name <= 0 || name > 0xffffffffLL)
        return luaL_error(state, "actor action requires an exact member and valid native identities");
    std::array<std::byte, action::kBytes> body{};
    if (!action::encode({static_cast<std::uint32_t>(generation), static_cast<std::uint32_t>(revision),
                        static_cast<std::uint32_t>(group), static_cast<std::uint32_t>(name)}, body))
        return luaL_error(state, "actor action encoder failed");
    return queue_slot_auth(state, actor, action::kSchema, action::kBits, body);
}

/** Gives a reserved squad to the named actor's native passenger-delivery component. */
[[nodiscard]] int slot_deliver_squad(lua_State* state) {
    namespace delivery = middleware::bap::activity_message::combatant_delivery;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 3> declared{"generation", "revision", "squad"};
    refuse_unknown_arguments(state, declared);
    const auto generation = checked_integer_argument(state, "generation");
    const auto revision = checked_integer_argument(state, "revision");
    const auto reference = checked_argument<SlotHandle>(state, "squad", kSlotMetatable);
    SlotDefinition actor{}, squad{};
    if (!current_slot(state, *handle, actor) || !exact_combatant_slot(actor)
        || !current_slot(state, reference, squad) || squad.slotType != 1
        || squad.componentClass != format::kSquadComponentClass
        || !(squad.flags & format::kSlotSchemaJoinExact)
        || actor.objectTag != squad.objectTag || actor.registryKey != squad.registryKey)
        return luaL_error(state, "delivery requires an exact member and same-registry squad");
    if (generation <= 0 || generation > 0x7fffffff || revision <= 0 || revision > 0x7fffffff
        || squad.slotIndex > 32767)
        return luaL_error(state, "delivery generation, revision or index is outside its native range");
    std::array<std::byte, delivery::kBytes> body{};
    if (!delivery::encode({static_cast<std::uint32_t>(generation), static_cast<std::uint32_t>(revision),
                          squad.registryKey, static_cast<std::uint16_t>(squad.slotIndex)}, body))
        return luaL_error(state, "delivery encoder failed");
    return queue_slot_auth(state, actor, delivery::kSchema, delivery::kBits, body);
}

/** Sends one native manifest so multiple reserved squads share the same ship and unload. */
[[nodiscard]] int slot_deliver_squads(lua_State* state) {
    namespace delivery = middleware::bap::activity_message::combatant_delivery;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view,3> declared{"generation","revision","squads"};
    refuse_unknown_arguments(state, declared);
    const auto generation = checked_integer_argument(state, "generation");
    const auto revision = checked_integer_argument(state, "revision");
    SlotDefinition actor{};
    if (!current_slot(state,*handle,actor) || !exact_combatant_slot(actor)
        || generation <= 0 || generation > 0x7fffffff || revision <= 0 || revision > 0x7fffffff)
        return luaL_error(state,"delivery requires an exact actor and positive generation/revision");
    lua_getfield(state,2,"squads"); luaL_checktype(state,-1,LUA_TTABLE);
    const int list=lua_gettop(state); const auto count=lua_rawlen(state,list);
    if (count==0 || count>delivery::kMaxSquads) return luaL_error(state,"delivery needs 1..8 reserved squads");
    std::array<delivery::SquadReference,delivery::kMaxSquads> refs{};
    for (std::size_t i=0;i<count;++i) {
        lua_rawgeti(state,list,static_cast<lua_Integer>(i+1));
        const auto* squadHandle=static_cast<const SlotHandle*>(luaL_checkudata(state,-1,kSlotMetatable));
        SlotDefinition squad{};
        if (!current_slot(state,*squadHandle,squad) || squad.slotType!=1
            || squad.componentClass!=format::kSquadComponentClass || !(squad.flags & format::kSlotSchemaJoinExact)
            || squad.registryKey!=actor.registryKey || squad.objectTag!=actor.objectTag || squad.slotIndex>32767)
            return luaL_error(state,"delivery squads must be exact and in the actor's registry");
        refs[i]={squad.registryKey,static_cast<std::uint16_t>(squad.slotIndex)};
        lua_pop(state,1);
    }
    lua_pop(state,1);
    std::array<std::byte,delivery::kMaxBytes> body{}; std::size_t written{},bits{};
    if (!delivery::encode_many(static_cast<std::uint32_t>(generation),static_cast<std::uint32_t>(revision),
        std::span(refs).first(count),body,written,bits)) return luaL_error(state,"invalid delivery manifest");
    return queue_slot_auth(state,actor,delivery::kSchema,bits,std::span(body).first(written));
}

/** Disables the named actor after completion, including retained Auth on region reload. */
[[nodiscard]] int slot_retire_actor(lua_State* state) {
    namespace retire = middleware::bap::activity_message::combatant_retire;
    const auto* handle = static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 1> declared{"generation"};
    refuse_unknown_arguments(state, declared);
    const auto generation = checked_integer_argument(state, "generation");
    SlotDefinition actor{};
    if (!current_slot(state, *handle, actor) || !exact_combatant_slot(actor)
        || generation <= 0 || generation > 0x7fffffff)
        return luaL_error(state, "actor retirement requires an exact member and positive generation");
    std::array<std::byte, retire::kBytes> body{};
    if (!retire::encode(static_cast<std::uint32_t>(generation), body))
        return luaL_error(state, "actor retirement encoder failed");
    return queue_slot_auth(state, actor, retire::kSchema, retire::kBits, body);
}

/** Binds one exact combatant to its package-authored squad member. */
[[nodiscard]] int slot_bind_combatant_to_squad(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_combatant_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-2 combatant");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::bindCombatantToSquad;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/**
 * Reads the optional `with` list: more object slots that answer on this one's revision.
 * @return False with the Lua error already raised.
 */
[[nodiscard]] bool parse_object_burst(lua_State* state, Intent& intent) {
    lua_getfield(state, 2, "with");
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        static_cast<void>(luaL_argerror(state, 2, "object burst list is not a table"));
        return false;
    }
    const int list = lua_gettop(state);
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(state, list));
    if (count < 0
        || static_cast<std::size_t>(count)
               > ::sunrise::state::activity::mission::kIntentBurstCapacity) {
        lua_pop(state, 1);
        static_cast<void>(luaL_argerror(state, 2, "object burst list is too long"));
        return false;
    }
    for (lua_Integer entry = 1; entry <= count; ++entry) {
        lua_rawgeti(state, list, entry);
        SlotDefinition member{};
        const bool resolved =
            resolve_slot(state, lua_gettop(state), member) && exact_object_slot(member);
        lua_pop(state, 1);
        if (!resolved) {
            lua_pop(state, 1);
            static_cast<void>(
                luaL_argerror(state, 2, "object burst names a slot that is not an exact type-4"));
            return false;
        }
        intent.burstRows[static_cast<std::size_t>(entry - 1)] = member.nativeRow;
    }
    lua_pop(state, 1);
    intent.burstRowCount = static_cast<std::uint8_t>(count);
    return true;
}

/** Instantiates or removes the package-owned entry one type-4 slot selects. */
[[nodiscard]] int slot_set_object_active(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_object_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-4 authored object");
    }
    static constexpr std::array<std::string_view, 2> kDeclared{"active", "with"};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setObjectActive;
    intent.firstRow = definition.nativeRow;
    intent.entryIndex = 0;
    intent.active = optional_boolean_argument(state, "active", true);
    if (!parse_object_burst(state, intent)) {
        return 0;
    }
    return queue_intent(state, frame, intent);
}

/** Sets one verified type-23 channel through the same guarded route. */
[[nodiscard]] int slot_set_channel(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_device_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-23 device");
    }
    // Both parameters carry their own bound, so neither the lane nor the range is tested here.
    static constexpr std::array<std::string_view, 3> kDeclared{"channel", "value", "snap"};
    refuse_unknown_arguments(state, kDeclared);
    const DeviceChannelHandle channel =
        checked_argument<DeviceChannelHandle>(state, "channel", kDeviceChannelMetatable);
    const UnitScalarHandle value =
        checked_argument<UnitScalarHandle>(state, "value", kUnitScalarMetatable);
    const bool snap = optional_boolean_argument(state, "snap", false);

    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setDeviceChannel;
    intent.firstRow = definition.nativeRow;
    intent.deviceValue = value.value;
    intent.deviceChannel = channel.channel;
    intent.deviceSnap = snap;
    return queue_intent(state, frame, intent);
}
/** Applies one named device transition through the same guarded channel route. */
[[nodiscard]] int slot_transition(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_device_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-23 device");
    }
    // The handle is a row of the closed vocabulary, so no word is matched here.
    static constexpr std::array<std::string_view, 2> kDeclared{"transition", "snap"};
    refuse_unknown_arguments(state, kDeclared);
    const DeviceTransitionHandle requested =
        checked_argument<DeviceTransitionHandle>(state, "transition", kDeviceTransitionMetatable);
    const bool snap = optional_boolean_argument(state, "snap", false);
    const DeviceTransition& transition = kDeviceTransitions[requested.row];
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setDeviceChannel;
    intent.firstRow = definition.nativeRow;
    intent.deviceValue = transition.value;
    intent.deviceChannel = static_cast<std::uint8_t>(transition.channel);
    intent.deviceSnap = snap;
    return queue_intent(state, frame, intent);
}
/**
 * Fires one type-31 configured trigger. One authored typed reference must be live and eligible.
 * The pulse carries no caller value. `enabled` is fixed true and the auxiliary stays zero, and
 * the Host mints the generation from the target's own guard so a replay cannot reorder.
 */
[[nodiscard]] int slot_fire_trigger(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale or invalid");
    }
    if (!exact_trigger_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-31 trigger");
    }
    // The pulse has no parameters, so an unsafe call cannot be spelled.
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::fireTrigger;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Lua `play_sequence` on a slot. Errors unless the slot is an exact type-5 sequence. */
[[nodiscard]] int slot_play_sequence(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_sequence_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-5 authored sequence");
    }
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playSequence;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Lua `set_cinematic_active` on a slot. Errors unless the slot is an exact cinematic. */
[[nodiscard]] int slot_set_cinematic_active(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_cinematic_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-6 authored cinematic");
    }
    static constexpr std::array<std::string_view, 1> kDeclared{"active"};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setCinematicActive;
    intent.firstRow = definition.nativeRow;
    intent.active = optional_boolean_argument(state, "active", true);
    return queue_intent(state, frame, intent);
}

/** Queues the parameter-free reset of every objective lane owned by one type-3 slot. */
[[nodiscard]] int slot_reset_objectives(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_objective_reset_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-3 objective reset");
    }
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::resetObjectives;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/** Advances the exact objective bit authored by one type-38 task slot. */
[[nodiscard]] int slot_advance_task(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_task_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-38 authored task");
    }
    static constexpr std::array<std::string_view, 0> kDeclared{};
    refuse_unknown_arguments(state, kDeclared);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::advanceTask;
    intent.firstRow = definition.nativeRow;
    return queue_intent(state, frame, intent);
}

/**
 * Starts one state of the actor a type-42 sensor drives. With no `state` the slot's target must
 * declare exactly one state; a generated `state` row must belong to this slot.
 */
[[nodiscard]] int slot_play_performance(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_performance_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-42 performance sensor");
    }
    static constexpr std::array<std::string_view, 1> kDeclared{"state"};
    refuse_unknown_arguments(state, kDeclared);
    lua_Integer slotRow = static_cast<lua_Integer>(definition.nativeRow);
    lua_Integer nameHash = 0;
    if (!lua_isnoneornil(state, 2)) {
        lua_getfield(state, 2, "state");
        if (!lua_isnil(state, -1)) {
            luaL_checktype(state, -1, LUA_TTABLE);
            slotRow = directive_integer(state, -1, "slot_row");
            nameHash = directive_integer(state, -1, "name_hash");
        }
        lua_pop(state, 1);
    }
    if (slotRow < 0 || slotRow > (std::numeric_limits<std::uint32_t>::max)() || nameHash < 0
        || nameHash > (std::numeric_limits<std::uint32_t>::max)()) {
        return luaL_error(state, "performance state declaration is outside its native field width");
    }
    Impl* const impl = impl_from_state(state);
    PerformanceStateDefinition resolved{};
    if (impl == nullptr || impl->definitions.resolvePerformanceState == nullptr
        || !impl->definitions.resolvePerformanceState(impl->definitions.context,
                                                      static_cast<std::uint32_t>(slotRow),
                                                      static_cast<std::uint32_t>(nameHash),
                                                      resolved)
        || resolved.slotRow != definition.nativeRow) {
        return luaL_error(state, "performance state does not belong to this slot");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playPerformance;
    intent.firstRow = definition.nativeRow;
    intent.secondRow = resolved.nameHash;
    return queue_intent(state, frame, intent);
}

/** Reads one unsigned 32-bit field from an atom declaration. */
[[nodiscard]] std::uint32_t atom_u32(lua_State* state, int table, const char* field) {
    const lua_Integer value = directive_integer(state, table, field);
    if (value < 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
        static_cast<void>(luaL_error(state, "atom field '%s' is outside a 32-bit range", field));
    }
    return static_cast<std::uint32_t>(value);
}

/** Reads one finite real field from an atom declaration. */
[[nodiscard]] float atom_real(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    const auto value = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    if (!std::isfinite(value)) {
        static_cast<void>(luaL_error(state, "atom field '%s' is not finite", field));
    }
    return value;
}

/** Reads one optional small unsigned field from an atom declaration. */
[[nodiscard]] std::uint8_t
atom_u8(lua_State* state, int table, const char* field, std::uint8_t bound) {
    lua_getfield(state, table, field);
    const lua_Integer value = lua_isnil(state, -1) ? 0 : luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (value < 0 || value > bound) {
        static_cast<void>(luaL_error(state, "atom field '%s' is outside its native width", field));
    }
    return static_cast<std::uint8_t>(value);
}

/** Reads one optional boolean field from an atom declaration. */
[[nodiscard]] bool atom_flag(lua_State* state, int table, const char* field) {
    lua_getfield(state, table, field);
    const bool value = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

/** Resolves the live Slot handle an atom points at into its exact 55-bit client reference. */
[[nodiscard]] scriptable_auth::Type2LaneClientRef atom_target(lua_State* state, int table) {
    scriptable_auth::Type2LaneClientRef reference{};
    lua_getfield(state, table, "target");
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, -1, kSlotMetatable));
    SlotDefinition target{};
    if (!current_slot(state, *handle, target)) {
        static_cast<void>(luaL_error(state, "atom target slot is stale"));
    }
    constexpr auto kHighestReferenceIndex =
        static_cast<std::uint32_t>((std::numeric_limits<std::int16_t>::max)());
    if (target.slotType > 126
        || static_cast<std::uint32_t>(target.slotIndex) > kHighestReferenceIndex) {
        static_cast<void>(luaL_error(state, "atom target slot is outside a client reference"));
    }
    reference.registryKey = target.registryKey;
    reference.slotType = static_cast<std::int8_t>(target.slotType);
    reference.slotIndex = static_cast<std::int16_t>(target.slotIndex);
    lua_pop(state, 1);
    return reference;
}

scriptable_auth::Type2LanePrimary atom_face(lua_State* state, int table) {
    return scriptable_auth::Type2LaneRefByte{atom_target(state, table),
                                             atom_u8(state, table, "value", 0xFFU)};
}

scriptable_auth::Type2LanePrimary atom_snap_to(lua_State* state, int table) {
    return scriptable_auth::Type2LaneAlternateRefByte{atom_target(state, table),
                                                      atom_u8(state, table, "value", 0xFFU)};
}

scriptable_auth::Type2LanePrimary atom_sequence(lua_State* state, int table) {
    return scriptable_auth::Type2LaneU32{atom_u32(state, table, "value")};
}

scriptable_auth::Type2LanePrimary atom_sleep(lua_State* state, int table) {
    return scriptable_auth::Type2LaneReal32{atom_real(state, table, "seconds")};
}

scriptable_auth::Type2LanePrimary atom_move_to(lua_State* state, int table) {
    return scriptable_auth::Type2LaneRefByteBool{atom_target(state, table),
                                                 atom_u8(state, table, "value", 0xFFU),
                                                 atom_flag(state, table, "enabled")};
}

scriptable_auth::Type2LanePrimary atom_trivial(lua_State* /*state*/, int /*table*/) {
    return scriptable_auth::Type2LaneEmpty{};
}

scriptable_auth::Type2LanePrimary atom_control_flag(lua_State* state, int table) {
    return scriptable_auth::Type2LaneU6{atom_u8(state, table, "value", 0x3FU)};
}

scriptable_auth::Type2LanePrimary atom_set_temperament(lua_State* state, int table) {
    return scriptable_auth::Type2LaneU32Bool{atom_u32(state, table, "value"),
                                             atom_flag(state, table, "enabled")};
}

scriptable_auth::Type2LanePrimary atom_set_channel(lua_State* state, int table) {
    return scriptable_auth::Type2LaneU32Real32{atom_u32(state, table, "channel"),
                                               atom_real(state, table, "value")};
}

/** Reads the three ability identities, the target and the two signed bytes. */
scriptable_auth::Type2LanePrimary atom_ability(lua_State* state, int table) {
    scriptable_auth::Type2LaneTripleRef ability{};
    lua_getfield(state, table, "values");
    luaL_checktype(state, -1, LUA_TTABLE);
    const int values = lua_gettop(state);
    for (std::size_t index = 0; index < ability.values.size(); ++index) {
        lua_rawgeti(state, values, static_cast<lua_Integer>(index) + 1);
        const lua_Integer value = luaL_checkinteger(state, -1);
        lua_pop(state, 1);
        if (value < 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
            static_cast<void>(luaL_error(state, "ability value is outside a 32-bit range"));
        }
        ability.values[index] = static_cast<std::uint32_t>(value);
    }
    lua_pop(state, 1);
    ability.reference = atom_target(state, table);
    ability.mode = static_cast<std::int8_t>(atom_u8(state, table, "mode", 6U));
    ability.value =
        static_cast<std::int8_t>(static_cast<int>(atom_u8(state, table, "value", 0xFFU)) - 128);
    return ability;
}

/** One script-facing atom name and the reader that fills its native lane child. */
struct AtomKind final {
    std::string_view name;
    scriptable_auth::Type2LanePrimary (*read)(lua_State*, int);
};

/** The ten primary lane schemas the client-atom runner selects between. */
constexpr std::array<AtomKind, 10> kAtomKinds{{
    {"face", &atom_face},
    {"sequence", &atom_sequence},
    {"sleep", &atom_sleep},
    {"move_to", &atom_move_to},
    {"trivial", &atom_trivial},
    {"control_flag", &atom_control_flag},
    {"set_temperament", &atom_set_temperament},
    {"set_channel", &atom_set_channel},
    {"snap_to", &atom_snap_to},
    {"ability", &atom_ability},
}};

/** Builds one atom lane from its declaration table at stack index `table`. */
void parse_atom(lua_State* state, int table, scriptable_auth::Type2KeyedLane& lane) {
    lua_getfield(state, table, "kind");
    const std::string_view kind = lua_string_view(state, -1);
    const auto match =
        std::find_if(kAtomKinds.begin(), kAtomKinds.end(), [kind](const AtomKind& row) noexcept {
            return row.name == kind;
        });
    if (match == kAtomKinds.end()) {
        static_cast<void>(luaL_error(state, "unknown atom kind"));
        return;
    }
    lane.primary = match->read(state, table);
    lua_pop(state, 1);
    lua_getfield(state, table, "quantized");
    if (!lua_isnil(state, -1)) {
        const lua_Integer quantized = luaL_checkinteger(state, -1);
        if (quantized < 0 || quantized > 0x7FF) {
            static_cast<void>(luaL_error(state, "atom quantized value is wider than 11 bits"));
        }
        lane.secondary =
            scriptable_auth::Type2LaneQuantized11{static_cast<std::uint16_t>(quantized)};
    }
    lua_pop(state, 1);
}

/**
 * Loads the 32-lane client-atom program one type-2 combatant runs on its bound actor. A rising
 * `generation` restarts the program; the same one leaves the running program alone.
 */
[[nodiscard]] int slot_run_atoms(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition slot{};
    if (!current_slot(state, *handle, slot) || !exact_combatant_slot(slot)) {
        return luaL_error(state, "activity slot is not an exact type-2 combatant");
    }
    static constexpr std::array<std::string_view, 4> kDeclared{
        "generation", "seed", "binding", "atoms"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer generation = checked_integer_argument(state, "generation");
    const lua_Integer seed = optional_integer_argument(state, "seed", 0);
    if (generation <= 0 || generation > 0x7FFFFFFF || seed < 0 || seed > 0x3F) {
        return luaL_error(state, "atom generation or seed is outside its native field width");
    }
    scriptable_auth::Type2Body body{};
    body.channels.revision = static_cast<std::uint32_t>(generation);
    body.channels.actorBinding = scriptable_auth::Type2ActorBinding::squadMember;
    if (push_argument(state, "binding") != LUA_TNIL) {
        const std::string_view binding = lua_string_view(state, -1);
        if (binding == "self") {
            body.channels.actorBinding = scriptable_auth::Type2ActorBinding::selfOwned;
        } else if (binding != "squad") {
            return luaL_error(state, "actor binding must be 'squad' or 'self'");
        }
    }
    lua_pop(state, 1);
    body.atoms.generation = static_cast<std::uint32_t>(generation);
    body.atoms.progressSeed = static_cast<std::uint8_t>(seed);

    lua_getfield(state, 2, "atoms");
    luaL_checktype(state, -1, LUA_TTABLE);
    const int list = lua_gettop(state);
    const auto count = static_cast<std::size_t>(lua_rawlen(state, list));
    if (count == 0 || count > body.atoms.lanes.size()) {
        return luaL_error(state, "an atom program holds one to 32 lanes");
    }
    for (std::size_t index = 0; index < count; ++index) {
        lua_rawgeti(state, list, static_cast<lua_Integer>(index) + 1);
        luaL_checktype(state, -1, LUA_TTABLE);
        parse_atom(state, lua_gettop(state), body.atoms.lanes[index]);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    body.atoms.count = static_cast<std::uint8_t>(count);
    if (body.atoms.progressSeed > body.atoms.count) {
        return luaL_error(state, "atom seed names a lane the program does not hold");
    }

    std::array<std::byte, scriptable_auth::kType2MaximumBodyByteCount> encoded{};
    std::size_t written = 0;
    std::size_t writtenBits = 0;
    if (!scriptable_auth::encode_type2_body(body, encoded, written, writtenBits)) {
        return luaL_error(state, "atom program native encoder failed");
    }
    return queue_slot_auth(state,
                           slot,
                           scriptable_auth::kType2Schema,
                           writtenBits,
                           std::span<const std::byte>(encoded.data(), written));
}

/** Fires one bounded cue from an exact type-53 authored dialogue list. */
[[nodiscard]] int slot_play_dialogue_cue(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition) || !exact_dialogue_slot(definition)) {
        return luaL_error(state, "activity slot is not an exact type-53 authored dialogue");
    }
    static constexpr std::array<std::string_view, 1> kDeclared{"cue"};
    refuse_unknown_arguments(state, kDeclared);
    const lua_Integer cue = checked_integer_argument(state, "cue");
    if (cue < 0 || cue > (std::numeric_limits<std::uint16_t>::max)()) {
        return luaL_error(state, "dialogue cue must be a non-negative 16-bit integer");
    }
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::playDialogueCue;
    intent.firstRow = definition.nativeRow;
    intent.secondRow = static_cast<std::uint32_t>(cue);
    return queue_intent(state, frame, intent);
}
/** Reads one Slot row member, its syntax methods, and its authorized actions. */
[[nodiscard]] int slot_index(lua_State* state) {
    const auto* const handle =
        static_cast<const SlotHandle*>(luaL_checkudata(state, 1, kSlotMetatable));
    SlotDefinition definition{};
    if (!current_slot(state, *handle, definition)) {
        return luaL_error(state, "activity slot is stale");
    }
    const std::string_view key = lua_string_view(state, 2);
    if (key == "row") {
        lua_pushinteger(state, definition.localRow);
    } else if (key == "id") {
        lua_pushlstring(state, definition.id.data(), definition.id.size());
    } else if (key == "name") {
        lua_pushlstring(state, definition.name.data(), definition.name.size());
    } else if (key == "object_id") {
        lua_pushlstring(state, definition.objectId.data(), definition.objectId.size());
    } else if (key == "object_tag") {
        lua_pushinteger(state, definition.objectTag);
    } else if (key == "registry_key") {
        lua_pushinteger(state, definition.registryKey);
    } else if (key == "slot_index") {
        lua_pushinteger(state, definition.slotIndex);
    } else if (key == "slot_type") {
        lua_pushinteger(state, definition.slotType);
    } else if (key == "component_class") {
        lua_pushinteger(state, definition.componentClass);
    } else if (key == "sense_schema") {
        lua_pushinteger(state, definition.senseSchema);
    } else if (key == "auth_schema") {
        lua_pushinteger(state, definition.authSchema);
    } else if (key == "sense_schema_id") {
        lua_pushlstring(state, definition.senseSchemaId.data(), definition.senseSchemaId.size());
    } else if (key == "auth_schema_id") {
        lua_pushlstring(state, definition.authSchemaId.data(), definition.authSchemaId.size());
    } else if (key == "auth_type" || key == "auth_min_bits" || key == "auth_max_bits"
               || key == "auth_component_offset" || key == "auth_dynamic"
               || key == "auth_writable") {
        const auth_catalog::Type* const auth = auth_catalog::find(
            static_cast<std::uint8_t>(definition.slotType), definition.authSchema);
        if (auth == nullptr) {
            lua_pushnil(state);
        } else if (key == "auth_type") {
            lua_pushlstring(state, auth->name.data(), auth->name.size());
        } else if (key == "auth_min_bits") {
            lua_pushinteger(state, auth->minimumBits);
        } else if (key == "auth_max_bits") {
            lua_pushinteger(state, auth->maximumBits);
        } else if (key == "auth_component_offset") {
            if (auth->hasContiguousMirror) {
                lua_pushinteger(state, auth->componentOffset);
            } else {
                lua_pushnil(state);
            }
        } else if (key == "auth_dynamic") {
            lua_pushboolean(state, auth->hasDynamicBody);
        } else {
            lua_pushboolean(state, auth->writable);
        }
    } else if (key == "flags") {
        lua_pushinteger(state, definition.flags);
    } else if (key == "set_object_filter") {
        lua_pushcfunction(state, &slot_set_object_filter);
    } else if (key == "set_mission_effect") {
        lua_pushcfunction(state, &slot_set_mission_effect);
    } else if (key == "watch_damage") {
        lua_pushcfunction(state, &slot_watch_damage);
    } else if (key == "set_object_active") {
        lua_pushcfunction(state, &slot_set_object_active);
    } else if (key == "set_channel") {
        lua_pushcfunction(state, &slot_set_channel);
    } else if (key == "transition") {
        lua_pushcfunction(state, &slot_transition);
    } else if (key == "set_occupancy_condition") {
        lua_pushcfunction(state, &slot_set_occupancy_condition);
    } else if (key == "set_directive") {
        lua_pushcfunction(state, &slot_set_directive);
    } else if (key == "clear_directives") {
        lua_pushcfunction(state, &slot_clear_directives);
    } else if (key == "set_darkness_zone") {
        lua_pushcfunction(state, &slot_set_darkness_zone);
    } else if (key == "set_music_section") {
        lua_pushcfunction(state, &slot_set_music_section);
    } else if (key == "set_interactable_object") {
        lua_pushcfunction(state, &slot_set_interactable_object);
    } else if (key == "set_ghost_link") {
        lua_pushcfunction(state, &slot_set_ghost_link);
    } else if (key == "set_engagement_state") {
        lua_pushcfunction(state, &slot_set_engagement_state);
    } else if (key == "set_public_event_state") {
        lua_pushcfunction(state, &slot_set_public_event_state);
    } else if (key == "assign_combat_objective") {
        lua_pushcfunction(state, &slot_assign_combat_objective);
    } else if (key == "play_actor_path") {
        lua_pushcfunction(state, &slot_play_actor_path);
    } else if (key == "deliver_squad") {
        lua_pushcfunction(state, &slot_deliver_squad);
    } else if (key == "deliver_squads") {
        lua_pushcfunction(state, &slot_deliver_squads);
    } else if (key == "play_actor_action") {
        lua_pushcfunction(state, &slot_play_actor_action);
    } else if (key == "retire_actor") {
        lua_pushcfunction(state, &slot_retire_actor);
    } else if (key == "bind_combatant_to_squad") {
        lua_pushcfunction(state, &slot_bind_combatant_to_squad);
    } else if (key == "run_atoms") {
        lua_pushcfunction(state, &slot_run_atoms);
    } else if (key == "fire_trigger") {
        lua_pushcfunction(state, &slot_fire_trigger);
    } else if (key == "play_sequence") {
        lua_pushcfunction(state, &slot_play_sequence);
    } else if (key == "set_cinematic_active") {
        lua_pushcfunction(state, &slot_set_cinematic_active);
    } else if (key == "reset_objectives") {
        lua_pushcfunction(state, &slot_reset_objectives);
    } else if (key == "advance_task") {
        lua_pushcfunction(state, &slot_advance_task);
    } else if (key == "play_dialogue_cue") {
        lua_pushcfunction(state, &slot_play_dialogue_cue);
    } else if (key == "play_performance") {
        lua_pushcfunction(state, &slot_play_performance);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

void register_slot_metatables(lua_State* state) {
    register_metatable(state, kSlotMetatable, &slot_index);
}
} // namespace sunrise::server::activity::mission::lua_vm::detail
