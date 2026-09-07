#include "scriptable_catalog_reference_scan.h"

#include <cstring>

#include "../../../state/build_data/scenarios/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {
namespace {

// Client references: the package class id and the fixed row stride.
constexpr std::uint32_t kClientReferenceClass = 0x80809C42U;
constexpr std::size_t kClientReferenceSize = 16;

/** Reads one bounded scalar without imposing alignment on package bytes. */
template <typename T>
[[nodiscard]] bool
read_value(std::span<const std::byte> blob, std::size_t offset, T& value) noexcept {
    value = {};
    if (offset > blob.size() || sizeof value > blob.size() - offset) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

} // namespace

/** Retains aligned ClientRef records from one reached config blob. */
void collect_typed_references(std::span<const std::byte> blob,
                              std::uint32_t configTag,
                              std::vector<RawReference>& output) {
    for (std::size_t offset = 0; offset + kClientReferenceSize <= blob.size(); offset += 4) {
        std::uint32_t classId = 0;
        std::uint32_t reserved = 0;
        RawReference row{};
        if (!read_value(blob, offset, classId) || classId != kClientReferenceClass
            || !read_value(blob, offset + 4, reserved) || reserved != 0
            || !read_value(blob, offset + 8, row.targetKey)
            || !read_value(blob, offset + 12, row.targetType)
            || !read_value(blob, offset + 14, row.targetIndex) || row.targetType == 0
            || row.targetType > state::build_data::scenarios::kMaximumSlotType) {
            continue;
        }
        row.configTag = configTag;
        row.offset = static_cast<std::uint32_t>(offset);
        output.push_back(row);
    }
}

} // namespace sunrise::client::content::activity::scriptables::internal
