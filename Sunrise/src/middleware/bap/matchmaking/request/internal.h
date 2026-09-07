#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../protobuf/codec.h"

namespace sunrise::middleware::bap::matchmaking::request {

/** One wanted protobuf field and the first occurrence found for it. */
struct FieldPick final {
    /** Field number to match. */
    std::uint32_t fieldNumber{};
    /** Wire type the field must carry to be usable here. */
    protobuf::WireType wireType{protobuf::WireType::varint};
    /** True once any occurrence of fieldNumber was met. */
    bool seen{};
    /** True when the first occurrence also carried the wanted wire type. */
    bool has{};
    /** Value of a usable varint pick, else zero. */
    std::uint64_t value{};
    /** Borrowed payload of a usable length-delimited pick, else empty. */
    std::span<const std::byte> bytes{};
};

/**
 * Records the first occurrence of each wanted field in one whole protobuf message.
 * A first occurrence with the wrong wire type is unusable but still blocks later duplicates.
 * @param input Complete protobuf message.
 * @param picks Wanted fields, updated in place.
 * @return True when every field in the message has a whole, supported wire encoding.
 */
[[nodiscard]] bool select_first_fields(std::span<const std::byte> input,
                                       std::span<FieldPick> picks) noexcept;

} // namespace sunrise::middleware::bap::matchmaking::request
