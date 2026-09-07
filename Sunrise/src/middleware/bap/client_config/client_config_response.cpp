#include "client_config_response.h"

#include <cstdint>

#include "../../protobuf/codec.h"

namespace sunrise::middleware::bap::client_config {
namespace {

/** Svc 19 requires protobuf field 1. */
constexpr std::uint32_t kFirstRequiredFieldNumber = 1;
/** Svc 19 also requires protobuf field 2. */
constexpr std::uint32_t kSecondRequiredFieldNumber = 2;
/** The minimal response encodes both required fields as zero. */
constexpr std::uint64_t kRequiredFieldValue = 0;
/** Each required field is a 1-byte key and a 1-byte zero varint. */
constexpr std::size_t kResponseSize = 4;

} // namespace

/** Encodes the minimal svc-19 response with its optional bytes omitted. */
bool encode_minimal_response(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kResponseSize) {
        return false;
    }
    protobuf::Writer writer(output);
    if (!writer.write_varint(kFirstRequiredFieldNumber, kRequiredFieldValue)
        || !writer.write_varint(kSecondRequiredFieldNumber, kRequiredFieldValue)) {
        return false;
    }
    written = writer.size();
    return true;
}

} // namespace sunrise::middleware::bap::client_config
