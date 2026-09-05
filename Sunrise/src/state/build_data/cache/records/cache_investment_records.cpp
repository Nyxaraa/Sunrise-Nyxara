#include "codec.h"

namespace sunrise::state::build_data::cache::records {
namespace {

/** Cache padding fields are always written as zero. */
constexpr unsigned int kReservedFieldValue = 0;

} // namespace

/** Flattens the 12 buckets into 3 parallel arrays with no per-bucket padding. */
bool encode(const abilities::Definition& value, AbilityBucketRecord& record) noexcept {
    record = {};
    record.socketEntryListIndex = value.socketEntryListIndex;
    record.movementEntry = value.selection.movementEntry;
    record.grenadeEntry = value.selection.grenadeEntry;
    record.superEntry = value.selection.superEntry;
    record.meleeEntry = value.selection.meleeEntry;
    record.classEntry = value.selection.classEntry;
    record.overflowCount = value.overflowCount;
    record.overflow = value.overflow;
    for (std::size_t bucket = 0; bucket < abilities::kBucketCapacity; ++bucket) {
        record.bucketKinds[bucket] = value.buckets[bucket].kind;
        record.bucketHashCounts[bucket] = value.buckets[bucket].hashCount;
        for (std::size_t entry = 0; entry < abilities::kBucketHashCapacity; ++entry) {
            record.bucketHashes[bucket * abilities::kBucketHashCapacity + entry] =
                value.buckets[bucket].hashes[entry];
        }
    }
    return true;
}

/** Rebuilds the 12 buckets from the flat disk arrays. */
bool decode(const AbilityBucketRecord& record, abilities::Definition& value) noexcept {
    value = {};
    if (record.overflowCount > abilities::kOverflowCapacity) {
        return false;
    }
    value.socketEntryListIndex = record.socketEntryListIndex;
    value.selection.movementEntry = record.movementEntry;
    value.selection.grenadeEntry = record.grenadeEntry;
    value.selection.superEntry = record.superEntry;
    value.selection.meleeEntry = record.meleeEntry;
    value.selection.classEntry = record.classEntry;
    value.overflowCount = record.overflowCount;
    value.overflow = record.overflow;
    for (std::size_t bucket = 0; bucket < abilities::kBucketCapacity; ++bucket) {
        if (record.bucketHashCounts[bucket] > abilities::kBucketHashCapacity) {
            return false;
        }
        value.buckets[bucket].kind = record.bucketKinds[bucket];
        value.buckets[bucket].hashCount = record.bucketHashCounts[bucket];
        for (std::size_t entry = 0; entry < abilities::kBucketHashCapacity; ++entry) {
            value.buckets[bucket].hashes[entry] =
                record.bucketHashes[bucket * abilities::kBucketHashCapacity + entry];
        }
    }
    return true;
}

/** Encodes one progression definition with its padding zeroed. */
bool encode(const progressions::Definition& value, ProgressionRecord& record) noexcept {
    record = {
        value.definitionIndex,
        static_cast<std::uint8_t>(value.scope),
        kReservedFieldValue,
    };
    return true;
}

/** Decodes one progression definition after checking its padding. */
bool decode(const ProgressionRecord& record, progressions::Definition& value) noexcept {
    value = {};
    if (record.reserved != kReservedFieldValue) {
        return false;
    }
    value = {record.definitionIndex, static_cast<progressions::Scope>(record.scope)};
    return true;
}

/** Encodes one record with its padding zeroed. */
bool encode(const build_data::records::Definition& value, RecordDefinitionRecord& record) noexcept {
    record = {
        value.definitionIndex,
        value.definitionHash,
        value.completionFlagIndex,
        value.loreRow,
        value.scoreValue,
        value.categoryValueIndex,
        static_cast<std::uint8_t>(value.hasTitle),
        0,
    };
    return true;
}

/** Decodes one record after checking its padding. */
bool decode(const RecordDefinitionRecord& record, build_data::records::Definition& value) noexcept {
    value = {};
    // Assigned by name, never positionally: this row outlived two field additions to Definition,
    // and a positional list silently filled the new fields with the wrong disk values.
    value.definitionIndex = record.definitionIndex;
    value.definitionHash = record.definitionHash;
    value.completionFlagIndex = record.completionFlagIndex;
    value.loreRow = record.loreRow;
    value.scoreValue = record.scoreValue;
    value.categoryValueIndex = record.categoryValueIndex;
    if (record.hasTitle > 1 || record.reserved != 0) {
        return false;
    }
    value.hasTitle = record.hasTitle != 0;
    return true;
}

/** Encodes one presentation node with canonical unused child rows. */
bool encode(const nodes::Definition& value, NodeDefinitionRecord& record) noexcept {
    if (value.childCount > nodes::kChildCapacity) {
        return false;
    }
    record = {};
    record.definitionIndex = value.definitionIndex;
    record.valueIndex = value.valueIndex;
    record.valueSlot = value.valueSlot;
    record.characterValueSlot = value.characterValueSlot;
    record.parentValueIndex = value.parentValueIndex;
    record.parentCharacterValueIndex = value.parentCharacterValueIndex;
    record.visibilityFlagIndex = value.visibilityFlagIndex;
    record.visibilityCharacterFlagIndex = value.visibilityCharacterFlagIndex;
    record.characterValueIndex = value.characterValueIndex;
    record.childCount = value.childCount;
    for (std::size_t index = 0; index < value.childCount; ++index) {
        record.children[index] = value.children[index];
    }
    return true;
}

/** Decodes one presentation node after checking its child list. */
bool decode(const NodeDefinitionRecord& record, nodes::Definition& value) noexcept {
    value = {};
    if (record.childCount > nodes::kChildCapacity) {
        return false;
    }
    for (std::size_t index = record.childCount; index < record.children.size(); ++index) {
        if (record.children[index] != 0) {
            return false;
        }
    }
    value.definitionIndex = record.definitionIndex;
    value.valueIndex = record.valueIndex;
    value.valueSlot = record.valueSlot;
    value.characterValueSlot = record.characterValueSlot;
    value.parentValueIndex = record.parentValueIndex;
    value.parentCharacterValueIndex = record.parentCharacterValueIndex;
    value.visibilityFlagIndex = record.visibilityFlagIndex;
    value.visibilityCharacterFlagIndex = record.visibilityCharacterFlagIndex;
    value.characterValueIndex = record.characterValueIndex;
    value.childCount = record.childCount;
    value.children = record.children;
    return true;
}

/** Encodes one incident-target definition. */
bool encode(const sobjects::Definition& value, SObjectDefinitionRecord& record) noexcept {
    record = {value.nameHash, value.lane4, value.typeCode};
    return true;
}

/** Decodes one incident-target definition. */
bool decode(const SObjectDefinitionRecord& record, sobjects::Definition& value) noexcept {
    value = {record.nameHash, record.lane4, record.typeCode};
    return true;
}

} // namespace sunrise::state::build_data::cache::records
