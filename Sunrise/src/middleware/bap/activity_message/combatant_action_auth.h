#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::combatant_action {
inline constexpr std::uint32_t kSchema = 0x80807DA1;
inline constexpr std::size_t kBits = 254, kBytes = 32;
struct Request {
    std::uint32_t generation{}, revision{}, group{}, action{};
};
// One native kind-9 custom action, with no spatial target. Keep the spawn
// generation while advancing the program revision; completion uses Sense .3.
// A97800 -> AB4030 -> A054C0 resolves the authored group/action pair.
[[nodiscard]] inline bool encode(const Request& request, std::span<std::byte> output) noexcept {
    if (output.size() != kBytes || request.generation == 0 || request.generation > 0x7fffffff
        || request.revision == 0 || request.revision > 0x7fffffff || request.action == 0 || request.action == 0x811c9dc5) return false;
    std::array<std::byte, kBytes> bytes{};
    encoding::bits::Writer w(bytes);
    std::size_t written{};
    const bool ok = w.write(1, 1) && w.write(request.generation, 31)
        && w.write(1, 2) && w.write(1, 3) && w.write(1, 1)
        && w.write(0, 2) && w.write(1, 1) // .4/.5 absent; .6 present
        && w.write(request.revision, 31) && w.write(0, 6) && w.write(1, 6)
        && w.write(1, 1) && w.write(10, 4) && w.write(1, 2) // kind 9, native completion
        && w.write(request.group, 32) && w.write(request.action, 32)
        && w.write(0x811c9dc5, 32) // no additional identity
        && w.write(0x811c9dc5, 32) && w.write(0, 7) && w.write(32767, 16)
        && w.write(0, 3) && w.write(127, 8) // no target, mode/marker -1
        && w.write(0, 1) && w.bit_count() == kBits // .7 absent
        && w.finish(written) && written == kBytes;
    if (!ok) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) output[i] = bytes[i];
    return true;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits != kBits || input.size() != kBytes) return false;
    encoding::bits::Reader r(input);
    std::uint64_t v{}, generation{}, revision{};
    return r.read(1, v) && v == 1 && r.read(31, generation) && generation > 0
        && r.read(2, v) && v == 1 && r.read(3, v) && v == 1
        && r.read(1, v) && v == 1 && r.read(2, v) && v == 0
        && r.read(1, v) && v == 1 && r.read(31, revision) && revision > 0
        && r.read(6, v) && v == 0 && r.read(6, v) && v == 1
        && r.read(1, v) && v == 1 && r.read(4, v) && v == 10
        && r.read(2, v) && v == 1 && r.read(32, v)
        && r.read(32, v) && v != 0 && v != 0x811c9dc5
        && r.read(32, v) && v == 0x811c9dc5
        && r.read(32, v) && v == 0x811c9dc5
        && r.read(7, v) && v == 0 && r.read(16, v) && v == 32767
        && r.read(3, v) && v == 0 && r.read(8, v) && v == 127
        && r.read(1, v) && v == 0 && r.read(2, v) && v == 0;
}
}
