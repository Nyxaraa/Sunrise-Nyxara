#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../crypto/hmac.h"
#include "association_keys.h"

namespace sunrise::middleware::gameplay::dtls {

/** Header, authentication tag and length open every record. */
inline constexpr std::size_t kRecordPrefixSize = 18;
/** The authentication tag is the leading bytes of a wider digest. */
inline constexpr std::size_t kRecordTagSize = 8;
/** Largest plaintext one record carries. */
inline constexpr std::size_t kRecordPayloadCapacity = 1400;
/** Block padding the cipher may add past the plaintext. */
inline constexpr std::size_t kRecordPaddingCapacity = 16;
/** Largest record this layer builds. */
inline constexpr std::size_t kRecordCapacity =
    kRecordPrefixSize + kRecordPayloadCapacity + kRecordPaddingCapacity;

/** Everything one record needs from its association. */
struct RecordContext {
    /** Keys the derivation produced. */
    Keys keys{};
    /** Digest the authentication tag is keyed with. */
    crypto::hmac::Algorithm authAlgorithm{crypto::hmac::Algorithm::sha256};
    /** Tag a record we send carries. Every record names the tag its receiver chose for itself. */
    std::uint16_t sendTag{};
};

/**
 * Reads the tag one received record is addressed to, without opening it.
 * The tag is the one its receiver chose. It picks the association out of several on one endpoint.
 * @param datagram Whole received record.
 * @param tag Receives the addressed tag.
 * @return True when the datagram is long enough to carry a header.
 */
[[nodiscard]] bool read_record_tag(std::span<const std::byte> datagram,
                                   std::uint16_t& tag) noexcept;

/**
 * Opens one received record.
 * @param context Association keys and tag.
 * @param datagram Whole received record.
 * @param plaintext Receives the decrypted payload.
 * @param size Receives the payload length.
 * @param sequence Receives the record's sequence number.
 * @return True when the tag verified and the record decrypted.
 */
[[nodiscard]] bool open(const RecordContext& context,
                        std::span<const std::byte> datagram,
                        std::span<std::byte> plaintext,
                        std::size_t& size,
                        std::uint32_t& sequence) noexcept;

/**
 * Builds one record for sending.
 * @param context Association keys and tag.
 * @param sequence Sequence number this record carries.
 * @param payload Plaintext to seal.
 * @param datagram Receives the whole record.
 * @param size Receives the record length.
 * @return True when the record fit and every stage succeeded.
 */
[[nodiscard]] bool seal(const RecordContext& context,
                        std::uint32_t sequence,
                        std::span<const std::byte> payload,
                        std::span<std::byte> datagram,
                        std::size_t& size) noexcept;

/**
 * Reports which digest authenticates a received record.
 * The peer never names its choice on the wire. Identify it once, then hold it for the association.
 * @param keys Association keys.
 * @param datagram Whole received record.
 * @param output Receives the digest only when one of them verifies.
 * @return True when a candidate verified.
 */
[[nodiscard]] bool identify_auth(const Keys& keys,
                                 std::span<const std::byte> datagram,
                                 crypto::hmac::Algorithm& output) noexcept;

} // namespace sunrise::middleware::gameplay::dtls
