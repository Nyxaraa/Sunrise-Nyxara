#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../../middleware/bap/activity_message/scriptable_auth_body.h"
#include "mission_script_lua_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

namespace scriptable_auth = middleware::bap::activity_message::scriptable_auth;

namespace {

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
    // A client reference carries the slot index as a signed 16-bit lane.
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
} // namespace

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
    // Named arguments this call accepts. Any other key is refused.
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

} // namespace sunrise::server::activity::mission::lua_vm::detail
