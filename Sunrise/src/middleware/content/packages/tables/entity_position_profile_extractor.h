#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "../../../../state/gameplay/external/entity_position_profiles.h"
#include "../named_tags.h"
namespace sunrise::middleware::content::packages::position_profiles {
/** One named package definition. Names use the bounded storage the extractor already produces. */
struct NamedTag final {
    NamedTag() noexcept = default;
    /**
     * @param text Definition name. A name over the capacity leaves this row unnamed.
     * @param tagValue Package tag of the definition.
     * @param classValue Package class of the definition.
     * @param base True when the definition came from a base package.
     */
    NamedTag(std::string_view text,
             std::uint32_t tagValue,
             std::uint32_t classValue,
             bool base) noexcept;
    /** @return The stored name, empty when none fit. */
    [[nodiscard]] std::string_view text() const noexcept;

    std::array<char, named_tags::kNameCapacity> name{};
    std::size_t nameLength{};
    std::uint32_t tag{}, classId{};
    bool basePackage{};
};
struct KeyTag final {
    std::uint64_t key{};
    std::uint32_t tag{}, classId{};
};
using Read = bool (*)(void*, std::uint32_t, std::uint32_t, std::vector<std::byte>&) noexcept;
/** Reads the package array marker, element class, and full extent. */
[[nodiscard]] bool array(std::span<const std::byte> bytes,
                         std::size_t field,
                         std::size_t stride,
                         std::uint32_t elementClass,
                         std::vector<std::size_t>& offsets) noexcept;
/** Joins exact scenario cell owners to complete package bounds. */
[[nodiscard]] bool extract(std::span<const NamedTag> names,
                           std::span<const KeyTag> keys,
                           Read read,
                           void* context,
                           state::gameplay::entity_position_profiles::Rows& rows) noexcept;
} // namespace sunrise::middleware::content::packages::position_profiles
