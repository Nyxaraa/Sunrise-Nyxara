#include <algorithm>
#include <string_view>

#include "codec.h"
namespace sunrise::state::build_data::cache::records {
/** The shared cache stores each profile in one zero-padded canonical record. */
bool encode(const gameplay::entity_position_profiles::Row& value,
            PositionProfileRecord& record) noexcept {
    record = {};
    if (!gameplay::entity_position_profiles::validate(std::span(&value, 1))) {
        return false;
    }
    const std::string_view activity = value.activity.view();
    std::copy(activity.begin(), activity.end(), record.activity.begin());
    record.nameLength = value.activity.length;
    record.cell = value.cell;
    record.bubble = value.bubble;
    record.axisBits = value.axisBits;
    return true;
}
/** Malformed padding and invalid native widths never reach the published catalogue. */
bool decode(const PositionProfileRecord& record,
            gameplay::entity_position_profiles::Row& value) noexcept {
    value = {};
    if (record.reserved || record.nameLength == 0 || record.nameLength >= record.activity.size()) {
        return false;
    }
    const auto end = record.activity.begin() + record.nameLength;
    if (std::find(record.activity.begin(), end, '\0') != end
        || !std::all_of(end, record.activity.end(), [](char byte) { return byte == 0; })) {
        return false;
    }
    value.activity = std::string_view(record.activity.data(), record.nameLength);
    value.cell = record.cell;
    value.bubble = record.bubble;
    value.axisBits = record.axisBits;
    return gameplay::entity_position_profiles::validate(std::span(&value, 1));
}
/** Only validated package-class rows enter the shared payload. */
bool encode(const gameplay::entity_object_types::Row& value, ObjectTypeRecord& record) noexcept {
    record = {};
    if (!gameplay::entity_object_types::validate(std::span(&value, 1))) {
        return false;
    }
    record.rsatTag = value.rsatTag;
    record.definitionTag = value.definitionTag;
    record.objectType = value.objectType;
    return true;
}
/** Canonical padding and native type bounds apply on every cache read. */
bool decode(const ObjectTypeRecord& record, gameplay::entity_object_types::Row& value) noexcept {
    value = {};
    if (!std::all_of(record.reserved.begin(), record.reserved.end(), [](auto byte) {
            return byte == std::byte{};
        })) {
        return false;
    }
    const gameplay::entity_object_types::Row row{
        record.rsatTag, record.definitionTag, record.objectType};
    if (!gameplay::entity_object_types::validate(std::span(&row, 1))) {
        return false;
    }
    value = row;
    return true;
}
} // namespace sunrise::state::build_data::cache::records
