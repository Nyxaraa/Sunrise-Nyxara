#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
constexpr bool ember_burn_source(std::uint32_t tag,std::uint32_t schema,
    std::uint64_t offset,std::uint32_t asset) noexcept {
    return tag==0x80B3C0C6U && schema==0x80809540U && offset==0xAC8U && asset==0x80C1D9E0U;
}
}
