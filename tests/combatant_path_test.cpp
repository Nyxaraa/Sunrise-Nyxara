#include "../Sunrise/src/middleware/bap/activity_message/squad_objective_auth.h"
#include <cassert>
#include <array>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/combatant_path_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_delivery_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_retire_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/squad_auth_body.h"
#include "../Sunrise/src/server/activity/mission/mission_script_actor_path_sense.h"
namespace sense = sunrise::middleware::bap::activity_message::sense_update;
namespace {
sense::TargetStatus group(const void*, std::uint32_t key, sense::GroupTarget& out) noexcept {
    if (key != 0xF6FFB59E) return sense::TargetStatus::targetUnavailable;
    out.objectTag = 0x80B3CB5E;
    return sense::TargetStatus::resolved;
}
sense::TargetStatus slot(const void*, const sense::GroupTarget&, std::uint8_t type,
                         std::uint16_t index, sense::SlotTarget& out) noexcept {
    if (type != 2 || index != 22) return sense::TargetStatus::targetUnavailable;
    out.senseSchema = 0x80807DA2;
    return sense::TargetStatus::resolved;
}
void packet_regression() {
    namespace delivery = sunrise::middleware::bap::activity_message::combatant_delivery;
    std::array<delivery::SquadReference,2> manifest{{{0xf6ffb59e,23},{0xf6ffb59e,25}}};
    std::array<std::byte,delivery::kMaxBytes> manifestBody{};
    std::size_t manifestBytes{}, manifestBits{};
    assert(delivery::encode_many(1,1,manifest,manifestBody,manifestBytes,manifestBits));
    assert(manifestBits==187 && manifestBytes==24);
    assert(delivery::validate(std::span(manifestBody).first(manifestBytes),manifestBits));
    manifest[1]=manifest[0];
    assert(!delivery::encode_many(1,1,manifest,manifestBody,manifestBytes,manifestBits));
    assert(!delivery::encode_many(1,1,{},manifestBody,manifestBytes,manifestBits));
    using namespace sunrise::server::activity::mission;
    // Independent complete msg-6 packets: initial movement cursor, delivery start,
    // delivery stop with absent movement fields, then real movement completion.
    constexpr std::array<std::string_view, 4> fixtures{
        "000000000000000000000000000000003edff6b3c0000019df6ffb59e07002d800000013020000000440000000210000000300",
        "000000000000000000000000000000003edff6b3c0000010df6ffb59e07002d040000000c10000000300",
        "000000000000000000000000000000003edff6b3c000000cff6ffb59e07002d0108000000180",
        "000000000000000000000000000000003edff6b3c00000121f6ffb59e07002d183000000020840000000c0",
    };
    const sense::Resolver resolver{nullptr, &group, &slot};
    ActorPathLevel level{};
    for (std::size_t i=0;i<fixtures.size();++i) {
        std::array<std::byte, 51> bytes{};
        auto nibble = [](char c) { return c <= '9' ? c-'0' : c-'a'+10; };
        const auto size = fixtures[i].size()/2;
        for (std::size_t j=0;j<size;++j)
            bytes[j] = std::byte((nibble(fixtures[i][j*2])<<4) | nibble(fixtures[i][j*2+1]));
        sense::SenseUpdate decoded{}; std::size_t consumed{};
        assert(sense::decode_sense_update(std::span(bytes).first(size), resolver, decoded, consumed));
        assert(decoded.decoded.status == sense::DecodeStatus::complete && consumed == size*8);
        assert(decoded.decoded.objectCount == 1);
        const auto& object = decoded.decoded.objects[0];
        assert(object.status == sense::ObjectStatus::decoded && object.generationPlusOne == 3);
        const auto fields = std::span(decoded.decoded.values).subspan(object.firstValue, object.valueCount);
        assert(update_actor_path_level(level, fields, object.schemaRow));
        assert(level.generation == 1 && level.revision == 1 && !level.dead);
        assert(level.state == (i == 3 ? 1 : 0)); // Delivery cannot fake a completed flight.
        assert(level.deliveryRevision == (i == 0 ? 0 : 1));
        assert(level.deliveryState == (i == 1 ? 1 : 0));
        assert(!update_actor_path_level(level, fields, object.schemaRow));
        assert(!sense::decode_sense_update(std::span(bytes).first(size-1), resolver, decoded, consumed));
    }
}
void reservation_regression() {
    namespace squad = sunrise::middleware::bap::activity_message::squad_auth;
    const std::array<std::int32_t, 2> counts{3,1};
    squad::Preset request{counts,1,squad::Mode::reserve};
    std::array<std::byte, squad::kMaximumByteCount> bytes{};
    std::size_t written{}, bits{};
    assert(squad::encode(request,{},bytes,written,bits));
    assert(bits == squad::exact_body_bit_count(2));
    sunrise::middleware::encoding::bits::Reader reader{std::span(bytes).first(written)};
    std::uint64_t mode{};
    assert(reader.skip(bits-36) && reader.read(3,mode) && mode==4); // biased mode 3
    const auto original = bytes;
    request.mode = static_cast<squad::Mode>(4);
    assert(!squad::encode(request,{},bytes,written,bits) && bytes == original);
}
}
int main() {
    packet_regression();
    reservation_regression();
    namespace objective = sunrise::middleware::bap::activity_message::squad_objective;
    std::array<std::byte, objective::kBytes> objectiveBytes{};
    assert(objective::encode({0xd8ce8390, 1, 0, -1}, objectiveBytes));
    constexpr std::array<unsigned,20> objectiveFixture{0xec,0x67,0x41,0xc8,0x4,0x80,0x0,0x0,0x8,0x0,0x0,0x0,0x14,0x8,0x13,0xc0,0x8e,0x4e,0xe2,0x80};
    for (unsigned i=0;i<20;++i) assert(std::to_integer<unsigned>(objectiveBytes[i]) == objectiveFixture[i]);
    assert(objective::validate(objectiveBytes,153));
    assert(objective::encode({0xd8ce8390,1,0,3,true},objectiveBytes));
    assert(objective::validate(objectiveBytes,153));
    sunrise::middleware::encoding::bits::Reader objectiveReader(objectiveBytes);
    std::uint64_t mode{};
    assert(objectiveReader.skip(objective::kBits-36) && objectiveReader.read(3,mode) && mode==4);
    assert(objective::encode({0xd8ce8390,1,0,23},objectiveBytes));
    assert(objective::validate(objectiveBytes,153));
    assert(!objective::encode({0xd8ce8390,1,0,24},objectiveBytes));
    assert(!objective::encode({0xd8ce8390,1,0,-2},objectiveBytes));
    assert(!objective::encode({0xd8ce8390,0,0,-1},objectiveBytes));
    objectiveBytes.back() |= std::byte{1}; assert(!objective::validate(objectiveBytes,153));

    namespace path = sunrise::middleware::bap::activity_message::combatant_path;
    std::array<std::byte, path::kBytes> bytes{};
    assert(path::encode({1, 1, 0xf6ffb59e, 115}, bytes));
    constexpr std::array<unsigned, 20> expected{0x80,0x0,0x0,0x1,0x4c,0x80,0x0,0x0,0x1,0x0,0x1a,0x3e,0xdf,0xf6,0xb3,0xce,0xe0,0x1c,0xc0,0x60};
    for (unsigned i = 0; i < bytes.size(); ++i) assert(std::to_integer<unsigned>(bytes[i]) == expected[i]);
    assert(path::validate(bytes, 156));
    auto atStart = bytes;
    atStart.back() &= ~std::byte{0x40};
    assert(!path::validate(atStart, 156)); // marker 0 parks at the entrance instead of unloading
    bytes.back() |= std::byte{1}; assert(!path::validate(bytes, 156));
    assert(!path::encode({0, 1, 1, 115}, bytes));
    assert(!path::encode({1, 0, 1, 115}, bytes));
    assert(!path::encode({1, 1, 1, 32768}, bytes));
    assert(!path::encode({1, 1, 1, 115}, std::span(bytes).first(19)));
    namespace retire = sunrise::middleware::bap::activity_message::combatant_retire;
    std::array<std::byte, retire::kBytes> disabled{};
    assert(retire::encode(2,disabled) && retire::validate(disabled,77));
    const std::array<std::byte,10> retiredFixture{std::byte{0x80},std::byte{0},std::byte{0},std::byte{2},std::byte{0x48},std::byte{0x40},std::byte{0},std::byte{0},std::byte{0},std::byte{0x10}};
    assert(disabled == retiredFixture);
    disabled[4] |= std::byte{4}; // Enabled actors cannot pass as retired.
    assert(!retire::validate(disabled,77));
    assert(!retire::encode(0,disabled));
    assert(!retire::encode(0x80000000,disabled));
    namespace delivery = sunrise::middleware::bap::activity_message::combatant_delivery;
    std::array<std::byte, delivery::kBytes> cargo{};
    assert(delivery::encode({1,1,0xf6ffb59e,23},cargo));
    constexpr std::array<unsigned,17> deliveryFixture{
        0x80,0,0,1,0x4c,0x47,0xdb,0xfe,0xd6,0x78,0x14,0x00,0xb8,0,0,0,0x10};
    for (unsigned i=0;i<cargo.size();++i) assert(std::to_integer<unsigned>(cargo[i]) == deliveryFixture[i]);
    assert(delivery::validate(cargo,132));
    assert(!path::validate(cargo,132) && !delivery::validate(bytes,156));
    cargo[10] |= std::byte{0x08}; // A non-squad ClientRef must be rejected.
    assert(!delivery::validate(cargo,132));
    assert(!delivery::encode({1,1,1,32768},cargo));
    assert(!delivery::encode({1,0,1,23},cargo));
    assert(!delivery::encode({0,1,1,23},cargo));
    using namespace sunrise::server::activity::mission;
    using sunrise::middleware::bap::activity_message::sense_update::DecodedValue;
    std::array<DecodedValue, 4> fields{};
    const unsigned ordinals[]{0, 1, 0, 10};
    for (unsigned i = 0; i < 4; ++i) { fields[i].schemaRow = 10; fields[i].fieldOrdinal = ordinals[i]; }
    fields[1].schemaRow = fields[2].schemaRow = 0x80807F6E;
    fields[0].signedValue = fields[1].signedValue = 1;
    ActorPathLevel level{};
    assert(!update_actor_path_level(level, std::span(fields).first(2), 10));
    assert(update_actor_path_level(level, std::span(fields).subspan(2), 10));
    assert(level.generation == 1 && level.revision == 1 && level.state == 0 && !level.dead);
    assert(!update_actor_path_level(level, fields, 10));
    fields[0].present = fields[1].present = false; fields[2].signedValue = 1;
    assert(update_actor_path_level(level, fields, 10) && level.revision == 1 && level.state == 1);
    fields[3].unsignedValue = 1;
    assert(update_actor_path_level(level, fields, 10) && level.dead);
}
