#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace sunrise::client::hooks::network::investment::relocation {

inline constexpr std::uint32_t kMarker = 0x80809FBDU;
inline constexpr std::uint32_t kMemberClass = 0x80802E03U;
inline constexpr std::uint32_t kConditionClass = 0x80807D31U;
inline constexpr std::size_t kDataOffset = 24;
inline constexpr std::size_t kMaximumMembers = 56;
inline constexpr std::size_t kMemberSize = 32;
inline constexpr std::size_t kConditionSize = 32;

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

inline bool valid(const Row& row) noexcept {
    const auto count = get<std::uint64_t>(row.bytes.data() + 8);
    if (count == 0) {
        return get<std::int64_t>(row.bytes.data() + 16) == 0;
    }
    return count == 1 && get<std::uint32_t>(row.condition.data()) == kMarker
           && get<std::uint64_t>(row.condition.data() + 4) == 1
           && get<std::uint32_t>(row.condition.data() + 12) == kConditionClass
           && get<std::uint32_t>(row.condition.data() + 16) == 0;
}

inline std::size_t capacity(std::size_t count) noexcept {
    return kDataOffset + count * (kMemberSize + kConditionSize) + 8;
}

/** Builds aligned, self-contained arrays; the original pointer bits are never reused. */
inline bool build(std::span<const Row> rows, std::span<std::byte> output) noexcept {
    if (rows.empty() || rows.size() > kMaximumMembers || output.size() < capacity(rows.size())
        || reinterpret_cast<std::uintptr_t>(output.data()) % 8 != 0) {
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
    put(output.data() + 4, kMarker);
    put(output.data() + 8, static_cast<std::uint64_t>(rows.size()));
    put(output.data() + 16, kMemberClass);
    std::size_t cursor = kDataOffset + rows.size() * 32 + 4;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t at = kDataOffset + i * 32;
        std::memcpy(output.data() + at, rows[i].bytes.data(), 32);
        if (get<std::uint64_t>(rows[i].bytes.data() + 8) != 0) {
            std::memcpy(output.data() + cursor, rows[i].condition.data(), 32);
            put(output.data() + at + 16,
                static_cast<std::int64_t>(cursor + 4) - static_cast<std::int64_t>(at + 16));
            cursor += 32;
        }
    }
    return true;
}

/** Independently follows every relocated reference and compares all original payload bytes. */
inline bool verify(std::span<const Row> rows, std::span<const std::byte> blob) noexcept {
    if (rows.empty() || rows.size() > kMaximumMembers
        || blob.size() < kDataOffset + rows.size() * 32
        || reinterpret_cast<std::uintptr_t>(blob.data()) % 8 != 0
        || get<std::uint32_t>(blob.data() + 4) != kMarker
        || get<std::uint64_t>(blob.data() + 8) != rows.size()
        || get<std::uint32_t>(blob.data() + 16) != kMemberClass
        || get<std::uint32_t>(blob.data() + 20) != 0) {
        return false;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t at = kDataOffset + i * 32;
        const auto* member = blob.data() + at;
        if (!valid(rows[i]) || std::memcmp(member, rows[i].bytes.data(), 16) != 0
            || std::memcmp(member + 24, rows[i].bytes.data() + 24, 8) != 0) {
            return false;
        }
        const auto relative = get<std::int64_t>(member + 16);
        if (get<std::uint64_t>(member + 8) == 0) {
            if (relative != 0) return false;
            continue;
        }
        // Owned condition storage is after the member array, so all relocated offsets are positive.
        if (relative <= 0 || static_cast<std::uint64_t>(relative) > blob.size() - at - 16) {
            return false;
        }
        const std::size_t header = at + 16 + static_cast<std::size_t>(relative);
        if (header % 8 != 0 || header < kDataOffset + rows.size() * 32 + 4
            || blob.size() - header < 28
            || std::memcmp(blob.data() + header - 4, rows[i].condition.data(), 32) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::client::hooks::network::investment::relocation
