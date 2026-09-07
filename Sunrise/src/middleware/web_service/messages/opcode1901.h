#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode1901 {

/** Web Service opcode used by the equipped-item shader application action. */
inline constexpr std::uint16_t kOpcode = 1901;

/** The equipment selector carries only the low 62 bits of the item instance's identity. */
inline constexpr std::uint64_t kInstanceIdentityMask = 0x3FFFFFFFFFFFFFFFULL;

/** The native replacement array reserves twelve entries, which its 4-bit count can address. */
inline constexpr std::size_t kReplacementCapacity = 12;

/** One socket replacement: which plug goes into which socket of the named item. */
struct Replacement {
    std::uint16_t plugDefinitionIndex{};
    std::uint8_t canonicalSocketKind{};
    std::uint8_t modelSocketKind{};
    std::uint32_t socketIndex{};
    std::uint64_t auxiliary{};
};

/**
 * Exact logical fields carried by the native equipped socket-action descriptor.
 * The descriptor is a counted run of replacements followed by the one item they apply to, so its
 * width grows with the count rather than being fixed.
 */
struct Request {
    std::array<Replacement, kReplacementCapacity> replacements{};
    std::size_t replacementCount{};
    /** Selector exactly as the descriptor carries it, before any decode. */
    std::uint64_t equipmentSelector{};
    /** The item-instance identity that selector encodes, already decoded. */
    std::uint64_t instanceIdentityToken{};
};

/**
 * Answers whether one decoded selector names a given item instance.
 * The selector carries only part of the instance identity, so this compares against that part
 * rather than testing equality. Callers hold whole identities and pass them unchanged.
 * @param instanceIdentityToken Decoded selector from a parsed request.
 * @param instanceSoid Whole item-instance identity held by State.
 * @return True when the selector names that instance.
 */
[[nodiscard]] bool identifies_instance(std::uint64_t instanceIdentityToken,
                                       std::uint64_t instanceSoid) noexcept;

/**
 * Parses the exact reflected opcode-1901 descriptor.
 * The request is a bounded replacement array followed by the target's equipment selector. Every
 * optional identity in that array and target is required.
 * @param message Parsed Web Service envelope.
 * @param request Receives the plug, socket descriptor, auxiliary identity, and equipment slot.
 * @return True only for the complete canonical 24-byte one-replacement request.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode1901
