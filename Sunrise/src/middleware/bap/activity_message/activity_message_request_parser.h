#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace sunrise::middleware::bap::activity_message {

/**
 * Checks either native service-8 body arm and borrows its exact declared payload. Discriminator 1
 * carries a peer-heard mask; discriminator 2 omits it. The payload can hold identity and session
 * fields, and must not outlive the input storage.
 * @param input Complete body after the generic 6-byte BAP request header.
 * @param request Cleared first. Receives fields only on success.
 * @return True when one complete arm and its exact payload length are valid.
 */
[[nodiscard]] bool parse_request(std::span<const std::byte> input, Request& request) noexcept;

} // namespace sunrise::middleware::bap::activity_message
