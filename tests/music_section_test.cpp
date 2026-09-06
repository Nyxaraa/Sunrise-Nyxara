#include <array>
#include <cassert>
#include "../Sunrise/src/middleware/bap/activity_message/music_section_auth.h"
using namespace sunrise::middleware::bap::activity_message;
int main() {
    std::array<std::byte, music_section::kBytes> body{};
    std::size_t written{};
    for (const auto section : {0, 5, 28, 31, 32, 127}) {
        assert(music_section::encode(section, true, body, written));
        assert(written == body.size() && music_section::validate(body, music_section::kBits));
        sunrise::middleware::encoding::bits::Reader r(body);
        for (int lane = 0; lane < 4; ++lane) {
            std::uint64_t v{};
            assert(r.read(32, v));
            assert(v == (section / 32 == lane ? std::uint32_t{1} << (section % 32) : 0));
        }
        body.back() |= std::byte{0x01};
        assert(!music_section::validate(body, music_section::kBits));
    }
    assert(music_section::encode(0, false, body, written));
    assert(music_section::validate(body, music_section::kBits));
    assert(!music_section::encode(128, true, body, written));
    assert(music_section::encode(0, true, body, written));
    body[3] |= std::byte{2};
    assert(!music_section::validate(body, music_section::kBits));
}
