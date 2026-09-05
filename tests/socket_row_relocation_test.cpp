#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../Sunrise/src/client/hooks/network/investment/socket_row_relocation.h"

namespace r = sunrise::client::hooks::network::investment::relocation;
int main(int argc, char** argv) {
    assert(argc == 2);
    std::array<r::Row, 71> original{};
    std::ifstream input(argv[1], std::ios::binary);
    std::string hex, line;
    while (input >> line)
        hex += line;
    assert(hex.size() == sizeof(original) * 2);
    auto* bytes = reinterpret_cast<unsigned char*>(original.data());
    for (std::size_t i = 0; i < sizeof(original); ++i) {
        bytes[i] = static_cast<unsigned char>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    }
    auto target = [](const r::Row& row) {
        auto id = r::get<std::uint32_t>(row.bytes.data());
        return id == 492 || id == 494 || id == 495 || id == 496;
    };
    std::vector<r::Row> general, legs, moved;
    for (std::size_t i = 0; i < original.size(); ++i) {
        assert(r::valid(original[i]));
        if (i < 19)
            (target(original[i]) ? moved : general).push_back(original[i]);
        else
            legs.push_back(original[i]);
    }
    legs.insert(legs.begin() + 1, moved.begin(), moved.end());
    assert(general.size() == 15 && legs.size() == 56 && moved.size() == 4);
    assert(legs[0].bytes == original[19].bytes);
    for (std::size_t i = 1; i < 5; ++i)
        assert(target(legs[i]));
    for (std::size_t i = 5; i < legs.size(); ++i) {
        assert(legs[i].bytes == original[19 + i - 4].bytes);
    }
    unsigned conditions = 0;
    for (auto& rows : {general, legs}) {
        std::vector<std::byte> blob(r::capacity(rows.size()));
        assert(r::build(rows, blob) && r::verify(rows, blob));
        auto relocated = blob;
        assert(r::verify(rows, relocated)); // actual allocation address changes
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (r::get<std::uint64_t>(rows[i].bytes.data() + 8) == 0) continue;
            ++conditions;
            auto shallow = blob;
            std::memcpy(shallow.data() + 24 + i * 32 + 16, rows[i].bytes.data() + 16, 8);
            assert(!r::verify(rows, shallow));
            auto corrupt = blob;
            auto at = 24 + i * 32;
            auto header = at + 16 + r::get<std::int64_t>(corrupt.data() + at + 16);
            corrupt[header + 20] ^= std::byte{1};
            assert(!r::verify(rows, corrupt));
        }
        assert(!r::build(rows, std::span(blob.data(), 32)));
        auto invalid = rows;
        r::put(invalid[0].bytes.data() + 8, std::uint64_t{2});
        assert(!r::build(invalid, blob));
        invalid = rows;
        invalid[1] = invalid[0];
        assert(!r::build(invalid, blob));
        assert(!r::verify(rows, std::span<const std::byte>(blob.data() + 1, blob.size() - 1)));
    }
    assert(conditions == 57);
    // Regression: substituting Enhanced's condition must fail comparison to target originals.
    auto wrong = legs;
    for (std::size_t i = 1; i < 5; ++i)
        wrong[i].condition = original[20].condition;
    std::vector<std::byte> blob(r::capacity(56));
    assert(r::build(wrong, blob));
    assert(!r::verify(legs, blob));
    std::cout << "PASS: 71 rows, 57 conditions; relocation, shallow-copy rejection, own-condition "
                 "identity, malformed shape, duplicate, capacity and alignment checks\n";
}
