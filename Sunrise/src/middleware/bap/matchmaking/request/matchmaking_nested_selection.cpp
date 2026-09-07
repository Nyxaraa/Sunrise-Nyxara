#include <array>
#include <limits>

#include "field_selection.h"
#include "internal.h"

namespace sunrise::middleware::bap::matchmaking::request {
namespace {

using protobuf::WireType;

/** Fields inside the kind-4 activity message. Both are 32-bit hashes. */
enum class ActivityField : std::uint32_t {
    definitionHash = 1,
    typeHash = 2,
};

/** A hash field is one 32-bit value carried as a varint. */
constexpr std::uint64_t kMaximumHash = (std::numeric_limits<std::uint32_t>::max)();

/** Fields inside the root advertisement message. */
enum class AdvertisementField : std::uint32_t {
    /** Optional existing advertisement id. */
    existingId = 1,
    /** Advertisement record with the stable key and descriptor. */
    record = 2,
};

/** Fields inside the advertisement record. */
enum class RecordField : std::uint32_t {
    /** Descriptor wrapper. It carries the whole opaque join descriptor. */
    descriptor = 2,
    /** Stable advertisement variant key. */
    variantKey = 4,
};

/** The descriptor wrapper carries its opaque bytes in field one. */
constexpr std::uint32_t kDescriptorBytesField = 1;

/** Index of each pick in the advertisement selection array. */
constexpr std::size_t kExistingIdPick = 0;
constexpr std::size_t kRecordPick = 1;
constexpr std::size_t kAdvertisementPickCount = 2;
/** Index of each pick in the record selection array. */
constexpr std::size_t kDescriptorPick = 0;
constexpr std::size_t kVariantKeyPick = 1;
constexpr std::size_t kRecordPickCount = 2;
/** The descriptor wrapper has a single pick. */
constexpr std::size_t kDescriptorBytesPick = 0;
constexpr std::size_t kDescriptorPickCount = 1;
/** Index of each pick in the activity selection array. */
constexpr std::size_t kDefinitionHashPick = 0;
constexpr std::size_t kTypeHashPick = 1;
constexpr std::size_t kActivityPickCount = 2;

} // namespace

/** Parses the kind-2 path into borrowed request fields. */
bool parse_update(std::span<const std::byte> input, AdvertisementUpdate& update) noexcept {
    update = {};
    std::array<FieldPick, kAdvertisementPickCount> advertisement{
        FieldPick{static_cast<std::uint32_t>(AdvertisementField::existingId), WireType::varint},
        FieldPick{static_cast<std::uint32_t>(AdvertisementField::record),
                  WireType::lengthDelimited},
    };
    if (!select_first_fields(input, advertisement)) {
        return false;
    }
    update.hasExistingId = advertisement[kExistingIdPick].has;
    update.existingId = advertisement[kExistingIdPick].value;
    if (!advertisement[kRecordPick].has) {
        return true;
    }

    // Only the submessages we pick are read further. Other byte fields stay opaque.
    std::array<FieldPick, kRecordPickCount> record{
        FieldPick{static_cast<std::uint32_t>(RecordField::descriptor), WireType::lengthDelimited},
        FieldPick{static_cast<std::uint32_t>(RecordField::variantKey), WireType::varint},
    };
    if (!select_first_fields(advertisement[kRecordPick].bytes, record)) {
        return false;
    }
    if (record[kVariantKeyPick].has) {
        update.variantKey = record[kVariantKeyPick].value;
    }
    if (!record[kDescriptorPick].has) {
        return true;
    }

    std::array<FieldPick, kDescriptorPickCount> descriptor{
        FieldPick{kDescriptorBytesField, WireType::lengthDelimited},
    };
    if (!select_first_fields(record[kDescriptorPick].bytes, descriptor)) {
        return false;
    }
    // A wrapper that carried bytes must carry the whole fixed-width descriptor.
    if (descriptor[kDescriptorBytesPick].has
        && descriptor[kDescriptorBytesPick].bytes.size() != kJoinDescriptorSize) {
        return false;
    }
    update.hasDescriptor = descriptor[kDescriptorBytesPick].has;
    update.descriptor = descriptor[kDescriptorBytesPick].bytes;
    return true;
}

/** Parses the kind-4 activity submessage into its two hashes. */
bool parse_activity(std::span<const std::byte> input, ConfigurationActivity& activity) noexcept {
    activity = {};
    std::array<FieldPick, kActivityPickCount> picks{
        FieldPick{static_cast<std::uint32_t>(ActivityField::definitionHash), WireType::varint},
        FieldPick{static_cast<std::uint32_t>(ActivityField::typeHash), WireType::varint},
    };
    if (!select_first_fields(input, picks)) {
        return false;
    }
    if (picks[kDefinitionHashPick].has && picks[kDefinitionHashPick].value <= kMaximumHash) {
        activity.hasDefinitionHash = true;
        activity.definitionHash = static_cast<std::uint32_t>(picks[kDefinitionHashPick].value);
    }
    if (picks[kTypeHashPick].has && picks[kTypeHashPick].value <= kMaximumHash) {
        activity.hasTypeHash = true;
        activity.typeHash = static_cast<std::uint32_t>(picks[kTypeHashPick].value);
    }
    return true;
}

} // namespace sunrise::middleware::bap::matchmaking::request
