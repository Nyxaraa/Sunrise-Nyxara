#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace sunrise::middleware::bap::matchmaking::request {

/** Checked first-occurrence fields borrowed from the service-42 root. */
struct Root final {
    RequestKind kind{RequestKind::none};
    bool hasAdvertisementMessage{};
    std::span<const std::byte> advertisementMessage{};
    bool hasActivityMessage{};
    std::span<const std::byte> activityMessage{};
};

/**
 * Picks the fields we need and checks the whole service-42 root.
 * @param input Whole service-42 protobuf body.
 * @param selected Receives the typed kind and an optional borrowed advertisement message.
 * @return True when every root field has a whole, supported wire encoding.
 */
[[nodiscard]] bool parse_root(std::span<const std::byte> input, Root& selected) noexcept;

/**
 * Parses the kind-2 path into borrowed request fields.
 * @param input Root advertisement submessage.
 * @param update Receives scalar values and an optional borrowed descriptor.
 * @return True when every nested message it reads is valid.
 */
[[nodiscard]] bool parse_update(std::span<const std::byte> input,
                                AdvertisementUpdate& update) noexcept;

/**
 * Parses the kind-4 activity submessage.
 * @param input Root field-11 submessage.
 * @param activity Receives the target's hashes.
 * @return True when every field has a whole, supported wire encoding.
 */
[[nodiscard]] bool parse_activity(std::span<const std::byte> input,
                                  ConfigurationActivity& activity) noexcept;

} // namespace sunrise::middleware::bap::matchmaking::request
