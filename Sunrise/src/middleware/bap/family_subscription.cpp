#include "family_subscription.h"

#include "../encoding/byte_order.h"

namespace sunrise::middleware::bap::family_subscription {
namespace {

/** The 1-byte family selector starts the authenticated request body. */
constexpr std::size_t kFamilyTypeOffset = 0;
/** The 8-byte big-endian root id follows the family selector. */
constexpr std::size_t kFamilyRootOffset = kFamilyTypeOffset + sizeof(std::uint8_t);
/** The svc-12 body is exactly the family selector and the root id. */
constexpr std::size_t kBodySize = kFamilyRootOffset + encoding::kU64Size;

} // namespace

/** Decodes the fixed authenticated family-subscription selector. */
bool parse(std::span<const std::byte> input, queuez::Subscription& subscription) noexcept {
    subscription = {};
    // The native decoder refuses any svc-12 body that is not exactly this size.
    if (input.size() != kBodySize) {
        return false;
    }
    subscription.familyType = std::to_integer<std::uint8_t>(input[kFamilyTypeOffset]);
    subscription.familyRootSoid =
        encoding::read_u64_be(std::span<const std::byte, encoding::kU64Size>(
            input.data() + kFamilyRootOffset, encoding::kU64Size));
    return true;
}

} // namespace sunrise::middleware::bap::family_subscription
