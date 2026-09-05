#include "sensor_auth_update.h"

#include <array>
#include <atomic>
#include <cstdio>

#include "../../../core/logging/log.h"

namespace sunrise::middleware::bap::activity_message::sensor_auth_update {
namespace {

namespace bits = encoding::bits;

/** Slot types whose auth body this module fills. Every other block is seed-only. */
constexpr std::uint8_t kSlotTypeParticipation = 13;
constexpr std::uint8_t kSlotTypeLifetime = 17;
constexpr std::uint8_t kSlotTypeConfiguration = 8;
constexpr std::uint8_t kSlotTypePackage = 16;
constexpr std::uint8_t kSlotTypeQueues = 41;
constexpr std::uint8_t kSlotTypeSpawnKeys = 67;
/**
 * The one published slot this host still announces without a body.
 * A run measured every published width: 13 carries 224 bits, 16 carries 7, 17 carries 520, 18
 * carries 386, 35 carries 359, and 37 carries **zero**. Its 1750-bit width was recovered from the
 * client's own field tables alongside the type-35 and type-18 ones, and never written. The block
 * ships on every region -- the roster body is byte-identical at region 8, 112 and 144 -- so it is
 * in the stream while the player stands at the Wall of Wishes.
 */
constexpr std::uint8_t kSlotTypeWideRecord = 37;
/**
 * The slot the bubble-14 roster group brought in, and the second one found shipping bodyless.
 * Publishing the Wall of Wishes room's object added type 30 to the stream and a measured run
 * reported it at `bits=0`.
 */
constexpr std::uint8_t kSlotTypeRegionRecord = 30;
/** The mission director. Its body is what an encounter bubble's script objects come from. */
constexpr std::uint8_t kSlotTypeDirector = 35;
/** The activity script runtime, which ships beside the director in the same group. */
constexpr std::uint8_t kSlotTypeScriptRuntime = 18;

/** Body widths, each checked against the writer after the body is written. */
constexpr std::size_t kParticipationBits = 192;
constexpr std::size_t kParticipationRegionBits = 32;
constexpr std::size_t kLifetimeBits = 520;
constexpr std::size_t kConfigurationBits = 35;
constexpr std::size_t kPackageBits = 7;
constexpr std::size_t kQueueBits = 12;
constexpr std::size_t kSpawnKeyBits = 32 * 32 + 1 + 32;
/**
 * Width of the empty type-37 body, derived from the client's schema tree rather than recalled.
 *
 * Slot 37 is schema `0x80805007` -> `0x80805008`, which holds two `0x8080500B` records and one
 * `0x80805009`. `0x8080500B` is 32 + 8 + `0x8080500F` (four groups of i8,i8,u32,bool = 196) + 7 + 1
 * + 32 + 32 + five biased i32 + `0x8080500D`; `0x80805009` is 32 + 8 + 8. `0x8080500D` is a 7-bit
 * COUNT followed by that many 16-bit elements, so **this body is variable width** -- a fixed number
 * cannot be right for it in general, and a zero count is the well-formed empty form.
 *
 * 2 x 475 + 48 = 998. An earlier note recorded 1750, which no whole element count produces
 * (23 gives 1734, 24 gives 1766); it was never verified on the wire the way the type-35 and
 * type-18 widths were, and it is not used.
 */
constexpr std::size_t kWideRecordBits = 998;
/**
 * Width of the type-30 body, from the client's field tables.
 * Slot 30 is schema `0x80809532`: a nested `0x80809C42` of {u32, 7-bit biased +1, 16-bit biased
 * +0x8000} followed by a 32-bit field biased +2^31. Fixed width, no presence bit and no array, so
 * there is exactly one legal length and the width check below is exact -- the same shape as the
 * type-35 and type-18 bodies, which this same decode reproduces at 359 and 386 exactly.
 */
constexpr std::size_t kRegionRecordBits = 32 + 7 + 16 + 32;
/**
 * The record shared by the director and the script runtime, class `0x808099C4`.
 * One bool, five raw 64-bit words and a raw 32-bit word. Every field is unbiased, so a zero body
 * decodes to zeroes rather than to a sentinel.
 */
constexpr std::size_t kSharedDirectorRecordBits = 1 + 5 * 64 + 32;
/** Words in that shared record. */
constexpr std::size_t kSharedDirectorWords = 5;
/** Director body: two bools, two bias-1 selectors, then the shared record. */
constexpr std::size_t kDirectorBits = 1 + 1 + 2 + 2 + kSharedDirectorRecordBits;
/** Script-runtime body: the shared record, a bool, then one biased signed word. */
constexpr std::size_t kScriptRuntimeBits = kSharedDirectorRecordBits + 1 + 32;
/** Width of the director's two selectors, each stored as a signed byte biased by one. */
constexpr std::uint8_t kDirectorSelectorWidth = 2;
/** Wire value those selectors need for zero. Zero would decode to -1, the none sentinel. */
constexpr std::uint32_t kDirectorSelectorZero = 1;
/** Type-30's 7-bit field carries a bias of one, so this wire value decodes to a literal zero. */
constexpr std::uint32_t kRegionSelectorZero = 1;
/** Type-30's 16-bit field carries a bias of 0x8000, so this wire value decodes to zero. */
constexpr std::uint32_t kUnsignedShortZero = 0x8000;

/** Signed fields in these bodies carry a -2^31 bias, so this wire value stores zero. */
constexpr std::uint32_t kSignedZero = 0x80000000;
/** The same bias wraps at the top of the field, so this wire value stores -1. */
constexpr std::uint32_t kSignedMinusOne = 0x7FFFFFFF;
/** The region index rides the same bias, so its wire value is the bias plus the index. */
constexpr std::uint32_t kRegionBias = 0x80000000;
/** Message 52's team-state byte 1, where bit 1 is `awaiting_client_sync`. */
constexpr std::uint32_t kAwaitingClientSync = 2;
/** Neutral runtime-i32 override that forces the type-17 waiting selector to zero. */
constexpr std::uint32_t kWaitingSwitchKey = 0xB3C1251B;
constexpr std::uint32_t kWaitingSwitchClass = 0x80800007;
/** Type 17 carries 3 spawn overrides. Wire zero stores index -1 and disables one. */
constexpr std::size_t kSpawnOverrideCount = 3;
constexpr std::uint8_t kSpawnOverrideIndexWidth = 10;
constexpr std::uint32_t kSpawnOverrideIndexBias = 1;
/** Type 67 maps the 32 spawn-key ordinals to themselves, matching its constructor. */
constexpr std::size_t kSpawnKeyCount = 32;

/**
 * Writes the participation body, which binds the player and latches the region.
 * Every biased field must carry its bias; a zero-filled field decodes to the smallest signed value.
 * @param writer Body writer.
 * @param snapshot Message input.
 * @return True when the body fits.
 */
[[nodiscard]] bool write_participation(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    // An optional field's value follows its presence bit, so sending +0 shifts everything below.
    bool encoded = writer.write(snapshot.hasRegion ? 1U : 0U, kPresenceWidth);
    if (encoded && snapshot.hasRegion) {
        encoded = writer.write(kRegionBias + snapshot.region, kParticipationRegionBits);
    }
    // The participation record is this body's head, so struct +8 and +10 are record +8 and +10.
    // Record +8 is step 36 task 9's own term and +10 is the spawn gate's.
    return encoded && writer.write(0, kPresenceWidth) && writer.write(1, kPresenceWidth)
           && writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
           && writer.write(0, kPresenceWidth) && writer.write(1, 3) && writer.write(1, 2)
           && writer.write(0, 3) && writer.write(0, 32) && writer.write(1, 5)
           && writer.write(0, kPresenceWidth) && writer.write(0, 3)
           && writer.write(1, kPresenceWidth) && writer.write(snapshot.playerKey, 64)
           && writer.write(0, 5) && writer.write(3, 6) && writer.write(0, 6)
           && writer.write(0, 6)
           // Byte 736 skips the respawn delay, whose countdown never expires when the content
           // delay is negative. Byte 737 holds the spawn while the client loads.
           && writer.write(1, kPresenceWidth)
           && writer.write(snapshot.awaitClientSync ? kAwaitingClientSync : 0U, 4)
           && writer.write(0, 3) && writer.write(0, kPresenceWidth) && writer.write(128, 8)
           && writer.write(kSignedZero, 32);
}

/**
 * Writes the lifetime body, which is the activity state the roster reports.
 * @param writer Body writer.
 * @param snapshot Message input.
 * @return True when the body fits.
 */
[[nodiscard]] bool write_lifetime(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    // Field `.4` names an authored spawn entry. No host model owns one, so it carries the empty
    // name hash; zero is a hash that no row matches.
    bool encoded = writer.write(std::uint32_t{snapshot.lifetime} + kLifetimeBias, kLifetimeWidth)
                   && writer.write(1, 3) && writer.write(0, kPresenceWidth)
                   && writer.write(kSignedZero, 32) && writer.write(kEmptyNameHash, 32)
                   && writer.write(kSignedZero, 32) && writer.write(1, 6)
                   && writer.write(kWaitingSwitchKey, 32) && writer.write(1, kPresenceWidth)
                   && writer.write(kWaitingSwitchClass, 32) && writer.write(kSignedZero, 32)
                   && writer.write(kSignedZero, 32);
    for (std::size_t index = 0; encoded && index < kSpawnOverrideCount; ++index) {
        const std::uint32_t slice =
            snapshot.hasSpawnOverride ? snapshot.spawnSliceSet + kSpawnOverrideIndexBias : 0U;
        const std::uint32_t hash =
            snapshot.hasSpawnOverride ? snapshot.spawnSetHash : kAbsentSpawnSetHash;
        encoded = writer.write(slice, kSpawnOverrideIndexWidth) && writer.write(hash, 32);
    }
    // Struct `+1256` is the out-of-bounds `activity_quarantine` selector. The reader arms the
    // quarantine at or below 0x3F unsigned, so minus one leaves it clear and teleports nobody.
    return encoded && writer.write(0, kPresenceWidth) && writer.write(0, 32)
           && writer.write(kSignedMinusOne, 32) && writer.write(0, 32)
           && writer.write(kSlotTypeBias, kSlotTypeWidth)
           && writer.write(kSlotIndexBias, kSlotIndexWidth) && writer.write(0, 32)
           && writer.write(0, 3);
}

/**
 * Writes the record shared by the director and the script-runtime bodies, class `0x808099C4`.
 * Recovered from the client's own static field table, whose walker reads a 1-bit bool, five
 * unbiased 64-bit words and one unbiased raw 32-bit word. Unbiased means a zero wire value stores
 * a literal zero, so this is the neutral, fully-constructed form of the record rather than one
 * that decodes to a sentinel.
 * @param writer Body writer.
 * @return True when the record fits.
 */
[[nodiscard]] bool write_shared_director_record(bits::Writer& writer) noexcept {
    bool encoded = writer.write(0, kPresenceWidth);
    for (std::size_t word = 0; encoded && word < kSharedDirectorWords; ++word) {
        encoded = writer.write(0, 64);
    }
    return encoded && writer.write(0, 32);
}

/**
 * Writes the mission-director body, class `0x808099BF`.
 * The director is the slot an encounter bubble's script objects are authored from, and until this
 * existed `auth_body_bits` returned zero for it, so the block went out with a header and no body.
 * The body is fixed width: the client's field table declares no presence bit, no array and no
 * variant field, so there is exactly one legal length and the width check below is exact.
 *
 * The two selectors are the only fields that are not zero-safe. Their descriptors carry a bias of
 * one, so a zero wire value decodes to -1 — the engine's none sentinel, the same shape as the
 * lifetime's `+1` and the bias-one spawn-override index whose zero disables the override.
 * @param writer Body writer.
 * @return True when the body fits.
 */
[[nodiscard]] bool write_director(bits::Writer& writer) noexcept {
    return writer.write(0, kPresenceWidth) && writer.write(0, kPresenceWidth)
           && writer.write(kDirectorSelectorZero, kDirectorSelectorWidth)
           && writer.write(kDirectorSelectorZero, kDirectorSelectorWidth)
           && write_shared_director_record(writer);
}

/**
 * Writes the activity-script-runtime body, class `0x80809919`.
 * The shared record comes first here, then this slot's own bool and signed word. The trailing word
 * rides the same `+2^31` bias as every other signed field in these bodies, so it carries the bias
 * rather than a plain zero.
 * @param writer Body writer.
 * @return True when the body fits.
 */
[[nodiscard]] bool write_script_runtime(bits::Writer& writer) noexcept {
    return write_shared_director_record(writer) && writer.write(0, kPresenceWidth)
           && writer.write(kSignedZero, 32);
}

/**
 * Writes the spawn-key body, which maps the 32 ordinals to themselves.
 * @param writer Body writer.
 * @return True when the body fits.
 */
[[nodiscard]] bool write_spawn_keys(bits::Writer& writer) noexcept {
    bool encoded = true;
    for (std::size_t index = 0; encoded && index < kSpawnKeyCount; ++index) {
        encoded = writer.write(kSignedZero + index, 32);
    }
    return encoded && writer.write(0, kPresenceWidth) && writer.write(kSignedMinusOne, 32);
}

} // namespace

/** Reports how many bits of auth body one slot carries, without reporting it. */
std::size_t auth_body_bits_of(const Snapshot& snapshot,
                              std::uint8_t slotType,
                              bool carriesPlayerKey) noexcept {
    if (slotType == kSlotTypeParticipation) {
        return carriesPlayerKey
                   ? kParticipationBits + (snapshot.hasRegion ? kParticipationRegionBits : 0)
                   : 0;
    }
    if (slotType == kSlotTypeLifetime) {
        return kLifetimeBits;
    }
    if (slotType == kSlotTypeConfiguration) {
        return kConfigurationBits;
    }
    if (slotType == kSlotTypePackage) {
        return kPackageBits;
    }
    if (slotType == kSlotTypeQueues) {
        return kQueueBits;
    }
    if (slotType == kSlotTypeSpawnKeys) {
        return kSpawnKeyBits;
    }
    // Both are settings-gated: a body of the wrong width does not fail this host's own width
    // check, it desynchronises the client's parse of every block after it in the same phase-2
    // stream, which would cost the player their spawn. Off, they go out bodyless as before.
    if (slotType == kSlotTypeDirector) {
        return snapshot.authorDirectorBodies ? kDirectorBits : 0;
    }
    if (slotType == kSlotTypeScriptRuntime) {
        return snapshot.authorDirectorBodies ? kScriptRuntimeBits : 0;
    }
    if (slotType == kSlotTypeWideRecord) {
        return snapshot.authorWideRecordBodies ? kWideRecordBits : 0;
    }
    if (slotType == kSlotTypeRegionRecord) {
        return snapshot.authorWideRecordBodies ? kRegionRecordBits : 0;
    }
    return 0;
}

/**
 * Names each published slot type and the body width it goes out with, once per distinct pair.
 *
 * A slot whose width is zero is announced to the client and then described with nothing -- the
 * exact shape of the gap that types 35 and 18 had before their bodies were written. Types 21 and
 * 37 are admitted by `kRosterSlotTypes` and still fall through to `return 0` here, and type 37's
 * body was measured at 1750 bits and never implemented. Printing the pairs says which published
 * slots are actually bodyless on this destination instead of inferring it from the filter.
 * @param slotType Slot type being sized.
 * @param bits Body width it will carry.
 */
void report_slot_width(std::uint8_t slotType, std::size_t bits) noexcept {
    static std::atomic<std::uint64_t> reported{};
    if (slotType >= 64 || !core::log::accepts(core::log::Channel::middleware,
                                              core::log::Level::debug)) {
        return;
    }
    const std::uint64_t bit = 1ULL << slotType;
    if ((reported.fetch_or(bit, std::memory_order_relaxed) & bit) != 0) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=slot_width type=%u bits=%zu%s",
                                      static_cast<unsigned>(slotType),
                                      bits,
                                      bits == 0 ? " result=bodyless" : "");
    if (written > 0) {
        core::log::write(core::log::Channel::middleware,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Reports how many bits of auth body one slot carries. */
std::size_t
auth_body_bits(const Snapshot& snapshot, std::uint8_t slotType, bool carriesPlayerKey) noexcept {
    const std::size_t width = auth_body_bits_of(snapshot, slotType, carriesPlayerKey);
    report_slot_width(slotType, width);
    return width;
}


/** Writes one slot's auth body. */
bool write_auth_body(bits::Writer& writer,
                     const Snapshot& snapshot,
                     std::uint8_t slotType,
                     bool carriesPlayerKey) noexcept {
    const std::size_t start = writer.bit_count();
    const std::size_t expected = auth_body_bits(snapshot, slotType, carriesPlayerKey);
    bool encoded = true;
    if (slotType == kSlotTypeParticipation && carriesPlayerKey) {
        encoded = write_participation(writer, snapshot);
    } else if (slotType == kSlotTypeLifetime) {
        encoded = write_lifetime(writer, snapshot);
    } else if (slotType == kSlotTypeConfiguration) {
        // Both optional arrays absent and the terminal tag clear is the constructed state.
        encoded = writer.write(0, kPresenceWidth) && writer.write(0, kPresenceWidth)
                  && writer.write(0, kPresenceWidth) && writer.write(0, 32);
    } else if (slotType == kSlotTypePackage) {
        // 7 absent top-level fields keep the package-owned configuration.
        encoded = pad_bits(writer, kPackageBits);
    } else if (slotType == kSlotTypeQueues) {
        encoded = writer.write(0, 7) && writer.write(0, 5);
    } else if (slotType == kSlotTypeSpawnKeys) {
        encoded = write_spawn_keys(writer);
    } else if (slotType == kSlotTypeDirector && snapshot.authorDirectorBodies) {
        encoded = write_director(writer);
    } else if (slotType == kSlotTypeScriptRuntime && snapshot.authorDirectorBodies) {
        encoded = write_script_runtime(writer);
    } else if (slotType == kSlotTypeRegionRecord && snapshot.authorWideRecordBodies) {
        // Zero is NOT the constructed state here: three of the four fields carry a bias, so a
        // neutral body writes each bias rather than a zero. Writing zeros would decode to -1 in
        // the 7-bit field and to large negatives in the other two.
        encoded = writer.write(0, 32) && writer.write(kRegionSelectorZero, 7)
                  && writer.write(kUnsignedShortZero, 16) && writer.write(kSignedZero, 32);
    } else if (slotType == kSlotTypeWideRecord && snapshot.authorWideRecordBodies) {
        // Zeroes are the empty form here rather than merely a neutral one: the two element
        // counts inside `0x8080500D` read zero, so the body declares two empty arrays and every
        // other field at its unbiased zero.
        encoded = pad_bits(writer, kWideRecordBits);
    }
    return encoded && writer.bit_count() == start + expected;
}

} // namespace sunrise::middleware::bap::activity_message::sensor_auth_update
