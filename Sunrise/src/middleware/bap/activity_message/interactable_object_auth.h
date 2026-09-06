#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::interactable_object {
inline constexpr std::uint32_t kSchema = 0x8080992FU;
inline constexpr std::uint32_t kReply = 0x80804FB7U;
inline constexpr std::size_t kBits = 375, kBytes = 47;
inline constexpr std::size_t kOwnerBits = kBits + 33, kOwnerBytes = (kOwnerBits + 7) / 8;
// Authored entry/transform plus one interaction subscription. F32220 emits 80804FB7
// only when 80804FB8 exists. Revision zero preserves the native one-shot used latch;
// F36640, reached after accepted player use, is responsible for setting it.
[[nodiscard]] inline bool encode(std::int32_t generation, std::span<std::byte> output,
                                 std::size_t& written, bool trackOwner = false, bool active = true) noexcept {
    written = 0;
    const auto bytes = trackOwner ? kOwnerBytes : kBytes;
    const auto bits = trackOwner ? kOwnerBits : kBits;
    if (generation <= 0 || output.size() < bytes) return false;
    encoding::bits::Writer w(output.first(bytes));
    const auto absent = [&]() { return w.write(0x811C9DC5U,32) && w.write(0,7) && w.write(0x7FFF,16); };
    return w.write(static_cast<std::uint32_t>(generation)+0x80000000U,32)
        && w.write(0x80000000U,32) && w.write(active ? 1 : 0,1) && w.write(0,1)
        && w.write(0x80000000U,32) && absent()
        && w.write(0,32) && w.write(0,32) && w.write(0,32) && w.write(0,1)
        && w.write(trackOwner ? 2 : 1,2) && w.write(1,1) && w.write(0x80804FB8U,32)
        // F33930 maps native -1 (wire 0) to +704=false. F32CD0 permits use
        // when that flag is false and no player filter is attached. Native +1
        // (wire 2) inverts that default and HIDES use without a matching filter.
        // Authored item/geometry criteria are still evaluated by F32CD0.
        && w.write(0,2) && absent() && w.write(0x80000000U,32) && w.write(0,1)
        // Native 9F0750 accepts empty 80809ACD and returns ownership in 80809ACC.
        && (!trackOwner || (w.write(1,1) && w.write(0x80809ACDU,32)))
        && w.bit_count()==bits && w.finish(written) && written==bytes;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    const bool owner = bits == kOwnerBits;
    if ((!owner && bits != kBits) || input.size() != (owner ? kOwnerBytes : kBytes)) return false;
    encoding::bits::Reader reader(input);
    std::uint64_t generation{};
    if (!reader.read(32,generation) || generation <= 0x80000000ULL) return false;
    std::uint64_t active{};
    if (!reader.skip(32) || !reader.read(1, active)) return false;
    std::uint64_t mode{};
    if (!reader.skip(220) || !reader.read(2, mode) || mode != 0) return false;
    std::array<std::byte,kOwnerBytes> canonical{};
    std::size_t written{};
    return encode(static_cast<std::int32_t>(generation-0x80000000ULL),canonical,written,owner,active != 0)
        && std::equal(input.begin(),input.end(),canonical.begin());
}
}
