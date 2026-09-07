#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::global_activity_state {

/** Global activity state uses activity message type 1. */
inline constexpr std::uint32_t kMessageType = 1;
/** The minimum encoding is 1,161 bits, which is this many whole bytes. */
inline constexpr std::size_t kEncodedSize = 146;
/** Bits the minimum encoding takes before a dynamic descriptor extends it. */
inline constexpr std::size_t kMeaningfulBitCount = 1'161;

/** Wire bit 0 arms the initial slice set. With it clear the client never reads the pair below. */
inline constexpr std::size_t kInitialSliceSetArmBit = 0;
/** The bubble count starts here and is 32 bits wide. */
inline constexpr std::size_t kBubbleCountBit = 67;
inline constexpr std::uint8_t kBubbleCountWidth = 32;
/** The count is biased, so a zeroed body decodes to a large negative rather than zero. */
inline constexpr std::uint32_t kBubbleCountBias = 0x80000000U;
/** The per-bubble slice-set state array starts here: 64 bytes. */
inline constexpr std::size_t kBubbleStateBit = 99;
inline constexpr std::size_t kBubbleStateCount = 64;
/** The byte that records no slice-set state for a bubble. */
inline constexpr std::uint8_t kBubbleStateNone = 0x7F;
/** The initial slice-set index, bias 1, so wire zero means none. */
inline constexpr std::size_t kSliceSetBit = 611;
inline constexpr std::uint8_t kSliceSetWidth = 10;
inline constexpr std::uint32_t kSliceSetBias = 1;
/** The spawn-set name hash. */
inline constexpr std::size_t kSpawnSetBit = 621;
inline constexpr std::uint8_t kSpawnSetWidth = 32;
/** The embedded selection descriptor. Its first three fields are all bias 1. */
inline constexpr std::size_t kDescriptorBit = 686;
inline constexpr std::uint8_t kDescriptorReasonWidth = 4;
inline constexpr std::uint8_t kDescriptorIndexWidth = 12;
/** The descriptor's name presence bit, and the name that follows it. */
inline constexpr std::size_t kNamePresenceBit = 733;
inline constexpr std::size_t kNameBit = 734;
/** The name is this many 8-bit elements, and every character is biased. */
inline constexpr std::size_t kNameCapacity = 40;
inline constexpr std::uint8_t kNameBias = 0x80;
/** Fields after the descriptor: an empty switch list, a name hash, a flag and the time base. */
inline constexpr std::size_t kTailBits = 103;
inline constexpr std::uint8_t kSwitchCountWidth = 6;
inline constexpr std::uint8_t kDescriptorNameHashWidth = 32;
inline constexpr std::uint8_t kDescriptorFlagWidth = 1;
inline constexpr std::uint8_t kTimeBaseWidth = 64;

static_assert(kNameBit + kNameCapacity * 8 + 4 + kTailBits == kMeaningfulBitCount);
static_assert(kSwitchCountWidth + kDescriptorNameHashWidth + kDescriptorFlagWidth + kTimeBaseWidth
              == kTailBits);

/** One destination's global activity state. */
struct GlobalActivityState final {
    /** Latin-1 activity name. One element is kept for the null. */
    std::array<char, kNameCapacity> name{};
    std::size_t nameLength{};
    /** One byte per bubble, as the scenario reports them. */
    std::array<std::uint8_t, kBubbleStateCount> bubbleStates{};
    std::uint32_t bubbleCount{};
    /** Initial slice-set index, or absent when the destination has none. */
    std::uint32_t sliceSetIndex{};
    bool hasSliceSet{};
    std::uint32_t spawnSetHash{};
    /** Selection descriptor scalars. Each accepts -1, which is encoded as wire zero. */
    std::int32_t reason{-1};
    /** Selection field 1: the activity asked for, before it resolved to the index below. */
    std::int32_t requestedActivityIndex{-1};
    std::int32_t activityIndex{-1};
    /**
     * Activity Host time origin in whole seconds, shared by every member of one session.
     * The client adds its own elapsed seconds and reduces the sum to a 0..1 periodic phase, so
     * members that hold different origins run that phase apart.
     */
    std::uint64_t timeBase{};
    /**
     * The client's own selection descriptor, left-aligned. It carries fields with no known name,
     * so it is replayed rather than re-encoded. Replay also needs the name branch below to agree
     * byte-for-byte with name; an empty or incoherent view uses the configured descriptor.
     */
    std::span<const std::byte> descriptorBits{};
    /** Meaningful bits in descriptorBits, or zero to force the configured descriptor. */
    std::size_t descriptorBitLength{};
    /** First package-name byte's bit offset inside descriptorBits. */
    std::size_t descriptorNameBit{};
    /** True only when the captured descriptor carried its native package-name branch. */
    bool hasDescriptorName{};
};

/**
 * Encodes one global activity state body. A coherent captured descriptor is replayed exactly;
 * otherwise the native minimal descriptor is written from the checked logical fields.
 *
 * @param state Destination name, bubble layout, slice set and spawn set.
 * @param output Caller storage, unchanged when a check fails or it is too small.
 * @param written Receives the encoded size on success, or zero on failure.
 * @return True when the whole body was encoded.
 */
[[nodiscard]] bool encode_global_activity_state(const GlobalActivityState& state,
                                                std::span<std::byte> output,
                                                std::size_t& written) noexcept;

} // namespace sunrise::middleware::bap::activity_message::global_activity_state
