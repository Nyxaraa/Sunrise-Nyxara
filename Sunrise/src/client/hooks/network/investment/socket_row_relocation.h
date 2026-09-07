#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace sunrise::client::hooks::network::investment::relocation {

/** Class marker every native content array and condition record starts with. */
inline constexpr std::uint32_t kMarker = 0x80809FBDU;
/** Element class the plug-set member array declares. */
inline constexpr std::uint32_t kMemberClass = 0x80802E03U;
/** Element class one member's own condition record declares. */
inline constexpr std::uint32_t kConditionClass = 0x80807D31U;
/** Member rows start here; the array header occupies everything before it. */
inline constexpr std::size_t kDataOffset = 24;
/** The largest member array this build relocates. */
inline constexpr std::size_t kMaximumMembers = 56;
/** One member row is 32 bytes. */
inline constexpr std::size_t kMemberSize = 32;
/** One condition record is 32 bytes. */
inline constexpr std::size_t kConditionSize = 32;

/** Array header fields, as byte offsets from the array start. */
inline constexpr std::size_t kArrayMarkerOffset = 4;
inline constexpr std::size_t kArrayCountOffset = 8;
inline constexpr std::size_t kArrayClassOffset = 16;

/** Member row fields, as byte offsets from the row start. */
inline constexpr std::size_t kMemberConditionCountOffset = 8;
inline constexpr std::size_t kMemberConditionRelativeOffset = 16;

/** Condition record fields, as byte offsets from the record start. */
inline constexpr std::size_t kConditionCountOffset = 4;
inline constexpr std::size_t kConditionClassOffset = 12;
inline constexpr std::size_t kConditionReservedOffset = 16;
/** A member's relative reference names this offset inside its condition record. */
inline constexpr std::size_t kConditionReferenceOffset = 4;

/** The native reader dereferences these records as 8-byte aligned. */
inline constexpr std::size_t kRecordAlignment = 8;

/** A captured member and its own opaque, single-record condition allocation. */
struct Row {
    std::array<std::byte, kMemberSize> bytes{};
    std::array<std::byte, kConditionSize> condition{};
};

template <typename T> T get(const std::byte* p) noexcept {
    T value{};
    std::memcpy(&value, p, sizeof value);
    return value;
}

template <typename T> void put(std::byte* p, T value) noexcept {
    std::memcpy(p, &value, sizeof value);
}

/** @return True for a row with no condition, or with exactly one record of the expected class. */
inline bool valid(const Row& row) noexcept {
    const auto count = get<std::uint64_t>(row.bytes.data() + kMemberConditionCountOffset);
    if (count == 0) {
        return get<std::int64_t>(row.bytes.data() + kMemberConditionRelativeOffset) == 0;
    }
    return count == 1 && get<std::uint32_t>(row.condition.data()) == kMarker
           && get<std::uint64_t>(row.condition.data() + kConditionCountOffset) == 1
           && get<std::uint32_t>(row.condition.data() + kConditionClassOffset) == kConditionClass
           && get<std::uint32_t>(row.condition.data() + kConditionReservedOffset) == 0;
}

/** @return Bytes one relocated array of that many rows needs, condition storage included. */
inline std::size_t capacity(std::size_t count) noexcept {
    return kDataOffset + count * (kMemberSize + kConditionSize) + kRecordAlignment;
}

/** Builds aligned, self-contained arrays; the original pointer bits are never reused. */
inline bool build(std::span<const Row> rows, std::span<std::byte> output) noexcept {
    if (rows.empty() || rows.size() > kMaximumMembers || output.size() < capacity(rows.size())
        || reinterpret_cast<std::uintptr_t>(output.data()) % kRecordAlignment != 0) {
        return false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!valid(rows[i])) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (get<std::uint32_t>(rows[i].bytes.data())
                == get<std::uint32_t>(rows[j].bytes.data())) {
                return false;
            }
        }
    }
    std::memset(output.data(), 0, output.size());
    put(output.data() + kArrayMarkerOffset, kMarker);
    put(output.data() + kArrayCountOffset, static_cast<std::uint64_t>(rows.size()));
    put(output.data() + kArrayClassOffset, kMemberClass);
    // Condition records follow the member rows, so every relocated offset is positive.
    std::size_t cursor = kDataOffset + rows.size() * kMemberSize + kConditionReferenceOffset;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t at = kDataOffset + i * kMemberSize;
        std::memcpy(output.data() + at, rows[i].bytes.data(), kMemberSize);
        if (get<std::uint64_t>(rows[i].bytes.data() + kMemberConditionCountOffset) != 0) {
            std::memcpy(output.data() + cursor, rows[i].condition.data(), kConditionSize);
            put(output.data() + at + kMemberConditionRelativeOffset,
                static_cast<std::int64_t>(cursor + kConditionReferenceOffset)
                    - static_cast<std::int64_t>(at + kMemberConditionRelativeOffset));
            cursor += kConditionSize;
        }
    }
    return true;
}

} // namespace sunrise::client::hooks::network::investment::relocation
