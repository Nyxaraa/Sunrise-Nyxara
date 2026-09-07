#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sunrise::state::gameplay::entity_position_profiles {
using Fingerprint = std::array<std::byte, 32>;
/** Package names reserve one final null byte on disk. */
inline constexpr std::size_t kNameCapacity = 128;

/** One activity name held inline at the disk capacity. */
struct Name final {
    std::array<char, kNameCapacity> chars{};
    std::uint8_t length{};

    Name() noexcept = default;

    /** A name that does not fit stores nothing, so validation rejects the row. */
    template <typename Text>
        requires std::convertible_to<const Text&, std::string_view>
    Name(const Text& value) noexcept {
        const std::string_view text{value};
        if (text.size() < chars.size()) {
            std::copy(text.begin(), text.end(), chars.begin());
            length = static_cast<std::uint8_t>(text.size());
        }
    }

    /** @return The stored name, without the reserved terminator. */
    [[nodiscard]] std::string_view view() const noexcept {
        return {chars.data(), length};
    }

    [[nodiscard]] bool operator==(const Name&) const noexcept = default;

    /** Orders names by text so the catalogue stays binary searchable. */
    [[nodiscard]] auto operator<=>(const Name& other) const noexcept {
        return view() <=> other.view();
    }
};

struct Row final {
    Name activity;
    std::uint16_t cell{};
    std::array<std::uint8_t, 3> axisBits{};
    std::uint8_t bubble{255};
    bool operator==(const Row&) const = default;
};
using Rows = std::vector<Row>;
/** Bounds heap-owned catalogue and shared-cache scratch storage. */
inline constexpr std::size_t kMaximumRows = 65536;
/** Rejects duplicates and widths outside the native 31-bit bound. */
[[nodiscard]] bool validate(std::span<const Row> rows) noexcept;
/** Publishes only one complete, validated extraction. */
[[nodiscard]] bool publish(Rows rows, const Fingerprint& fingerprint) noexcept;
/** Checks whether the installed content already owns the published rows. */
[[nodiscard]] bool ready(const Fingerprint& fingerprint) noexcept;
/** Clears package-derived values before a new build is loaded. */
void reset() noexcept;
/** Restored rows remain unavailable until their package fingerprint is confirmed. */
[[nodiscard]] bool restore(std::span<const Row> rows, const Fingerprint& fingerprint) noexcept;
/** Confirms a restored shared-cache domain against the installed packages. */
[[nodiscard]] bool confirm(const Fingerprint& fingerprint) noexcept;
/** Reports whether validated package data is available for shared-cache publication. */
[[nodiscard]] bool available() noexcept;
/** Copies active rows and their fingerprint into shared-cache scratch. */
[[nodiscard]] bool
snapshot(std::span<Row> output, std::size_t& count, Fingerprint& fingerprint) noexcept;
/** Returns the exact map-to-scenario bubble join for one native cell. */
[[nodiscard]] bool
lookup_bubble(std::string_view activity, std::uint16_t cell, std::uint8_t& bubble) noexcept;
/** Looks up only package-validated widths for this exact activity and cell. */
[[nodiscard]] bool lookup(std::string_view activity,
                          std::uint16_t cell,
                          std::array<std::uint8_t, 3>& axisBits) noexcept;
} // namespace sunrise::state::gameplay::entity_position_profiles
