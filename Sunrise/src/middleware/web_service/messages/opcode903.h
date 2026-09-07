#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode903 {

/** Web Service opcode used by an ordinary item socket-plug insertion. */
inline constexpr std::uint16_t kOpcode = 903;

/** Exact logical fields carried by the native 144-bit socket action descriptor. */
struct Request {
    std::uint64_t instanceSoid{};
    std::uint32_t socketIndex{};
    std::uint16_t targetDefinitionIndex{};
    std::uint16_t plugDefinitionIndex{};
    bool hasInstance{};
    bool hasTargetDefinition{};
    bool hasPlugDefinition{};
};

/**
 * Parses the exact reflected opcode-903 descriptor, including both alignment layers.
 * The item identity names either an instance or an uninstanced definition. State decides which
 * shapes it supports once this codec has proved the wire representation.
 * @param message Parsed Web Service envelope.
 * @param request Receives the decoded target, socket lane, and plug definition.
 * @return True only for the complete canonical 18-byte request.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode903
