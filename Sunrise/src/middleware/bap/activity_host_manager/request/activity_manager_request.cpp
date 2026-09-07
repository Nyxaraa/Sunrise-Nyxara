#include "activity_manager_request.h"

#include <algorithm>
#include <array>

#include "../../../encoding/byte_order.h"

namespace sunrise::middleware::bap::activity_host_manager::request {
namespace {

/** Fixed fields that precede the service-6 protobuf payload. */
struct RequestLayout final {
    /** The request variant discriminator starts the fixed envelope. */
    static constexpr std::size_t discriminator = 0;
    /** The little-endian protobuf length follows the discriminator byte. */
    static constexpr std::size_t declaredLength = discriminator + sizeof(std::byte);
    /** The protobuf starts after its fixed-width length field. */
    static constexpr std::size_t protobuf = declaredLength + encoding::kU32Size;
    /** The fixed request reserves protobuf capacity followed by unused padding. */
    static constexpr std::size_t protobufCapacity = kProtobufCapacity;
    /** Service 6 requires the complete fixed-width request body. */
    static constexpr std::size_t size = kRequestBodySize;
};

/** Discriminator 3 selects the activity-host-manager request variant. */
constexpr std::byte kRequestDiscriminator{3};

/** Writes the service-6 little-endian protobuf length without changing shared byte-order APIs. */
void write_declared_length(std::span<std::byte, encoding::kU32Size> output,
                           std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::byte>(value >> (index * encoding::kBitsPerByte));
    }
}

/** Encodes checked declared and reserved regions through one transactional local body. */
bool encode_regions(std::span<const std::byte> protobuf,
                    std::span<const std::byte> padding,
                    std::span<std::byte> output,
                    std::size_t& written) noexcept {
    written = 0;
    if (protobuf.size() > RequestLayout::protobufCapacity
        || padding.size() != RequestLayout::protobufCapacity - protobuf.size()
        || output.size() < RequestLayout::size) {
        return false;
    }

    std::array<std::byte, RequestLayout::size> body{};
    body[RequestLayout::discriminator] = kRequestDiscriminator;
    write_declared_length(
        std::span(body).subspan<RequestLayout::declaredLength, encoding::kU32Size>(),
        static_cast<std::uint32_t>(protobuf.size()));
    std::copy(protobuf.begin(), protobuf.end(), body.begin() + RequestLayout::protobuf);
    std::copy(padding.begin(),
              padding.end(),
              body.begin() + RequestLayout::protobuf
                  + static_cast<std::ptrdiff_t>(protobuf.size()));
    std::copy(body.begin(), body.end(), output.begin());
    written = body.size();
    return true;
}

} // namespace

/** Checks one service-6 body and borrows only its declared protobuf bytes. */
bool parse_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() != RequestLayout::size
        || input[RequestLayout::discriminator] != kRequestDiscriminator) {
        return false;
    }

    const std::uint32_t declaredLength =
        encoding::read_u32_le(input.subspan<RequestLayout::declaredLength, encoding::kU32Size>());
    if (declaredLength > RequestLayout::protobufCapacity) {
        return false;
    }

    // Both views share the sensitive input lifetime. Callers may inspect only the declared region
    // or use both to perform an exact byte-preserving re-encode.
    request.protobuf =
        input.subspan(RequestLayout::protobuf, static_cast<std::size_t>(declaredLength));
    request.padding = input.subspan(RequestLayout::protobuf + declaredLength);
    return true;
}

/** Re-encodes one parsed request while preserving the complete reserved region. */
bool encode_request(const Request& request,
                    std::span<std::byte> output,
                    std::size_t& written) noexcept {
    return encode_regions(request.protobuf, request.padding, output, written);
}

/** Encodes one request with a zero-filled reserved tail. */
bool encode_request(std::span<const std::byte> protobuf,
                    std::span<std::byte> output,
                    std::size_t& written) noexcept {
    written = 0;
    // The capacity must hold before the padding length below can be computed.
    if (protobuf.size() > RequestLayout::protobufCapacity) {
        return false;
    }
    std::array<std::byte, RequestLayout::protobufCapacity> padding{};
    return encode_regions(
        protobuf,
        std::span(padding).first(RequestLayout::protobufCapacity - protobuf.size()),
        output,
        written);
}

} // namespace sunrise::middleware::bap::activity_host_manager::request
