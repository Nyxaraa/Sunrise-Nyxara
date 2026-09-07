#include "control_packet.h"

#include "../../encoding/bit_raw.h"
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::association {

namespace {

namespace bits = encoding::bits;

/** Both association words are 32-bit value fields. */
constexpr std::uint8_t kWordWidth = 32;
/** The opcode is a 3-bit value field. */
constexpr std::uint8_t kOpcodeWidth = 3;
/** The presence of the relayed SignOn field is one bit. */
constexpr std::uint8_t kPresenceWidth = 1;
/** The response echo is a 32-bit value field. */
constexpr std::uint8_t kResponseWidth = 32;
/** Highest opcode the enum defines. The three-bit field also holds 6 and 7, which are refused. */
constexpr std::uint64_t kMaximumOpcode = 5;
/** @return True when the opcode carries the full key-exchange body. */
[[nodiscard]] bool carries_body(Opcode opcode) noexcept {
    return opcode == Opcode::keyExchangeOffer || opcode == Opcode::keyExchangeResponse;
}

/**
 * Reads the key-exchange body that both the offer and the response carry.
 * @param reader Positioned just after the opcode.
 * @param output Packet receiving the body fields.
 * @return True when every field was present.
 */
[[nodiscard]] bool read_body(bits::Reader& reader, ControlPacket& output) noexcept {
    if (!bits::read_raw(reader, output.networkId) || !bits::read_raw(reader, output.identityNonce)
        || !bits::read_raw(reader, output.verifier)) {
        return false;
    }
    std::uint64_t present = 0;
    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    output.hasSignOnBlob = present != 0;
    if (output.hasSignOnBlob && !bits::read_raw(reader, output.signOnBlob)) {
        return false;
    }
    if (!bits::read_raw(reader, output.seed) || !bits::read_raw(reader, output.publicValue)) {
        return false;
    }
    if (output.opcode != Opcode::keyExchangeResponse) {
        return true;
    }
    std::uint64_t response = 0;
    if (!reader.read(kResponseWidth, response)) {
        return false;
    }
    output.responseValue = static_cast<std::uint32_t>(response);
    return true;
}

/**
 * Writes the key-exchange body that both the offer and the response carry.
 * @param writer Positioned just after the opcode.
 * @param packet Source fields.
 * @return True when every field fit the output.
 */
[[nodiscard]] bool write_body(bits::Writer& writer, const ControlPacket& packet) noexcept {
    if (!bits::write_raw(writer, packet.networkId) || !bits::write_raw(writer, packet.identityNonce)
        || !bits::write_raw(writer, packet.verifier)
        || !writer.write(packet.hasSignOnBlob ? 1U : 0U, kPresenceWidth)) {
        return false;
    }
    if (packet.hasSignOnBlob && !bits::write_raw(writer, packet.signOnBlob)) {
        return false;
    }
    if (!bits::write_raw(writer, packet.seed) || !bits::write_raw(writer, packet.publicValue)) {
        return false;
    }
    if (packet.opcode != Opcode::keyExchangeResponse) {
        return true;
    }
    return writer.write(packet.responseValue, kResponseWidth);
}

} // namespace

/** Decodes one received control datagram. */
bool decode(std::span<const std::byte> datagram, ControlPacket& output) noexcept {
    if (datagram.size() <= kTrailerSize) {
        return false;
    }
    // The address demux strips the clear trailer before any control field is read.
    bits::Reader reader(datagram.subspan(0, datagram.size() - kTrailerSize));
    std::uint64_t wordA = 0;
    std::uint64_t wordB = 0;
    std::uint64_t opcode = 0;
    if (!reader.read(kWordWidth, wordA) || !reader.read(kWordWidth, wordB)
        || !reader.read(kOpcodeWidth, opcode) || opcode > kMaximumOpcode) {
        return false;
    }
    ControlPacket candidate{};
    candidate.wordA = static_cast<std::uint32_t>(wordA);
    candidate.wordB = static_cast<std::uint32_t>(wordB);
    candidate.opcode = static_cast<Opcode>(opcode);
    if (carries_body(candidate.opcode) && !read_body(reader, candidate)) {
        return false;
    }
    std::copy_n(datagram.end() - static_cast<std::ptrdiff_t>(kTrailerSize),
                kTrailerSize,
                candidate.trailer.begin());
    output = candidate;
    return true;
}

/** Encodes one control datagram. */
bool encode(const ControlPacket& packet,
            std::span<std::byte> output,
            std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kControlCapacity) {
        return false;
    }
    bits::Writer writer(output.subspan(0, output.size() - kTrailerSize));
    if (!writer.write(packet.wordA, kWordWidth) || !writer.write(packet.wordB, kWordWidth)
        || !writer.write(static_cast<std::uint64_t>(packet.opcode), kOpcodeWidth)) {
        return false;
    }
    if (carries_body(packet.opcode) && !write_body(writer, packet)) {
        return false;
    }
    std::size_t bodySize = 0;
    if (!writer.finish(bodySize)) {
        return false;
    }
    // The trailer names the destination. It sits outside the bit padding.
    std::copy(packet.trailer.begin(),
              packet.trailer.end(),
              output.begin() + static_cast<std::ptrdiff_t>(bodySize));
    written = bodySize + kTrailerSize;
    return true;
}

} // namespace sunrise::middleware::gameplay::association
