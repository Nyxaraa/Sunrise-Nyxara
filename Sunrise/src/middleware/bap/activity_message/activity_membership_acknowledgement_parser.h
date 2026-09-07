#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::membership_acknowledgement {

/** Membership acknowledgements use activity message type 38. */
inline constexpr std::uint32_t kMessageType = 38;
/** One big-endian revision forms the complete acknowledgement body. */
inline constexpr std::size_t kEncodedSize = 4;

/** Revision of the last membership snapshot applied by the client. */
struct MembershipAcknowledgement final {
    std::uint32_t membershipRevision{};
};

/**
 * Parses the complete fixed membership-acknowledgement body.
 * @param input Activity message body holding exactly 4 bytes.
 * @param acknowledgement Cleared first. Filled in only on success.
 * @return True only when the whole big-endian revision is present with no tail.
 */
[[nodiscard]] bool
parse_membership_acknowledgement(std::span<const std::byte> input,
                                 MembershipAcknowledgement& acknowledgement) noexcept;

} // namespace sunrise::middleware::bap::activity_message::membership_acknowledgement
