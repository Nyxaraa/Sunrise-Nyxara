#include "srp_exchange.h"

#include <Windows.h>

#include <span>

#include "../../crypto/random_bytes.h"

namespace sunrise::middleware::gameplay::association::srp {

namespace {

namespace modular = crypto::modular;

/** Private exponents are 32 bytes, the width the remote peer also generates. */
constexpr std::size_t kPrivateSize = 32;
/** Generator of the group below. It is fixed at 2 by the group's definition. */
constexpr std::uint64_t kGenerator = 2;
/** Multiplier that scales the verifier into the public value. SRP fixes it at 3. */
constexpr std::uint64_t kMultiplier = 3;

/**
 * The published 1024-bit SRP group modulus, least significant limb first.
 * Both endpoints are fixed to this group. It is a public parameter and carries no secret.
 */
constexpr modular::Number kGroupModulus{
    0x9FC61D2FC0EB06E3ULL,
    0xFD5138FE8376435BULL,
    0x2FD4CBF4976EAA9AULL,
    0x68EDBC3C05726CC0ULL,
    0xC529F566660E57ECULL,
    0x82559B297BCF1885ULL,
    0xCE8EF4AD69B15D49ULL,
    0x5DC7D7B46154D6B6ULL,
    0x8E495C1D6089DAD1ULL,
    0xE0D5D8E250B98BE4ULL,
    0x383B4813D692C6E0ULL,
    0xD674DF7496EA81D3ULL,
    0x9EA2314C9C256576ULL,
    0x6072618775FF3C0BULL,
    0x9C33F80AFA8FC5E8ULL,
    0xEEAF0AB9ADB38DD6ULL,
};

/**
 * Checks one exchanged public value against the Sunrise range policy.
 * @param value Imported integer.
 * @param modulus Prepared group.
 * @return True when the value is nonzero and below the modulus.
 */
[[nodiscard]] bool in_range(const modular::Number& value,
                            const modular::Modulus& modulus) noexcept {
    return !modular::is_zero(value) && modular::less_than(value, modulus.value);
}

/**
 * Right-aligns a short value inside one fixed-width integer.
 * @param source Bytes shorter than one integer.
 * @param output Receives the zero-padded integer.
 */
void left_pad(std::span<const std::byte> source,
              std::array<std::byte, kIntegerSize>& output) noexcept {
    output = {};
    for (std::size_t index = 0; index < source.size(); ++index) {
        output[kIntegerSize - source.size() + index] = source[index];
    }
}

/**
 * Computes the scrambling parameter from both public values.
 * @param clientPublic Offered public value in wire form.
 * @param serverPublic Answered public value in wire form.
 * @param output Receives the parameter as a reduced integer.
 * @return True when the hash succeeded.
 */
[[nodiscard]] bool scramble(const Integer& clientPublic,
                            const Integer& serverPublic,
                            modular::Number& output) noexcept {
    crypto::sha256::Digest digest{};
    if (!crypto::sha256::hash_pair(clientPublic, serverPublic, digest)) {
        return false;
    }
    // The digest is shorter than one integer, so it enters the arithmetic left zero padded.
    std::array<std::byte, kIntegerSize> padded{};
    left_pad(digest, padded);
    modular::import_big_endian(padded, output);
    return true;
}

} // namespace

/** Runs the server half of the exchange. */
bool derive(Exchange& exchange) noexcept {
    exchange.complete = false;
    // Preparing the group is far cheaper than one exponentiation, so it stays call local.
    modular::Modulus modulus{};
    if (!modular::prepare(kGroupModulus, modulus)) {
        return false;
    }
    modular::Number verifier{};
    modular::Number clientPublic{};
    modular::import_big_endian(exchange.verifier, verifier);
    modular::import_big_endian(exchange.clientPublic, clientPublic);
    if (!in_range(verifier, modulus) || !in_range(clientPublic, modulus)) {
        return false;
    }

    std::array<std::byte, kPrivateSize> privateBytes{};
    if (!crypto::random::fill(privateBytes)) {
        return false;
    }
    std::array<std::byte, kIntegerSize> paddedPrivate{};
    left_pad(privateBytes, paddedPrivate);
    modular::Number exponent{};
    modular::import_big_endian(paddedPrivate, exponent);
    SecureZeroMemory(privateBytes.data(), privateBytes.size());
    SecureZeroMemory(paddedPrivate.data(), paddedPrivate.size());

    modular::Number generator{};
    modular::set_small(generator, kGenerator);
    modular::Number multiplier{};
    modular::set_small(multiplier, kMultiplier);

    modular::Number serverPublic{};
    modular::power(modulus, generator, exponent, serverPublic);
    modular::Number scaled{};
    modular::multiply(modulus, multiplier, verifier, scaled);
    modular::add(modulus, scaled, serverPublic, serverPublic);
    modular::export_big_endian(serverPublic, exchange.serverPublic);

    modular::Number parameter{};
    bool derived = scramble(exchange.clientPublic, exchange.serverPublic, parameter);
    if (derived) {
        modular::Number shared{};
        modular::power(modulus, verifier, parameter, shared);
        modular::multiply(modulus, clientPublic, shared, shared);
        modular::power(modulus, shared, exponent, shared);
        std::array<std::byte, kIntegerSize> sharedBytes{};
        modular::export_big_endian(shared, sharedBytes);
        derived = crypto::sha256::hash(sharedBytes, exchange.derivedKey);
        SecureZeroMemory(sharedBytes.data(), sharedBytes.size());
        SecureZeroMemory(shared.data(), shared.size() * sizeof(std::uint64_t));
    }
    SecureZeroMemory(exponent.data(), exponent.size() * sizeof(std::uint64_t));
    exchange.complete = derived;
    return derived;
}

} // namespace sunrise::middleware::gameplay::association::srp
