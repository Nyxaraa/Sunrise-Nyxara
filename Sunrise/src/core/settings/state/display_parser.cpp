#include <bitset>
#include <limits>

#include "../parser.h"

namespace sunrise::core::settings::parser {

/** Parses screen and renderer settings under stable Sunrise-owned names. */
bool Parser::display_settings(state::account::settings::Display& output) noexcept {
    enum class Field : std::size_t {
        brightness,
        showFps,
        hdrMode,
        calibrationPrimary,
        calibrationAlpha,
        count,
    };

    output = {};
    if (!consume('{')) {
        return false;
    }
    std::bitset<static_cast<std::size_t>(Field::count)> supplied;
    // Tracked apart from the bitset because these two keys are optional: an enumerated field is
    // required by the supplied.all() below, and an older settings file does not carry them.
    bool hasVerticalSyncInterval = false;
    bool hasFieldOfView = false;
    const auto mark = [&supplied](Field field) noexcept {
        const std::size_t index = static_cast<std::size_t>(field);
        if (supplied.test(index)) {
            return false;
        }
        supplied.set(index);
        return true;
    };
    if (consume('}')) {
        return false;
    }
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "brightness") {
            if (!mark(Field::brightness) || !signed_byte(output.brightness)) {
                return false;
            }
        } else if (key == "show_fps") {
            if (!mark(Field::showFps) || !boolean(output.showFps)) {
                return false;
            }
        } else if (key == "hdr_mode") {
            if (!mark(Field::hdrMode) || !signed_byte(output.hdrMode)) {
                return false;
            }
        } else if (key == "vertical_sync_interval") {
            std::uint64_t value = 0;
            // Bounds the narrowing only. The 0 to 4 presentation domain is checked with the rest
            // of the account settings, so it is stated once.
            if (hasVerticalSyncInterval || !unsigned_integer(value)
                || value > (std::numeric_limits<std::uint8_t>::max)()) {
                return false;
            }
            output.verticalSyncInterval = static_cast<std::uint8_t>(value);
            hasVerticalSyncInterval = true;
        } else if (key == "field_of_view") {
            if (hasFieldOfView || !signed_32(output.fieldOfView)) {
                return false;
            }
            hasFieldOfView = true;
        } else if (key == "calibration_primary") {
            if (!mark(Field::calibrationPrimary) || !floating_point(output.calibrationPrimary)) {
                return false;
            }
        } else if (key == "calibration_alpha") {
            if (!mark(Field::calibrationAlpha) || !floating_point(output.calibrationAlpha)) {
                return false;
            }
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            return supplied.all();
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace sunrise::core::settings::parser
