#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../Sunrise/src/middleware/bap/activity_message/cinematic_incident.h"
#include "../Sunrise/src/middleware/bap/activity_message/incident.h"
#include "../Sunrise/src/middleware/bap/activity_message/player_trigger_incident.h"
#include "../Sunrise/src/server/activity/mission/mission_script_cinematic.h"

namespace incident = sunrise::middleware::bap::activity_message::incident;
namespace cinematic = sunrise::middleware::bap::activity_message::cinematic_incident;
namespace trigger = sunrise::middleware::bap::activity_message::player_trigger_incident;
namespace bits = sunrise::middleware::encoding::bits;
namespace resolver = sunrise::server::activity::mission::cinematic;
namespace catalog = sunrise::state::build_data::scriptables;

static std::vector<std::byte> encode(const incident::Incident& input) {
    std::vector<std::byte> bytes(incident::kMaximumBodyBytes);
    bits::Writer writer(bytes);
    assert(incident::write(writer, input));
    std::size_t size = 0;
    assert(writer.finish(size));
    bytes.resize(size);
    return bytes;
}

static void round_trip_fields() {
    // Extra targets move the selector/payload across every possible byte alignment.
    for (unsigned extras = 0; extras < 8; ++extras) {
        for (bool selector : {false, true}) {
            for (bool optional : {false, true}) {
                incident::Incident input{};
                input.primaryTarget = cinematic::kTerminatedTarget;
                input.extraTargetCount = extras;
                for (unsigned i = 0; i < extras; ++i)
                    input.extraTargets[i] = 100 + i;
                input.hasCompressedSelector = selector;
                input.selectorLength = selector ? incident::kSelectorMaximum : 0;
                for (unsigned i = 0; i < input.selectorLength; ++i)
                    input.selector[i] = static_cast<std::byte>((i * 19 + 7) & 255);
                input.hasOptionalBlock = optional;
                input.optionalWordA = optional ? 0xFEDCBA98U : 0;
                input.optionalWordB = optional ? 0x76543210U : 0;
                input.payloadLength = incident::kPayloadMaximum;
                for (unsigned i = 0; i < input.payloadLength; ++i)
                    input.payload[i] = static_cast<std::byte>((i * 37 + 13) & 255);
                const auto encoded = encode(input);
                incident::Incident parsed{};
                assert(incident::validate(encoded, parsed) == incident::Verdict::accepted);
                assert(parsed.hasCompressedSelector == selector
                       && parsed.hasOptionalBlock == optional);
                assert(parsed.selector == input.selector && parsed.payload == input.payload);
                assert(parsed.optionalWordA == input.optionalWordA
                       && parsed.optionalWordB == input.optionalWordB);
                assert(parsed.hasPayload && parsed.payloadLength == input.payloadLength);
                assert(encode(parsed) == encoded);
                for (std::size_t size = 0; size < encoded.size(); ++size)
                    assert(incident::validate(std::span(encoded).first(size), parsed)
                           == incident::Verdict::truncated);
            }
        }
    }
    incident::Incident empty{};
    empty.primaryTarget = 100;
    empty.hasCompressedSelector = true; // Presence is distinct from a nonzero length.
    incident::Incident parsed{};
    assert(incident::validate(encode(empty), parsed) == incident::Verdict::accepted);
    assert(parsed.hasCompressedSelector && !parsed.hasPayload && parsed.payloadLength == 0);
}

static void cinematic_handoff() {
    constexpr std::uint32_t registry = 0x91EAC345U;
    constexpr std::uint64_t runtime = 0x123456789ABCDEF0ULL;
    std::array<std::byte, cinematic::kPayloadBytes> body{};
    bits::Writer writer(body);
    for (unsigned i = 0; i < 5; ++i)
        assert(writer.write(0xABABABABABABABABULL, 64));
    assert(writer.write(0x1234, 15)); // 335 bits of schema prefix.
    assert(writer.write(registry, 32));
    assert(writer.write(6 + 1, 7));
    assert(writer.write(32768, 16)); // Authored slot zero.
    assert(writer.write(runtime, 64));
    assert(writer.write(std::bit_cast<std::uint32_t>(1.0F), 32));
    std::size_t size = 0;
    assert(writer.finish(size) && size == body.size());
    catalog::Snapshot world{};
    catalog::Object object{};
    object.registryKey = registry;
    object.objectTag = 0x81234567U;
    object.stateRow = 3; // Alternate-state identity must survive the event path.
    world.objects.push_back(object);
    catalog::Slot slot{};
    slot.slotType = 6;
    world.slots.push_back(slot);
    for (const auto target : {cinematic::kStartedTarget, cinematic::kTerminatedTarget}) {
        incident::Incident input{};
        input.primaryTarget = target;
        input.payloadLength = body.size();
        std::copy(body.begin(), body.end(), input.payload.begin());
        incident::Incident parsed{};
        assert(incident::validate(encode(input), parsed) == incident::Verdict::accepted);
        cinematic::Payload movie{};
        assert(cinematic::decode(std::span(parsed.payload).first(parsed.payloadLength), movie));
        assert(movie.registryKey == registry && movie.slotType == 6 && movie.slotIndex == 0);
        assert(movie.runtimeObjectId == runtime && movie.eventValue == 1.0F);
        resolver::Source source{};
        assert(resolver::resolve(world, movie, source) == resolver::ResolveStatus::ready);
        assert(source.registryKey == registry && source.objectTag == object.objectTag);
        movie.registryKey ^= 1;
        assert(resolver::resolve(world, movie, source) == resolver::ResolveStatus::absent);
    }
}

static void player_trigger_payload() {
    incident::Incident input{};
    input.primaryTarget = trigger::kPrimaryTarget;
    input.payloadLength = trigger::kPayloadBytes;
    bits::Writer writer(std::span(input.payload).first(input.payloadLength));
    for (unsigned i = 0; i < 5; ++i)
        assert(writer.write(0, 64));
    assert(writer.write(0, 15));
    assert(writer.write(0x87654321, 32));
    assert(writer.write(32, 7)); // Type 31 plus bias.
    assert(writer.write(32768 + 70, 16));
    assert(writer.write(0x10203040, 32));
    std::size_t size = 0;
    assert(writer.finish(size) && size == input.payloadLength);
    incident::Incident parsed{};
    assert(incident::validate(encode(input), parsed) == incident::Verdict::accepted);
    trigger::Payload decoded{};
    assert(trigger::decode(std::span(parsed.payload).first(parsed.payloadLength), decoded));
    assert(decoded.registryKey == 0x87654321 && decoded.slotType == 31 && decoded.slotIndex == 70);
    assert(decoded.resolvedObjectId == 0x10203040);
}

int main() {
    round_trip_fields();
    cinematic_handoff();
    player_trigger_payload();
    std::cout << "Incident retention, truncation, cinematic resolution and player-trigger decoding "
                 "passed\n";
}
