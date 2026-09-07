#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mission_script_sdk_bridge.h"
#include "mission_script_world_sdk.h"

namespace sunrise::server::activity::mission::sdk_bridge {

// Value constructors for one generated-world row field.

/** Stores one unsigned value under the caller's Lua shape. */
[[nodiscard]] bool
field_u64(lua_vm::WorldFieldDefinition& output,
          std::uint64_t value,
          lua_vm::WorldFieldKind kind = lua_vm::WorldFieldKind::unsignedInteger) noexcept;

/** Stores one signed value under the caller's Lua shape. */
[[nodiscard]] bool
field_i64(lua_vm::WorldFieldDefinition& output,
          std::int64_t value,
          lua_vm::WorldFieldKind kind = lua_vm::WorldFieldKind::signedInteger) noexcept;

[[nodiscard]] bool field_bool(lua_vm::WorldFieldDefinition& output, bool value) noexcept;

/**
 * Stores one borrowed string.
 * @param optional Publishes nil instead of an empty string.
 */
[[nodiscard]] bool field_string(lua_vm::WorldFieldDefinition& output,
                                std::string_view value,
                                bool optional = false) noexcept;

/** Stores one zero-based row as its one-based Lua row, or nil when the row is absent. */
[[nodiscard]] bool field_row(lua_vm::WorldFieldDefinition& output,
                             std::uint32_t zeroBased) noexcept;

/** Stores the first row of a range, or nil when the range is empty. */
[[nodiscard]] bool field_first_row(lua_vm::WorldFieldDefinition& output,
                                   std::uint32_t zeroBased,
                                   std::uint32_t count) noexcept;

/** Stores one to four lanes of a stored vector. */
template <std::size_t N>
[[nodiscard]] bool field_vector(lua_vm::WorldFieldDefinition& output,
                                const std::array<float, N>& value) noexcept {
    static_assert(N > 0 && N <= 4);
    output = {};
    output.kind = lua_vm::WorldFieldKind::vector;
    std::copy(value.begin(), value.end(), output.vectorValue.begin());
    output.valueCount = static_cast<std::uint8_t>(N);
    return true;
}

/** Stores one fixed byte mask up to the 32-byte field capacity. */
template <typename T, std::size_t N>
[[nodiscard]] bool field_bytes(lua_vm::WorldFieldDefinition& output,
                               const std::array<T, N>& value) noexcept {
    static_assert(N <= 32 && (std::same_as<T, std::byte> || std::same_as<T, std::uint8_t>));
    output = {};
    output.kind = lua_vm::WorldFieldKind::bytes;
    for (std::size_t index = 0; index < N; ++index) {
        output.bytesValue[index] = static_cast<std::byte>(value[index]);
    }
    output.valueCount = static_cast<std::uint8_t>(N);
    return true;
}

/** Converts one stored zero-based row to its one-based Lua row, or zero when absent. */
[[nodiscard]] std::uint32_t one_based(std::uint32_t row) noexcept;

// Names for the closed enumerations stored in generated-world rows. Each returns nullptr for a
// value outside its enumeration.

[[nodiscard]] const char* provenance_text(std::uint32_t value) noexcept;
[[nodiscard]] const char* safety_text(state::build_data::scriptables::GroupSafety value) noexcept;
[[nodiscard]] const char*
reference_join_text(state::build_data::scriptables::ReferenceJoin value) noexcept;
[[nodiscard]] const char*
spatial_context_text(state::build_data::scriptables::SpatialContextJoin value) noexcept;
[[nodiscard]] const char* point_context_status_text(
    state::build_data::scriptables::AuthoredSquadPointContextStatus value) noexcept;

// Selected package names carried by most generated-world rows.

/** Copies one selected name while retaining its exact evidence tier and source. */
[[nodiscard]] lua_vm::WorldNameDefinition
selected_name(const state::build_data::scriptables::Snapshot& snapshot,
              std::uint32_t row,
              bool tagName) noexcept;

/**
 * Resolves the selected-name evidence suffixes shared by all catalog rows.
 * @param prefix Row-specific key prefix, such as "selected" or "container".
 * @return False when the key does not carry that prefix and a known suffix.
 */
[[nodiscard]] bool selected_name_field(const state::build_data::scriptables::Snapshot& snapshot,
                                       std::uint32_t row,
                                       bool tagName,
                                       std::string_view key,
                                       std::string_view prefix,
                                       lua_vm::WorldFieldDefinition& output) noexcept;

/** Returns the bound world view only while every generation digest still matches. */
[[nodiscard]] const state::activity_sdk::generated_world::GeneratedWorldView*
checked_world(const void* context, const lua_vm::WorldGenerationIdentity& generation) noexcept;

} // namespace sunrise::server::activity::mission::sdk_bridge
