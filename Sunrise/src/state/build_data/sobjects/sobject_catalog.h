#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::state::build_data::sobjects {

/** Capacity of the package-derived table indexed by 13-bit wire target. */
inline constexpr std::size_t kDefinitionCapacity = 8192;

/** A row carrying this type code has no handler and cannot produce a schema. */
inline constexpr std::int32_t kAbsentTypeCode = -1;

/** Fields used to resolve one incident target. */
struct Definition {
    /** FNV-1 of the definition name. */
    std::uint32_t nameHash{};
    /** Packed pair selected by typeCode: record row in the low half or lore ordinal in the high. */
    std::uint32_t lane4{};
    /** Selects the payload shape. Runs -1 to 48. */
    std::int32_t typeCode{kAbsentTypeCode};

    /** @return Lane 4's low half, the record row a typeCode-10 sobject names. */
    [[nodiscard]] constexpr std::uint16_t recordRow() const noexcept {
        return static_cast<std::uint16_t>(lane4 & 0xFFFFU);
    }

    /** @return Lane 4's high half, the world-lore ordinal a type-code 2 sobject names. */
    [[nodiscard]] constexpr std::uint16_t loreObjectOrdinal() const noexcept {
        return static_cast<std::uint16_t>(lane4 >> 16U);
    }
};

/** Clears every generated row. */
void clear() noexcept;

/** @return True when the rows are dense and within capacity. */
[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

/** Replaces the whole table in one step. */
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

/** Copies every row in incident-target order. */
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

/** Finds a validated wire target by row index. */
[[nodiscard]] bool find(std::uint16_t targetIndex, Definition& definition) noexcept;

/** @return Number of installed rows. */
[[nodiscard]] std::size_t count() noexcept;

} // namespace sunrise::state::build_data::sobjects
