#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::family_unsubscription {

/** Client-chosen family and root released by one authenticated request. */
struct Request {
    std::uint8_t familyType{};
    std::uint64_t familyRootSoid{};
};

/**
 * Decodes one fixed authenticated family-unsubscription request.
 * @param input Svc-14 body with the family and root prefix.
 * @param request Receives the family type and the client-chosen family root.
 * @return True when the body has both fixed prefix fields.
 */
[[nodiscard]] bool parse(std::span<const std::byte> input, Request& request) noexcept;

} // namespace sunrise::middleware::bap::family_unsubscription
