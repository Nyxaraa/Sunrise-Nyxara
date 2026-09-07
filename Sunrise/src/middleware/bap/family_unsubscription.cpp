#include "family_unsubscription.h"

#include "../encoding/byte_order.h"

namespace sunrise::middleware::bap::family_unsubscription {
namespace {

/** The 1-byte family selector starts the request body, at the same offset svc 12 uses. */
constexpr std::size_t kFamilyTypeOffset = 0;
/** The 8-byte big-endian root id follows the family selector. */
constexpr std::size_t kFamilyRootOffset = kFamilyTypeOffset + sizeof(std::uint8_t);
/** The svc-14 body is exactly the family selector and the root id, the same as svc 12. */
constexpr std::size_t kBodySize = kFamilyRootOffset + encoding::kU64Size;

} // namespace

/** Decodes one fixed authenticated family-unsubscription request. */
bool parse(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    // The native decoder refuses any svc-14 body that is not exactly this size.
    if (input.size() != kBodySize) {
        return false;
    }
    request.familyType = std::to_integer<std::uint8_t>(input[kFamilyTypeOffset]);
    request.familyRootSoid = encoding::read_u64_be(std::span<const std::byte, encoding::kU64Size>(
        input.data() + kFamilyRootOffset, encoding::kU64Size));
    return true;
}

} // namespace sunrise::middleware::bap::family_unsubscription
