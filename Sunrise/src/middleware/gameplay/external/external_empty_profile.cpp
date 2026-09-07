#include "external_empty_profile.h"

namespace sunrise::middleware::gameplay::external {
namespace {

/** A channel-2 current entity cell is one byte. */
constexpr std::uint8_t kEntityCellWidth = 8;

} // namespace

/** Reads the receive-only common root and empty-channel profile. */
EmptyProfileResult read_empty_profile(encoding::bits::Reader& reader,
                                      EmptyProfile& output) noexcept {
    EmptyProfile candidate{};
    if (!read_flag(reader, candidate.commonPresent)) {
        return EmptyProfileResult::malformed;
    }
    if (candidate.commonPresent && !read_common_state(reader, candidate.common)) {
        return EmptyProfileResult::malformed;
    }

    bool present = false;
    if (!read_flag(reader, present)) {
        return EmptyProfileResult::malformed;
    }
    if (present) {
        return EmptyProfileResult::channel0Present;
    }
    if (!read_flag(reader, present)) {
        return EmptyProfileResult::malformed;
    }
    if (present) {
        return EmptyProfileResult::channel1Present;
    }
    // Channel 2 starts with a one-bit auxiliary-token count.
    if (!read_flag(reader, present)) {
        return EmptyProfileResult::malformed;
    }
    if (present) {
        return EmptyProfileResult::channel2Present;
    }
    if (!read_flag(reader, candidate.currentCellPresent)) {
        return EmptyProfileResult::malformed;
    }
    if (candidate.currentCellPresent) {
        std::uint64_t cell = 0;
        if (!reader.read(kEntityCellWidth, cell)) {
            return EmptyProfileResult::malformed;
        }
        candidate.currentCell = static_cast<std::uint8_t>(cell);
    }
    // An empty channel-2 record list is encoded by its true terminator.
    if (!read_flag(reader, present)) {
        return EmptyProfileResult::malformed;
    }
    if (!present) {
        return EmptyProfileResult::channel2Present;
    }
    // Channel 3 contributes only its absence bit. A trailing list exists only when present.
    if (!read_flag(reader, present)) {
        return EmptyProfileResult::malformed;
    }
    if (present) {
        return EmptyProfileResult::channel3Present;
    }

    output = candidate;
    return EmptyProfileResult::accepted;
}

} // namespace sunrise::middleware::gameplay::external
