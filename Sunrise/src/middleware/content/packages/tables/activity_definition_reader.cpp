#include "activity_definition_reader.h"

#include <array>
#include <limits>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/** The fixed activity index begins after the table prefix. */
constexpr std::size_t kIndexOffset = 160;
/** One index row holds a hash, zero padding, and a signed self-relative pointer. */
constexpr std::size_t kIndexStride = 16;
constexpr std::size_t kIndexPaddingOffset = 4;
constexpr std::size_t kIndexPointerOffset = 8;
constexpr std::size_t kIndexEnd = kIndexOffset + kActivityDefinitionCount * kIndexStride;
/** Every fixed field used by the inventory ends inside the first 225 record bytes. */
constexpr std::size_t kMinimumRecordSize = 225;

/** Activity record field offsets. */
constexpr std::size_t kRecordDefinitionHashOffset = 0;
constexpr std::size_t kRecordMatchmakingPointerOffset = 32;
constexpr std::size_t kRecordInternalNamePointerOffset = 104;
constexpr std::size_t kRecordRequiredLevelOffset = 176;
constexpr std::size_t kRecordRequiredPowerOffset = 180;
constexpr std::size_t kRecordRequiredLevel2Offset = 184;
constexpr std::size_t kRecordRequiredPower2Offset = 188;
constexpr std::size_t kRecordUnlockSlotOffset = 216;
constexpr std::size_t kRecordTypeIndexOffset = 218;
constexpr std::size_t kRecordGameplaySettingsHashOffset = 220;
constexpr std::size_t kRecordDestinationIndexOffset = 224;

/** The installed enums have these exclusive upper bounds. */
constexpr std::uint8_t kTypeCount = 54;
constexpr std::uint8_t kDestinationCount = 48;
/** Package tags use two 13-bit lanes above their base. */
constexpr std::uint32_t kTagBase = 0x80800000U;
constexpr std::uint64_t kTagNamespaceSize = std::uint64_t{1} << 26U;

/** One validated index row. */
struct IndexRow final {
    std::uint32_t definitionHash{};
    std::size_t recordOffset{};
};

/** Adds one signed self-relative pointer without signed overflow. */
[[nodiscard]] bool
add_relative(std::size_t member, std::int64_t relative, std::size_t& target) noexcept {
    if (relative >= 0) {
        const auto distance = static_cast<std::uint64_t>(relative);
        if (distance > (std::numeric_limits<std::size_t>::max)() - member) {
            return false;
        }
        target = member + static_cast<std::size_t>(distance);
        return true;
    }
    const auto distance = static_cast<std::uint64_t>(-(relative + 1)) + 1U;
    if (distance > member) {
        return false;
    }
    target = member - static_cast<std::size_t>(distance);
    return true;
}

/** Resolves a signed pointer and requires its target field to fit in the blob. */
[[nodiscard]] bool follow(std::span<const std::byte> blob,
                          std::size_t member,
                          std::size_t targetSize,
                          std::size_t& target) noexcept {
    std::int64_t relative = 0;
    return read(blob, member, relative) && add_relative(member, relative, target)
           && target <= blob.size() && targetSize <= blob.size() - target;
}

/** The package namespace has two 13-bit index lanes. */
[[nodiscard]] bool valid_package_tag(std::uint32_t tag) noexcept {
    return tag >= kTagBase && static_cast<std::uint64_t>(tag - kTagBase) < kTagNamespaceSize;
}

/** Reads one strict ASCII string whose NUL must occur inside the fixed scan limit. */
[[nodiscard]] bool read_internal_name(std::span<const std::byte> blob,
                                      std::size_t offset,
                                      ActivityDefinition& output) noexcept {
    if (offset >= blob.size()) {
        return false;
    }
    const std::size_t available = blob.size() - offset;
    const std::size_t limit =
        available < output.internalName.size() ? available : output.internalName.size();
    for (std::size_t length = 0; length < limit; ++length) {
        const auto value = std::to_integer<std::uint8_t>(blob[offset + length]);
        if (value == 0) {
            output.internalNameLength = static_cast<std::uint16_t>(length);
            return true;
        }
        if (value > 0x7FU) {
            return false;
        }
        output.internalName[length] = static_cast<char>(value);
    }
    return false;
}

/** Reads and validates the fixed 1,170-row index. */
[[nodiscard]] bool read_index(std::span<const std::byte> blob,
                              std::array<IndexRow, kActivityDefinitionCount>& rows) noexcept {
    std::size_t previousOffset = 0;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const std::size_t offset = kIndexOffset + index * kIndexStride;
        std::uint32_t padding = 0;
        std::int64_t relative = 0;
        std::size_t recordOffset = 0;
        if (!read(blob, offset, rows[index].definitionHash)
            || !read(blob, offset + kIndexPaddingOffset, padding) || padding != 0
            || !read(blob, offset + kIndexPointerOffset, relative)
            || !add_relative(offset + kIndexPointerOffset, relative, recordOffset)
            || recordOffset < kIndexEnd || recordOffset >= blob.size()
            || (index != 0 && recordOffset <= previousOffset)) {
            return false;
        }
        rows[index].recordOffset = recordOffset;
        previousOffset = recordOffset;
    }
    return true;
}

/** Reads one activity after the complete index has passed validation. */
[[nodiscard]] bool read_definition(std::span<const std::byte> blob,
                                   const std::array<IndexRow, kActivityDefinitionCount>& rows,
                                   std::size_t index,
                                   ActivityDefinition& output) noexcept {
    output = {};
    const IndexRow& row = rows[index];
    // The last record runs to the end of the blob; every other one ends at its successor.
    const std::size_t recordEnd =
        index + 1 < rows.size() ? rows[index + 1].recordOffset : blob.size();
    const std::size_t recordLength = recordEnd - row.recordOffset;
    std::uint32_t recordHash = 0;
    std::size_t matchmakingOffset = 0;
    std::uint32_t matchmakingTag = 0;
    std::size_t internalNameOffset = 0;
    if (recordLength < kMinimumRecordSize
        || !read(blob, row.recordOffset + kRecordDefinitionHashOffset, recordHash)
        || recordHash != row.definitionHash
        || !follow(blob,
                   row.recordOffset + kRecordMatchmakingPointerOffset,
                   sizeof matchmakingTag,
                   matchmakingOffset)
        || !read(blob, matchmakingOffset, matchmakingTag)
        || (matchmakingTag != kActivityDefinitionNoMatchmakingConfig
            && !valid_package_tag(matchmakingTag))
        || !follow(blob,
                   row.recordOffset + kRecordInternalNamePointerOffset,
                   sizeof(std::byte),
                   internalNameOffset)) {
        return false;
    }

    output.activityIndex = static_cast<std::uint32_t>(index);
    output.definitionHash = row.definitionHash;
    output.recordOffset = row.recordOffset;
    output.recordLength = recordLength;
    output.matchmakingPointerOffset = matchmakingOffset;
    output.matchmakingConfigTag = matchmakingTag;
    output.hasMatchmakingConfig = matchmakingTag != kActivityDefinitionNoMatchmakingConfig;
    output.internalNamePointerOffset = internalNameOffset;
    if (!read_internal_name(blob, internalNameOffset, output)
        || !read(blob, row.recordOffset + kRecordRequiredLevelOffset, output.requiredLevel)
        || !read(blob, row.recordOffset + kRecordRequiredPowerOffset, output.requiredPower)
        || !read(blob, row.recordOffset + kRecordRequiredLevel2Offset, output.requiredLevel2)
        || !read(blob, row.recordOffset + kRecordRequiredPower2Offset, output.requiredPower2)
        || !read(blob, row.recordOffset + kRecordUnlockSlotOffset, output.unlockSlot)
        || !read(blob, row.recordOffset + kRecordTypeIndexOffset, output.typeIndex)
        || !read(
            blob, row.recordOffset + kRecordGameplaySettingsHashOffset, output.gameplaySettingsHash)
        || !read(blob, row.recordOffset + kRecordDestinationIndexOffset, output.destinationIndex)
        || output.typeIndex >= kTypeCount || output.destinationIndex >= kDestinationCount) {
        output = {};
        return false;
    }
    return true;
}

} // namespace

/** Validates and visits the complete installed activity-definition table. */
bool visit_activity_definitions(std::span<const std::byte> blob,
                                ActivityDefinitionVisitor visitor,
                                void* context) noexcept {
    if (visitor == nullptr || blob.size() < kIndexEnd) {
        return false;
    }
    std::array<IndexRow, kActivityDefinitionCount> rows{};
    if (!read_index(blob, rows)) {
        return false;
    }

    ActivityDefinition definition{};
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (!read_definition(blob, rows, index, definition)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (!read_definition(blob, rows, index, definition) || !visitor(context, definition)) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
