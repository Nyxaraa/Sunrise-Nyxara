#include <array>
#include <cassert>
#include <limits>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/ghost_link_auth.h"
#include "../Sunrise/src/server/activity/mission/mission_script_ghost_sense.h"
namespace sense = sunrise::middleware::bap::activity_message::sense_update;
namespace {
sense::TargetStatus group(const void*, std::uint32_t key, sense::GroupTarget& out) noexcept {
    if (key != 0xF6FFB59E) return sense::TargetStatus::targetUnavailable;
    out.objectTag = 0x80B3CB5E;
    return sense::TargetStatus::resolved;
}
sense::TargetStatus slot(const void*, const sense::GroupTarget&, std::uint8_t type,
                         std::uint16_t index, sense::SlotTarget& out) noexcept {
    if (type != 65 || index != 60) return sense::TargetStatus::targetUnavailable;
    out.senseSchema = 0x80804D3E;
    return sense::TargetStatus::resolved;
}
void packet_regression() {
    using sunrise::server::activity::mission::GhostLevel;
    using sunrise::server::activity::mission::update_ghost_level;
    // Independent complete msg-6 fixtures: group length 155, type 65 / slot 60,
    // required active + raw float + biased generation, then publication generation.
    constexpr std::array<std::string_view, 3> fixtures{
        "000000000000000000000000000000003edff6b3c00000137f6ffb59e8500790000000040000000800000018",
        "000000000000000000000000000000003edff6b3c00000137f6ffb59e8500799f80000040000001000000018",
        "000000000000000000000000000000003edff6b3c00000137f6ffb59e8500791fc0000040000001000000018",
    };
    GhostLevel level{};
    const sense::Resolver resolver{nullptr, &group, &slot};
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        std::array<std::byte, 44> packet{};
        auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (std::size_t j = 0; j < packet.size(); ++j)
            packet[j] = std::byte((nibble(fixtures[i][j * 2]) << 4) | nibble(fixtures[i][j * 2 + 1]));
        sense::SenseUpdate decoded{};
        std::size_t consumed{};
        assert(sense::decode_sense_update(packet, resolver, decoded, consumed));
        assert(decoded.decoded.status == sense::DecodeStatus::complete);
        assert(consumed == 352 && decoded.decoded.objectCount == 1);
        const auto& object = decoded.decoded.objects[0];
        assert(object.status == sense::ObjectStatus::decoded && object.valueCount == 3);
        assert(object.generationPlusOne == 3 && object.deltaBits == 66);
        assert(update_ghost_level(level, std::span(decoded.decoded.values).first(3), object.schemaRow));
        assert(level.generation == (i == 0 ? 1 : 2));
        assert(level.active == (i == 1));
        assert(level.progress == (i == 0 ? 0.0f : i == 1 ? 0.5f : 1.0f));
        // Losing the packet tail cannot turn a partial scan into completion.
        assert(!sense::decode_sense_update(std::span(packet).first(43), resolver, decoded, consumed));
    }
}
}
int main() {
    packet_regression();
    namespace ghost = sunrise::middleware::bap::activity_message::ghost_link;
    using sunrise::middleware::bap::activity_message::sense_update::DecodedValue;
    using sunrise::server::activity::mission::GhostLevel;
    using sunrise::server::activity::mission::update_ghost_level;
    std::array<std::byte, 9> bytes{};
    std::size_t written = 0;
    assert(ghost::encode(2, true, bytes, written) && written == 9);
    // Independent MSB-first wire fixture: biased generation, enabled, authored hash.
    constexpr std::array<unsigned, 9> expected{0x80,0,0,2,0xC0,0x8E,0x4E,0xE2,0x80};
    for (std::size_t i = 0; i < bytes.size(); ++i) assert(std::to_integer<unsigned>(bytes[i]) == expected[i]);
    assert(ghost::validate(bytes, 65));
    assert(!ghost::validate(bytes, 64));
    bytes.back() |= std::byte{1};
    assert(!ghost::validate(bytes, 65));
    assert(!ghost::encode(0, true, bytes, written));
    assert(!ghost::encode(-1, true, bytes, written));
    assert(!ghost::encode(1, true, std::span(bytes).first(8), written));
    assert(ghost::encode(1, false, bytes, written) && (std::to_integer<unsigned>(bytes[4]) & 0x80) == 0);
    GhostLevel level{};
    std::array<DecodedValue, 3> fields{};
    for (unsigned i = 0; i < 3; ++i) { fields[i].schemaRow = 7; fields[i].fieldOrdinal = i; fields[i].present = true; }
    fields[0].unsignedValue = 1;
    fields[1].realValue = 0.5f;
    fields[2].signedValue = 2;
    assert(!update_ghost_level(level, std::span(fields).first(1), 7));
    assert(!update_ghost_level(level, std::span(fields).subspan(1, 1), 7));
    assert(update_ghost_level(level, std::span(fields).subspan(2), 7));
    assert(level.active && level.progress == .5f && level.generation == 2);
    assert(!update_ghost_level(level, fields, 7));
    fields[1].realValue = 1;
    fields[0].present = fields[2].present = false;
    assert(update_ghost_level(level, fields, 7) && level.active && level.generation == 2);
    fields[0].present = true; fields[0].unsignedValue = 0; fields[1].present = false;
    assert(update_ghost_level(level, fields, 7) && !level.active && level.progress == 1);
    fields[1].present = true; fields[1].realValue = std::numeric_limits<float>::quiet_NaN();
    assert(!update_ghost_level(level, fields, 7) && level.progress == 1);
    assert(!update_ghost_level(level, fields, 8));
}
