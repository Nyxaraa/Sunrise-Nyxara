#include "current_activity_body.h"

namespace sunrise::middleware::gameplay::group::current_activity {
namespace {

namespace bits = encoding::bits;

/**
 * One field of the definition, in the order the field walker visits them.
 * `presence` names a field the walker precedes with one bit; a clear bit omits its value, so an
 * absent field costs exactly one bit. A field without one is unconditional and always written.
 */
struct Field {
    std::uint8_t presenceBit;
    std::uint8_t valueWidth;
    bool present;
};

/**
 * Fields 0 through 15 of the definition, minus the nonce, which the writer places itself.
 * Biased fields take zero, which decodes to the value one below their bias and is the absent
 * encoding every index field in this descriptor uses.
 */
constexpr std::uint8_t kNoPresenceBit = 0;
constexpr std::uint8_t kHasPresenceBit = 1;

/** Optional fields between the active-activity index and the selection nonce. */
constexpr Field kBeforeNonce[] = {
    {kHasPresenceBit, 9, false},  // element index, absent
    {kHasPresenceBit, 64, false}, // absent
};

/**
 * Fields written after the nonce.
 * The first is the nested count-prefixed array: a five-bit count of zero ends it, because its
 * element count is read from that same field.
 */
constexpr Field kTrailing[] = {
    {kNoPresenceBit, 5, true},    // nested array count, zero elements
    {kNoPresenceBit, 8, true},    // uint8 biased by zero
    {kHasPresenceBit, 8, false},  // absent
    {kHasPresenceBit, 32, false}, // absent
    {kHasPresenceBit, 32, false}, // absent
    {kHasPresenceBit, 0, false},  // nested, absent
    {kHasPresenceBit, 32, false}, // absent
    {kNoPresenceBit, 1, true},    // bool
    {kHasPresenceBit, 0, false},  // nested, absent
    {kHasPresenceBit, 0, false},  // nested, absent
};

/** Root bit, set because at least one optional field below it is present. */
constexpr std::uint8_t kRootBitSet = 1;

/**
 * Writes one field at its absent or zero encoding.
 * @param writer Open writer.
 * @param field Field to write.
 * @return True when its bits fit.
 */
[[nodiscard]] bool write_field(bits::Writer& writer, const Field& field) noexcept {
    if (field.presenceBit == kHasPresenceBit && !writer.write(field.present ? 1U : 0U, 1)) {
        return false;
    }
    if (field.presenceBit == kHasPresenceBit && !field.present) {
        return true;
    }
    return field.valueWidth == 0 || writer.write(0U, field.valueWidth);
}

/** Bits the tables and the nonce add up to, checked against the measured body width. */
consteval std::size_t total_bits() {
    // Root, launch reason, actual-activity index and active-activity index.
    std::size_t bits = 1 + 4 + 12 + 12;
    for (const Field& field : kBeforeNonce) {
        bits += field.presenceBit == kHasPresenceBit ? 1 : 0;
        bits += field.present ? field.valueWidth : 0;
    }
    bits += 1 + 64;
    for (const Field& field : kTrailing) {
        bits += field.presenceBit == kHasPresenceBit ? 1 : 0;
        bits += field.present ? field.valueWidth : 0;
    }
    return bits;
}

static_assert(total_bits() == kBodyBits);

/** Bias-1 encoding shared by the descriptor's reason and index scalars. */
[[nodiscard]] constexpr std::uint16_t encode_biased(std::int16_t value) noexcept {
    return static_cast<std::uint16_t>(value + 1);
}

// Focused boundary checks for the exact biased fields consumed by the transition classifier.
static_assert(encode_biased(kAbsentReason) == 0);
static_assert(encode_biased(0) == 1);
static_assert(encode_biased(kMaximumReason) == 15);
static_assert(encode_biased(kAbsentActivityIndex) == 0);
static_assert(encode_biased(0) == 1);
static_assert(encode_biased(kMaximumActivityIndex) == 4'095);

} // namespace

/** Writes one `current-activity` body carrying the transition inputs and selection nonce. */
bool write_body(bits::Writer& writer,
                std::int8_t reason,
                std::int16_t actualActivityIndex,
                std::int16_t activityIndex,
                std::uint64_t nonce) noexcept {
    if (!descriptor_is_valid(reason, actualActivityIndex, activityIndex, nonce)
        || !writer.write(kRootBitSet, 1)) {
        return false;
    }
    if (!writer.write(encode_biased(reason), 4)
        || !writer.write(encode_biased(actualActivityIndex), 12)
        || !writer.write(encode_biased(activityIndex), 12)) {
        return false;
    }
    for (const Field& field : kBeforeNonce) {
        if (!write_field(writer, field)) {
            return false;
        }
    }
    // The nonce is the only present optional field, so its own bit is set before its value.
    if (!writer.write(1U, 1) || !writer.write(nonce, 64)) {
        return false;
    }
    for (const Field& field : kTrailing) {
        if (!write_field(writer, field)) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::middleware::gameplay::group::current_activity
