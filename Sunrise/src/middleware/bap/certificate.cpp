#include "certificate.h"

#include <cstdint>

#include "../protobuf/codec.h"

namespace sunrise::middleware::bap::certificate {
namespace {

/** Svc304 and svc305 carry their certificate wrapper in field 3. */
constexpr std::uint32_t kCertificateField = 3;
/** Svc305 field 1 reports success with numeric value zero. */
constexpr std::uint32_t kResultField = 1;
constexpr std::uint64_t kSuccessResult = 0;

/**
 * Finds the first valid length-delimited protobuf field.
 * @param input Complete protobuf message.
 * @param wantedField Numeric field to borrow.
 * @return Borrowed payload, or an empty span when absent or malformed.
 */
[[nodiscard]] std::span<const std::byte> find_length_delimited(std::span<const std::byte> input,
                                                               std::uint32_t wantedField) noexcept {
    protobuf::Reader reader(input);
    protobuf::Field field{};
    while (reader.next(field)) {
        if (field.fieldNumber == wantedField
            && field.wireType == protobuf::WireType::lengthDelimited) {
            // The first wrapper wins. Later bytes are left unread on purpose.
            return field.bytes;
        }
    }
    return {};
}

} // namespace

/** Rewraps a svc304 request certificate into the svc305 protobuf body. */
bool encode_response(std::span<const std::byte> requestBody,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    const auto certificate = find_length_delimited(requestBody, kCertificateField);
    // Size is proven up front so a rejected response leaves the whole output untouched.
    std::size_t resultSize = 0;
    std::size_t wrapperSize = 0;
    if (!protobuf::measure_varint_field(kResultField, kSuccessResult, resultSize)
        || !protobuf::measure_length_delimited_field(
            kCertificateField, certificate.size(), wrapperSize)
        || output.size() < resultSize + wrapperSize) {
        return false;
    }

    protobuf::Writer writer(output);
    if (!writer.write_varint(kResultField, kSuccessResult)
        || !writer.write_length_delimited(kCertificateField, certificate)) {
        return false;
    }
    written = writer.size();
    return true;
}

} // namespace sunrise::middleware::bap::certificate
