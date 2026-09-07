#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::external {

/** A common root carries at most three activity entries. */
inline constexpr std::size_t kCommonEntryCapacity = 3;
/** Presence bits, list terminators and empty-channel markers are all one bit wide. */
inline constexpr std::uint8_t kFlagWidth = 1;

/** Reads one boolean field. @return True when the bit was present. */
[[nodiscard]] inline bool read_flag(encoding::bits::Reader& reader, bool& output) noexcept {
    std::uint64_t value = 0;
    if (!reader.read(kFlagWidth, value)) {
        return false;
    }
    output = value != 0;
    return true;
}

/** Writes one boolean field. @return True when the bit fit. */
[[nodiscard]] inline bool write_flag(encoding::bits::Writer& writer, bool value) noexcept {
    return writer.write(value ? 1U : 0U, kFlagWidth);
}

/** One activity binding in the gameplay common root. */
struct CommonEntry {
    std::uint64_t activitySessionId{};
    std::uint8_t reconciliationGeneration{};
};

/** Exact decoded gameplay common root. */
struct CommonState {
    std::array<std::uint64_t, 2> patchEpoch{};
    std::array<CommonEntry, kCommonEntryCapacity> entries{};
    std::uint8_t entryCount{};
};

/** Reads one common root without changing output on failure. */
[[nodiscard]] bool read_common_state(encoding::bits::Reader& reader, CommonState& output) noexcept;

/** Writes one bounded common root. */
[[nodiscard]] bool write_common_state(encoding::bits::Writer& writer,
                                      const CommonState& state) noexcept;

} // namespace sunrise::middleware::gameplay::external
