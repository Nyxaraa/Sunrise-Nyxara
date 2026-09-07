#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "world_globals_state.h"

namespace sunrise::middleware::bap::activity_message::world_globals_state {
namespace {

namespace bits = encoding::bits;

/** Consumes the seven zero bits after the fixed 33-bit body. */
[[nodiscard]] bool finish_padding(bits::Reader& reader) noexcept {
    std::uint64_t padding = 0;
    return reader.remaining_bits() == 7 && reader.read(7, padding) && padding == 0
           && reader.remaining_bits() == 0;
}

} // namespace

/** Encodes the exact bool followed by the unaligned 32-bit real value. */
bool encode(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kByteCount));
    return writer.write(body.flag ? 1U : 0U, 1) && writer.write(body.real32Bits, 32)
           && writer.bit_count() == kBitCount && writer.finish(written) && written == kByteCount;
}

/** Decodes one exact five-byte body and rejects nonzero trailing padding. */
bool decode(std::span<const std::byte> input, Body& body) noexcept {
    if (input.size() != kByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t flag = 0;
    std::uint64_t real32 = 0;
    if (!reader.read(1, flag) || !reader.read(32, real32) || !finish_padding(reader)) {
        return false;
    }
    const Body parsed{flag != 0, static_cast<std::uint32_t>(real32)};
    body = parsed;
    return true;
}

/** Checks one exact message-2 body without retaining its decoded state. */
bool validate(std::span<const std::byte> input) noexcept {
    Body body{};
    return decode(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::world_globals_state
