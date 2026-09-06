#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::scene_events {
inline constexpr std::uint32_t kSchema=0x8080626B;
inline constexpr std::size_t kMaximumEvents=32, kMaximumBytes=138;
// Full type-43 Auth: generation, clear, dependency count, scalar, event count, keys.
// Keep generation unchanged when appending events: changing it restarts the scene.
inline bool encode(std::int32_t generation, std::span<const std::uint32_t> events,
                   std::span<std::byte> output, std::size_t& bytes, std::size_t& bits) noexcept {
    bytes=bits=0;
    if (generation<=0 || events.size()>kMaximumEvents || output.size()<(74+32*events.size()+7)/8)
        return false;
    for (std::size_t i=0;i<events.size();++i) {
        if (!events[i] || events[i]==0xFFFFFFFFU) return false;
        for (std::size_t j=0;j<i;++j) if (events[i]==events[j]) return false;
    }
    encoding::bits::Writer w(output);
    if (!w.write(static_cast<std::uint32_t>(generation)+0x80000000U,32)
        || !w.write(0,1) || !w.write(0,4) || !w.write(0,31) || !w.write(events.size(),6)) return false;
    for (const auto event : events) if (!w.write(event,32)) return false;
    bits=w.bit_count(); return w.finish(bytes);
}

// Admit only the event-only subset above; dependencies, clear commands and scalar
// inputs require their own checked API. Reject malformed lengths and nonzero padding.
inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits<74 || bits>74+32*kMaximumEvents || input.size()!=(bits+7)/8) return false;
    encoding::bits::Reader r(input);
    std::uint64_t generation{}, value{}, count{};
    if (!r.read(32,generation) || generation<=0x80000000U
        || !r.read(1,value) || value!=0 || !r.read(4,value) || value!=0
        || !r.read(31,value) || value!=0 || !r.read(6,count)
        || count>kMaximumEvents || bits!=74+32*count) return false;
    std::array<std::uint32_t,kMaximumEvents> events{};
    for (std::size_t i=0;i<count;++i) {
        if (!r.read(32,value)) return false;
        events[i]=static_cast<std::uint32_t>(value);
    }
    std::array<std::byte,kMaximumBytes> canonical{};
    std::size_t written{}, expectedBits{};
    if (!encode(static_cast<std::int32_t>(generation-0x80000000U),
                std::span(events).first(count),canonical,written,expectedBits)
        || written!=input.size() || expectedBits!=bits) return false;
    for (std::size_t i=0;i<written;++i) if (canonical[i]!=input[i]) return false;
    return true;
}
}
