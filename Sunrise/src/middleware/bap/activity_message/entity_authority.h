#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "entity_slots.h"

namespace sunrise::middleware::bap::activity_message::entity_authority {

/** The Client abandons slots for bubbles it has left. */
inline constexpr std::uint32_t kAbandonMessageType = 26;
/** The Client asks the host to purge slots it could not claim. */
inline constexpr std::uint32_t kRequestPurgeMessageType = 27;
/** The Client acknowledges an authority-mask reset. */
inline constexpr std::uint32_t kResetAcknowledgementMessageType = 29;
/** The Client answers a mask query with one selector's slots. */
inline constexpr std::uint32_t kQueryPerBubbleMessageType = 31;
/** The Client answers a mask query with its whole mask. */
inline constexpr std::uint32_t kQueryResponseMessageType = 32;
/** The Client gives up authority over a set of slots. */
inline constexpr std::uint32_t kAbdicateMessageType = 33;

/** Msgs 26 and 33 lead with a compact bubble selector byte. */
inline constexpr std::uint8_t kSelectorWidth = 8;
/** Bubble selectors range from 0 through the no-bubble value 64. */
inline constexpr std::uint8_t kMaximumSelector = 64;
/** Msgs 26 and 33 store one complete selector byte. */
inline constexpr std::size_t kSelectorSize = sizeof(std::uint8_t);
/** The reason on msg 26 and the leading field on msg 27 are 3 bits with bias 1. */
inline constexpr std::uint8_t kReasonWidth = 3;
/** The 3-bit reason decodes as wire minus 1, so the logical range is -1 to 6. */
inline constexpr std::int32_t kReasonBias = 1;
/** Msg 26 is one selector byte, the mask, then the 3-bit reason. */
inline constexpr std::size_t kAbandonBits =
    kSelectorWidth + entity_slots::kSlotCount + kReasonWidth;
/** The msg-26 body pads to a whole byte. */
inline constexpr std::size_t kAbandonByteCount = (kAbandonBits + 7) / 8;
/** Msg 27 is the 3-bit field then the mask, so its mask is not byte aligned. */
inline constexpr std::size_t kRequestPurgeBits = kReasonWidth + entity_slots::kSlotCount;
inline constexpr std::size_t kRequestPurgeByteCount = (kRequestPurgeBits + 7) / 8;
/** Msg 33 is one selector byte then the mask. */
inline constexpr std::size_t kAbdicateBits = kSelectorWidth + entity_slots::kSlotCount;
inline constexpr std::size_t kAbdicateByteCount = (kAbdicateBits + 7) / 8;
/** Msgs 29, 31 and 32 lead with the 4-byte correlation the host sent on msg 28 or 30. */
inline constexpr std::size_t kCorrelationSize = sizeof(std::uint32_t);
inline constexpr std::size_t kResetAcknowledgementByteCount = kCorrelationSize;
inline constexpr std::size_t kQueryPerBubbleByteCount =
    kCorrelationSize + kSelectorSize + entity_slots::kEncodedSize;
/** Msg 32 is the correlation then the whole mask, with no selector. */
inline constexpr std::size_t kQueryResponseByteCount =
    kCorrelationSize + entity_slots::kEncodedSize;

/** One decoded slot-release message. Msg 33 carries no reason. */
struct Release {
    entity_slots::EntitySlotMask mask{};
    std::uint8_t selector{};
    std::int32_t reason{};
    bool hasReason{};
};

/** One decoded msg-27 purge request. Its mask begins three bits into the body. */
struct PurgeRequest {
    entity_slots::EntitySlotMask mask{};
    std::int32_t reason{};
};

/** One decoded answer to a host mask query or reset. */
struct QueryAnswer {
    entity_slots::EntitySlotMask mask{};
    std::int32_t correlation{};
    std::uint8_t selector{};
    bool hasSelector{};
    bool hasMask{};
};

/**
 * Parses msg 26, whose mask starts on a byte boundary after the selector.
 * @param payload Activity message payload after the 17-byte envelope.
 * @param release Receives the fields on success and stays unchanged on failure.
 * @return True when the body is exact, bounded, and has zero padding.
 */
[[nodiscard]] bool parse_abandon(std::span<const std::byte> payload, Release& release) noexcept;

/**
 * Parses msg 33, which is the selector and the mask with no reason.
 * @param payload Activity message payload after the 17-byte envelope.
 * @param release Receives the fields on success and stays unchanged on failure.
 * @return True when the body is exact and the selector is valid.
 */
[[nodiscard]] bool parse_abdicate(std::span<const std::byte> payload, Release& release) noexcept;

/**
 * Parses the 3-bit leading field and the complete unaligned 8,192-bit mask of msg 27.
 * @param payload Activity message payload after the 17-byte envelope.
 * @param request Receives the fields on success and stays unchanged on failure.
 * @return True when the body is exact and has zero padding.
 */
[[nodiscard]] bool parse_request_purge(std::span<const std::byte> payload,
                                       PurgeRequest& request) noexcept;

/**
 * Parses msg 29, 31 or 32. Msg 29 is the correlation alone, msg 31 adds a selector and a mask,
 * and msg 32 adds a mask.
 * @param messageType One of 29, 31 or 32.
 * @param payload Activity message payload after the 17-byte envelope.
 * @param answer Receives the fields on success and stays unchanged on failure.
 * @return True when the body has the exact size and valid selector for its type.
 */
[[nodiscard]] bool parse_query_answer(std::uint32_t messageType,
                                      std::span<const std::byte> payload,
                                      QueryAnswer& answer) noexcept;

} // namespace sunrise::middleware::bap::activity_message::entity_authority
