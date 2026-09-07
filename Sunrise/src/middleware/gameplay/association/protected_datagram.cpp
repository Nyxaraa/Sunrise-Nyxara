#include "protected_datagram.h"

#include "../../crypto/aes_gcm_decrypt.h"
#include "../../crypto/aes_gcm_encrypt.h"
#include "../../encoding/bit_raw.h"
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::association {

namespace {

namespace bits = encoding::bits;

/** Both clear words are 32-bit value fields. */
constexpr std::uint8_t kWordWidth = 32;
/** The mode selector is three bits and the writer emits zero. */
constexpr std::uint8_t kModeWidth = 3;
/** Direct IPv4 uses 11-bit byte-length fields. */
constexpr std::uint8_t kLengthWidth = 11;
/** Only mode zero is produced or accepted; the legacy modes are not negotiated. */
constexpr std::uint64_t kMode = 0;
/** Bits in one byte. */
constexpr unsigned kByteBits = 8;
/** Mask of one byte. */
constexpr std::uint32_t kByteMask = 0xFF;
/**
 * Derives one packet nonce.
 * @param base Association nonce base for this direction.
 * @param wordA Clear word A of the packet.
 * @param wordB Clear word B of the packet.
 * @param output Receives the 12-byte nonce.
 */
void derive_nonce(std::span<const std::byte, kNonceSize> base,
                  std::uint32_t wordA,
                  std::uint32_t wordB,
                  std::span<std::byte, kNonceSize> output) noexcept {
    // Each clear word covers four bytes of the nonce base, in big-endian order.
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    for (std::size_t index = 0; index < kNonceSize; ++index) {
        output[index] = base[index];
    }
    for (std::size_t index = 0; index < kWordBytes; ++index) {
        const unsigned shift = static_cast<unsigned>(kWordBytes - 1 - index) * kByteBits;
        output[index] ^= static_cast<std::byte>((wordA >> shift) & kByteMask);
        output[kWordBytes + index] ^= static_cast<std::byte>((wordB >> shift) & kByteMask);
    }
}

} // namespace

/** Seals one payload as a protected datagram. */
bool seal(ProtectedContext& context,
          std::span<const std::byte> payload,
          std::span<std::byte> output,
          std::size_t& written) noexcept {
    written = 0;
    if (!context.installed || payload.size() > kMaximumPayload
        || output.size() < kProtectedCapacity) {
        return false;
    }
    // Word A advances before the header so no two protected sends share a nonce.
    ++context.outboundWordA;

    std::array<std::byte, kProtectedCapacity> plaintext{};
    std::copy(payload.begin(), payload.end(), plaintext.begin());
    std::copy(context.addressTrailer.begin(),
              context.addressTrailer.end(),
              plaintext.begin() + static_cast<std::ptrdiff_t>(payload.size()));
    const std::size_t plaintextSize = payload.size() + context.addressTrailer.size();

    std::array<std::byte, kNonceSize> nonce{};
    derive_nonce(context.outboundBase, context.outboundWordA, context.outboundWordB, nonce);
    std::array<std::byte, kTagSize> tag{};
    std::array<std::byte, kProtectedCapacity> ciphertext{};
    if (!crypto::aes_gcm::encrypt(context.key,
                                  nonce,
                                  std::span<const std::byte>(plaintext.data(), plaintextSize),
                                  ciphertext,
                                  tag)) {
        return false;
    }
    bits::Writer writer(output.first(output.size() - kTrailerSize));
    std::size_t bodySize = 0;
    if (!writer.write(context.outboundWordA, kWordWidth)
        || !writer.write(context.outboundWordB, kWordWidth) || !writer.write(kMode, kModeWidth)
        || !writer.write(payload.size(), kLengthWidth)
        || !writer.write(context.addressTrailer.size(), kLengthWidth)
        || !bits::write_raw(writer, std::span(ciphertext).first(plaintextSize))
        || !bits::write_raw(writer, tag) || !writer.finish(bodySize)
        || bodySize + kTrailerSize > output.size()) {
        return false;
    }
    std::copy(context.addressTrailer.begin(),
              context.addressTrailer.end(),
              output.begin() + static_cast<std::ptrdiff_t>(bodySize));
    written = bodySize + kTrailerSize;
    return true;
}

/** Opens one received protected datagram. */
bool open(const ProtectedContext& context,
          std::span<const std::byte> datagram,
          std::span<std::byte> payload,
          std::size_t& payloadSize,
          std::uint32_t& wordA) noexcept {
    payloadSize = 0;
    if (!context.installed || datagram.size() <= kTrailerSize + kTagSize
        || datagram.size() > kProtectedCapacity) {
        return false;
    }
    bits::Reader reader(datagram.first(datagram.size() - kTrailerSize));
    std::uint64_t receivedWordA = 0;
    std::uint64_t receivedWordB = 0;
    std::uint64_t mode = 0;
    std::uint64_t declaredPayload = 0;
    std::uint64_t declaredTrailer = 0;
    if (!reader.read(kWordWidth, receivedWordA) || !reader.read(kWordWidth, receivedWordB)
        || !reader.read(kModeWidth, mode) || mode != kMode
        || !reader.read(kLengthWidth, declaredPayload)
        || !reader.read(kLengthWidth, declaredTrailer)) {
        return false;
    }
    const std::size_t cipherSize = static_cast<std::size_t>(declaredPayload + declaredTrailer);
    std::array<std::byte, kProtectedCapacity> ciphertext{};
    std::array<std::byte, kTagSize> tag{};
    if (cipherSize > ciphertext.size()
        || !bits::read_raw(reader, std::span(ciphertext).first(cipherSize))
        || !bits::read_raw(reader, tag)) {
        return false;
    }
    std::array<std::byte, kNonceSize> nonce{};
    derive_nonce(context.inboundBase,
                 static_cast<std::uint32_t>(receivedWordA),
                 static_cast<std::uint32_t>(receivedWordB),
                 nonce);
    std::array<std::byte, kProtectedCapacity> plaintext{};
    if (!crypto::aes_gcm::decrypt(
            context.key, nonce, std::span(ciphertext).first(cipherSize), tag, plaintext)) {
        return false;
    }
    const auto declared = static_cast<std::size_t>(declaredPayload);
    if (declared > payload.size()) {
        return false;
    }
    std::copy_n(plaintext.begin(), declared, payload.begin());
    payloadSize = declared;
    wordA = static_cast<std::uint32_t>(receivedWordA);
    return true;
}

} // namespace sunrise::middleware::gameplay::association
