/** Validates incident framing and rejects targets unsafe for the Client's unbounded table read. */

#include <algorithm>

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"
#include "incident.h"

namespace sunrise::middleware::bap::activity_message::incident {
namespace {

/** Retains byte fields even when their wire start is not byte-aligned. */
[[nodiscard]] bool read_bytes(encoding::bits::Reader& reader,
                              std::span<std::byte> output) noexcept {
    for (std::byte& value : output) {
        std::uint64_t field = 0;
        if (!reader.read(encoding::kBitsPerByte, field)) {
            return false;
        }
        value = static_cast<std::byte>(field);
    }
    return true;
}

/** @return True when one target index is safe to hand to the Client's table lookup. */
[[nodiscard]] bool target_allowed(std::uint32_t target, Verdict& verdict) noexcept {
    if (target > kTargetMaximum) {
        verdict = Verdict::targetOutOfRange;
        return false;
    }
    if (std::find(kPoisonTargets.begin(), kPoisonTargets.end(), target) != kPoisonTargets.end()) {
        verdict = Verdict::targetPoisoned;
        return false;
    }
    return true;
}

} // namespace

/** @return A short stable name for one verdict, for the log line. */
const char* verdict_name(Verdict verdict) noexcept {
    switch (verdict) {
    case Verdict::accepted:
        return "accepted";
    case Verdict::truncated:
        return "truncated";
    case Verdict::targetOutOfRange:
        return "target_out_of_range";
    case Verdict::targetPoisoned:
        return "target_poisoned";
    case Verdict::tooManyTargets:
        return "too_many_targets";
    case Verdict::payloadTooLong:
        return "payload_too_long";
    case Verdict::selectorTooLong:
        return "selector_too_long";
    }
    return "unknown";
}

/** Validates one incident body from its first target to the end of its payload. */
Verdict validate(std::span<const std::byte> payload, Incident& parsed) noexcept {
    parsed = {};
    encoding::bits::Reader reader(payload);

    std::uint64_t field = 0;
    if (!reader.read(kTargetWidth, field)) {
        return Verdict::truncated;
    }
    parsed.primaryTarget = static_cast<std::uint32_t>(field);
    Verdict verdict = Verdict::accepted;
    if (!target_allowed(parsed.primaryTarget, verdict)) {
        return verdict;
    }

    if (!reader.read(kExtraCountWidth, field)) {
        return Verdict::truncated;
    }
    parsed.extraTargetCount = static_cast<std::uint32_t>(field);
    if (parsed.extraTargetCount > kExtraTargetMaximum) {
        return Verdict::tooManyTargets;
    }
    for (std::uint32_t index = 0; index < parsed.extraTargetCount; ++index) {
        if (!reader.read(kTargetWidth, field)) {
            return Verdict::truncated;
        }
        parsed.extraTargets[index] = static_cast<std::uint16_t>(field);
        if (!target_allowed(parsed.extraTargets[index], verdict)) {
            return verdict;
        }
    }

    if (!reader.read(kSelectorPresenceWidth, field)) {
        return Verdict::truncated;
    }
    parsed.hasCompressedSelector = field != 0;
    if (parsed.hasCompressedSelector) {
        if (!reader.read(kSelectorLengthWidth, field)) {
            return Verdict::truncated;
        }
        parsed.selectorLength = static_cast<std::uint32_t>(field);
        if (parsed.selectorLength > kSelectorMaximum) {
            return Verdict::selectorTooLong;
        }
        if (!read_bytes(reader, std::span(parsed.selector).first(parsed.selectorLength))) {
            return Verdict::truncated;
        }
    }

    if (!reader.read(kOptionalPresenceWidth, field)) {
        return Verdict::truncated;
    }
    parsed.hasOptionalBlock = field != 0;
    if (parsed.hasOptionalBlock) {
        if (!reader.read(kOptionalWordWidth, field)) {
            return Verdict::truncated;
        }
        parsed.optionalWordA = static_cast<std::uint32_t>(field);
        if (!reader.read(kOptionalWordWidth, field)) {
            return Verdict::truncated;
        }
        parsed.optionalWordB = static_cast<std::uint32_t>(field);
    }

    if (!reader.read(kPayloadLengthWidth, field)) {
        return Verdict::truncated;
    }
    parsed.payloadLength = static_cast<std::uint32_t>(field);
    if (parsed.payloadLength > kPayloadMaximum) {
        return Verdict::payloadTooLong;
    }
    if (!read_bytes(reader, std::span(parsed.payload).first(parsed.payloadLength))) {
        return Verdict::truncated;
    }
    parsed.hasPayload = parsed.payloadLength != 0;
    parsed.consumedBits = static_cast<std::uint32_t>(payload.size() * encoding::kBitsPerByte
                                                     - reader.remaining_bits());
    return Verdict::accepted;
}

} // namespace sunrise::middleware::bap::activity_message::incident
