#include "user_message_response.h"

#include <cstdint>

#include "../../protobuf/codec.h"

namespace sunrise::middleware::bap::user_message {
namespace {

/** Svc 33 stores its required value in protobuf field 4. */
constexpr std::uint32_t kRequiredFieldNumber = 4;
/** The smallest seen response encodes required field 4 as zero. */
constexpr std::uint64_t kRequiredFieldValue = 0;

} // namespace

/** Encodes the minimal svc-33 response with all optional strings omitted. */
bool encode_minimal_response(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    protobuf::Writer writer(output);
    if (!writer.write_varint(kRequiredFieldNumber, kRequiredFieldValue)) {
        return false;
    }
    written = writer.size();
    return true;
}

} // namespace sunrise::middleware::bap::user_message
