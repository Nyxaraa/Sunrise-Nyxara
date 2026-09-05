#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "settings.h"

namespace sunrise::core::settings::parser {

/**
 * Fixed-storage JSON reader for the supported Core settings.
 * Every parse below writes `output` only once its whole object is valid, and skips unknown keys.
 * A false return leaves `output` as the caller left it.
 */
class Parser {
public:
    /** @param input Complete JSON text, borrowed and never changed. */
    explicit Parser(std::string_view input) noexcept;

    /** Parses the root object on top of the caller's defaults. */
    [[nodiscard]] bool parse_root(Settings& output) noexcept;

private:
    /** Parses the Core settings object. */
    [[nodiscard]] bool core(Settings& output) noexcept;
    /** Parses the activity SDK generation boot gate. `enabled` must be unique and boolean. */
    [[nodiscard]] bool
    activity_sdk_generation_settings(ActivitySdkGenerationSettings& output) noexcept;
    /** Parses Client settings. Each supported object may appear at most once. */
    [[nodiscard]] bool client_settings(client::Settings& output) noexcept;
    /** Parses the in-game UI boot and input policy. Keys must name a Windows key. */
    [[nodiscard]] bool client_ui_settings(ui::runtime::Settings& output) noexcept;
    /** Parses the external-server block. Each supported key may appear at most once. */
    [[nodiscard]] bool client_external_settings(client::external::Settings& output) noexcept;
    /** Parses the standalone Server settings object. */
    [[nodiscard]] bool server_settings(server::Settings& output) noexcept;
    /** Parses the activation gate block. Every supported key carries a boolean. */
    [[nodiscard]] bool activation_settings(server::activation::Settings& output) noexcept;
    /** Parses the gameplay endpoint block. Topology, addresses, port and reserve must agree. */
    [[nodiscard]] bool gameplay_settings(server::gameplay::Settings& output) noexcept;
    /**
     * Parses the authored entitlement array, replacing the bundled policy.
     * Array order is the handle order the Client finds definitions by, so rows are kept as
     * configured.
     */
    [[nodiscard]] bool entitlements(state::entitlements::Table& output) noexcept;
    /** Parses one entitlement. It needs a bounded name and a supported ownership form. */
    [[nodiscard]] bool entitlement(state::entitlements::Entitlement& output) noexcept;
    /** Parses Steam settings. Each supported object may appear at most once. */
    [[nodiscard]] bool steam_settings(steam::Settings& output) noexcept;
    /** Parses the single local Steam user. Its persona must be unique and bounded. */
    [[nodiscard]] bool steam_user_settings(steam::User& output) noexcept;
    /** Parses the State settings object. */
    [[nodiscard]] bool state_settings(Settings& output) noexcept;
    /** Parses the activity settings object on top of the State defaults. */
    [[nodiscard]] bool
    activity_settings(state::activity::defaults::ActivityDefaults& output) noexcept;
    /** Parses one local destination and its launch policy. Every required field appears once. */
    [[nodiscard]] bool
    default_destination(state::activity::defaults::DefaultDestination& output) noexcept;
    /** Parses one arrival override row. It must name a destination and at least one value. */
    [[nodiscard]] bool
    arrival_override(state::activity::defaults::ArrivalOverride& output) noexcept;
    /** Parses the arrival override table, which has to fit fixed storage. */
    [[nodiscard]] bool
    arrival_overrides(state::activity::defaults::ActivityDefaults& output) noexcept;
    /** Parses the unlock table. */
    [[nodiscard]] bool unlocks(state::unlocks::Table& output) noexcept;
    /** Fills the flag bank from authored runs. */
    [[nodiscard]] bool flag_runs(std::span<std::uint8_t> bank) noexcept;
    /** Fills the flag bank from authored indices. */
    [[nodiscard]] bool flag_indices(std::span<std::uint8_t> bank) noexcept;
    /** Fills the objective bank from authored signed values. */
    [[nodiscard]] bool objective_values(std::span<std::int32_t> bank) noexcept;
    /** Fills the progression bank from authored values. */
    [[nodiscard]] bool progression_values(state::unlocks::ProgressionBank& bank) noexcept;
    /** Parses the family-5 override group. Id and gate fields stay default. */
    [[nodiscard]] bool investment(state::Family5State& output) noexcept;
    /** Fills the flag-override list from [slot, value] pairs, each within its bounds. */
    [[nodiscard]] bool unlock_flag_overrides(state::Family5State& output) noexcept;
    /** Fills the signed value-override list from [slot, value] pairs, each within its bounds. */
    [[nodiscard]] bool unlock_value_overrides(state::Family5State& output) noexcept;
    /** Parses the account object. */
    [[nodiscard]] bool account(state::AccountState& output) noexcept;
    /** Parses the character array, in configuration order, up to the playable size. */
    [[nodiscard]] bool characters(state::AccountState& output) noexcept;
    /**
     * Parses the account-wide item array, in configuration order.
     * A profile item names only its definition and quantity. Its slot is not authored: the
     * inventory bucket the definition belongs to names the first slot of that bucket's run.
     */
    [[nodiscard]] bool profile_items(state::AccountState& output) noexcept;
    /** Parses the definition-driven ordinary-gear dismantle payout. */
    [[nodiscard]] bool dismantle_rewards(state::AccountState& output) noexcept;
    /** Parses Triumph reward overrides keyed by record index. */
    [[nodiscard]] bool record_rewards(state::AccountState& output) noexcept;
    /** Parses one character identity. The object must contain one nonzero SOID. */
    [[nodiscard]] bool character(state::CharacterState& output) noexcept;

    /** Parses the equipment object. Every slot name must be known and appear at most once. */
    [[nodiscard]] bool equipment(state::account::inventory::Equipment& output) noexcept;
    /** Parses the unequipped inventory array, in authored bucket-placement order. */
    [[nodiscard]] bool
    character_inventory(state::account::inventory::CharacterItems& output) noexcept;
    /** Parses one item. Every required named field must appear exactly once. */
    [[nodiscard]] bool equipment_item(state::account::inventory::Item& output) noexcept;
    /** Parses the socket policy: null for the native default, or up to 12 hash-or-null lanes. */
    [[nodiscard]] bool equipment_plugs(state::account::inventory::Sockets& output) noexcept;
    /** Parses one definition hash: any 32-bit value except the engine no-definition sentinel. */
    [[nodiscard]] bool inventory_definition_hash(std::uint32_t& output) noexcept;
    /** Parses the grouped account settings. Its migration latch has to be complete. */
    [[nodiscard]] bool account_settings(state::account::settings::AccountSettings& output) noexcept;

    // --- Account setting groups ---------------------------------------------------------------
    // Each takes stable Sunrise-owned names and writes bounded native value types. No group
    // exposes a record offset.

    /** Parses controller and mouse settings. */
    [[nodiscard]] bool controls_settings(state::account::settings::Controls& output) noexcept;
    /** Parses voice, volume and migration settings. */
    [[nodiscard]] bool audio_settings(state::account::settings::Audio& output) noexcept;
    /** Parses screen and renderer settings. */
    [[nodiscard]] bool display_settings(state::account::settings::Display& output) noexcept;
    /** Parses HUD and text settings. */
    [[nodiscard]] bool interface_settings(state::account::settings::Interface& output) noexcept;
    /** Parses matchmaking and chat settings. */
    [[nodiscard]] bool social_settings(state::account::settings::Social& output) noexcept;

    /** Parses the action table. Every action appears once, with both input halves. */
    [[nodiscard]] bool
    key_bindings(state::account::settings::bindings::KeyBindings& output) noexcept;
    /** Parses one binding. Both halves appear exactly once; null means unbound. */
    [[nodiscard]] bool key_binding(state::account::settings::bindings::Binding& output) noexcept;
    /** Parses one input name, or null for unbound. Numbers are not accepted. */
    [[nodiscard]] bool optional_input_code(std::optional<std::uint16_t>& output) noexcept;
    /**
     * Turns one input name into its input code.
     * @param name Key name, or one modifier and the key it prefixes joined by "+".
     */
    [[nodiscard]] static bool input_code_value(std::string_view name,
                                               std::uint16_t& output) noexcept;
    /** Parses logging sinks and channel levels. */
    [[nodiscard]] bool logging(log::Settings& output) noexcept;
    /** Parses named channel levels and ignores unknown channels. */
    [[nodiscard]] bool levels(log::Settings& output) noexcept;

    // --- JSON scanning ---------------------------------------------------------------------
    // These read one token each and never allocate.

    /** Checks and skips one value of any type. @param depth Current nesting depth. */
    [[nodiscard]] bool skip_value(unsigned depth) noexcept;
    /** Reads one string. @param output Receives the borrowed bytes between the quotes. */
    [[nodiscard]] bool string(std::string_view& output) noexcept;
    /** Reads a true or false literal. */
    [[nodiscard]] bool boolean(bool& output) noexcept;
    /** Reads one unsigned decimal integer with no precision loss. */
    [[nodiscard]] bool unsigned_integer(std::uint64_t& output) noexcept;
    /** Reads an unsigned id as an integer or a quoted hex token. */
    [[nodiscard]] bool unsigned_value(std::uint64_t& output) noexcept;
    /** Reads one signed integer. Fractions and exponents are refused. */
    [[nodiscard]] bool signed_integer(std::int64_t& output) noexcept;
    /** Reads one signed integer that has to fit a native signed byte. */
    [[nodiscard]] bool signed_byte(std::int8_t& output) noexcept;
    /** Reads one signed integer that has to fit a native signed 32-bit value. */
    [[nodiscard]] bool signed_32(std::int32_t& output) noexcept;
    /** Reads one finite number that has to fit a float. */
    [[nodiscard]] bool floating_point(float& output) noexcept;
    /** Reads one complete number. */
    [[nodiscard]] bool number() noexcept;
    /** Reads one exact literal after leading whitespace. @param value Literal bytes to match. */
    [[nodiscard]] bool literal(std::string_view value) noexcept;
    /** Reads one expected structural character after whitespace. */
    [[nodiscard]] bool consume(char value) noexcept;
    /** @return True when only trailing whitespace remains. */
    [[nodiscard]] bool at_end() noexcept;
    /** Skips JSON whitespace. Other control bytes are left alone. */
    void whitespace() noexcept;

    /** Turns a level token into the logging enum. */
    [[nodiscard]] static bool level_value(std::string_view name, log::Level& output) noexcept;
    /** @return Channel index for the token, or Channel::count when it names none. */
    [[nodiscard]] static std::size_t channel_index(std::string_view name) noexcept;
    /** Maps one settings key name to a Windows SDK virtual key from the supported menu set. */
    [[nodiscard]] static bool ui_toggle_key_value(std::string_view name, UINT& output) noexcept;

    std::string_view input_;
    std::size_t position_{};
};

} // namespace sunrise::core::settings::parser
