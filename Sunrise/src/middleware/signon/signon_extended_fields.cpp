#include "signon_extended_fields.h"

#include <array>
#include <span>

namespace sunrise::middleware::signon::extended {
namespace {

/** Success field 12 carries the optional extended sub-message. */
constexpr unsigned kExtendedField = 12;
/** The server fills field 1 of the extended sub-message. Fields 2 and 3 are request-side only. */
constexpr unsigned kNetworkIdField = 1;
/**
 * Relayed into the peer key exchange as 64 bits and never validated. Kept in the shape a real
 * server uses so a reader that range-checks the class nibble still accepts it.
 */
constexpr std::uint64_t kNetworkId = 0x4000000000000001ULL;
/** The extended sub-message holds one varint and its field key. */
constexpr std::size_t kExtendedBufferSize = 16;

/**
 * Encodes the extended sub-message into fixed storage.
 * Field 4, the server's 128-byte public key, is omitted. The applier zeroes its block for any
 * other length, and a synthetic key would ship invented material to peers.
 * @param size Cleared, then receives the encoded byte count.
 * @return True when the network id fits.
 */
[[nodiscard]] bool encode_extended(std::span<std::byte> output, std::size_t& size) noexcept {
    size = 0;
    Writer writer(output);
    if (!writer.varint(kNetworkIdField, kNetworkId)) {
        return false;
    }
    size = writer.size();
    return true;
}

} // namespace

/** Appends the optional SignOn success extended sub-message. */
bool append(Writer& success) noexcept {
    std::array<std::byte, kExtendedBufferSize> extended{};
    std::size_t extendedSize = 0;
    return encode_extended(extended, extendedSize)
           && success.bytes(kExtendedField, std::span(extended).first(extendedSize));
}

} // namespace sunrise::middleware::signon::extended
