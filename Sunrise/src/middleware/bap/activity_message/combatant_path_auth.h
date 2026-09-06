#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::combatant_path {
inline constexpr std::uint32_t kSchema = 0x80807DA1;
inline constexpr std::size_t kBits = 156, kBytes = 20;
struct Request {
    std::uint32_t generation{}, revision{}, registryKey{};
    std::uint16_t pathIndex{};
};
// AB6340 creates the authored member. Auth .6 runs one kind-3 movement command:
// A97BB0 -> 4FFEC0 resolves a type-58 authored path; native AI follows its curve.
// Auth .7 is a squad-reference delivery request, NOT a movement path list.
// Preserve spawn generation while advancing the action-program revision. The authored
// entry/exit curves have two markers: marker 0 is the START, marker 1 the destination.
// AB0C30 resolves the marker index, not an action index or a duration in seconds.
[[nodiscard]] inline bool encode(const Request& request, std::span<std::byte> output) noexcept {
    if (output.size() != kBytes || request.generation == 0 || request.generation > 0x7fffffff
        || request.revision == 0 || request.revision > 0x7fffffff || request.registryKey == 0
        || request.pathIndex > 32767) return false;
    std::array<std::byte, kBytes> bytes{};
    encoding::bits::Writer w(bytes);
    std::size_t written{};
    const bool ok = w.write(1, 1) && w.write(request.generation, 31)
        && w.write(1, 2) && w.write(1, 3) && w.write(1, 1)
        && w.write(0, 2) && w.write(1, 1) // .4/.5 absent; .6 present
        && w.write(request.revision, 31) && w.write(0, 6) && w.write(1, 6)
        && w.write(1, 1) && w.write(4, 4) && w.write(1, 2) // kind 3, completion 0
        && w.write(request.registryKey, 32) && w.write(59, 7)
        && w.write(request.pathIndex + 32768U, 16)
        && w.write(1, 8) && w.write(1, 1) // destination marker; follow curve
        && w.write(0, 1) && w.bit_count() == kBits // .7 absent
        && w.finish(written) && written == kBytes;
    if (!ok) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) output[i] = bytes[i];
    return true;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits != kBits || input.size() != kBytes) return false;
    encoding::bits::Reader r(input);
    std::uint64_t v{}, generation{}, registry{}, index{}, revision{};
    return r.read(1, v) && v == 1 && r.read(31, generation) && generation > 0
        && r.read(2, v) && v == 1 && r.read(3, v) && v == 1
        && r.read(1, v) && v == 1 && r.read(2, v) && v == 0
        && r.read(1, v) && v == 1 && r.read(31, revision) && revision > 0
        && r.read(6, v) && v == 0 && r.read(6, v) && v == 1
        && r.read(1, v) && v == 1 && r.read(4, v) && v == 4
        && r.read(2, v) && v == 1 && r.read(32, registry) && registry != 0
        && r.read(7, v) && v == 59 && r.read(16, index) && index >= 32768
        && r.read(8, v) && v == 1 && r.read(1, v) && v == 1
        && r.read(1, v) && v == 0 && r.read(4, v) && v == 0;
}
}
