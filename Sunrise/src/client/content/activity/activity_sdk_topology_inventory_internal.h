#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <vector>

#include "activity_sdk_topology_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::topology_inventory::detail {

namespace catalog = state::build_data::scriptables;
namespace format = state::activity_sdk::format;

/** This slice proves the root and scenario joins before scenario extraction closes. */
inline constexpr std::uint32_t kActivityTopologyJoinMask =
    format::kActivityRootExact | format::kActivityScenarioExact;
/** This slice cannot claim the later content join required by the runtime. */
inline constexpr std::uint32_t kActivityTopologyReadyMask =
    kActivityTopologyJoinMask | format::kActivityExtractionPresent;
static_assert(kActivityTopologyReadyMask != format::kActivityExactMask);

/** Formats one canonical structural ID into owned pre-string-table storage. */
template <typename... Arguments>
[[nodiscard]] bool format_text(Text& output, const char* pattern, Arguments... arguments) noexcept {
    output = {};
    const int length =
        std::snprintf(output.value.data(), output.value.size(), pattern, arguments...);
    if (length < 0 || static_cast<std::size_t>(length) >= output.value.size()
        || static_cast<unsigned>(length) > (std::numeric_limits<std::uint16_t>::max)()) {
        output = {};
        return false;
    }
    output.length = static_cast<std::uint16_t>(length);
    return true;
}

[[nodiscard]] bool text_view(const Text& source, std::string_view& output) noexcept;
[[nodiscard]] bool byte_less(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] bool valid_utf8(std::string_view value) noexcept;
[[nodiscard]] bool valid_registry_field(std::uint32_t value) noexcept;
[[nodiscard]] bool build_object(const catalog::Snapshot& source,
                                std::uint32_t sourceObjectRow,
                                std::size_t& nextSlot,
                                std::size_t& nextDescriptor,
                                Object& output);
[[nodiscard]] bool merge_object(Object input, Object& output);
[[nodiscard]] bool merge_object(Object input, std::vector<Object>& output);
[[nodiscard]] bool build_occurrence(const catalog::Object& input,
                                    const State& state,
                                    std::uint32_t scenarioTag,
                                    std::uint32_t scenarioIndex,
                                    std::uint32_t bubbleIndex,
                                    std::uint32_t stateIndex,
                                    Occurrence& output) noexcept;
[[nodiscard]] bool valid_open_prefix(const Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::topology_inventory::detail
