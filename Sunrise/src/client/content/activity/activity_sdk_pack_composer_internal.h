#pragma once

#include <string_view>
#include <vector>

#include "activity_sdk_pack_composer.h"

namespace sunrise::client::content::activity::sdk_generation::pack_composer::detail {

/** Parent ranges derived from final global child-row indexes. */
struct PreparedRanges final {
    std::vector<format::Range> scenarioBubbles{};
    std::vector<format::Range> scenarioStates{};
    std::vector<format::Range> scenarioOccurrences{};
    std::vector<format::Range> bubbleStates{};
    std::vector<format::Range> objectSlots{};
    std::vector<format::Range> activityCapabilities{};
    std::vector<format::Range> slotCapabilities{};
    std::vector<format::Range> hostCapabilities{};
};

/** Type-erased lookup into the composer-owned final string table. */
struct StringResolver final {
    const void* context{};
    bool (*lookup)(const void*, std::string_view, format::StringRef&) noexcept {};

    [[nodiscard]] bool resolve(std::string_view value, format::StringRef& output) const noexcept {
        return context != nullptr && lookup != nullptr && lookup(context, value, output);
    }
};

/** Translates all inventory rows after the final string table is frozen. */
[[nodiscard]] bool translate_rows(const Inputs& inputs,
                                  const PreparedRanges& ranges,
                                  const StringResolver& strings,
                                  Storage& output);

/** Checks section counts fit u32 and the prepared per-parent range vectors match. */
[[nodiscard]] bool validate_storage(const Inputs& inputs,
                                    const PreparedRanges& prepared,
                                    const Storage& value) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::pack_composer::detail
