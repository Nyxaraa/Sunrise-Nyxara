#include "reward_persistence.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "../../../../core/filesystem/path.h"
#include "../../../../core/logging/log.h"
#include "reward_catalog.h"

namespace sunrise::state::build_data::records::rewards {
namespace {

/** Generated input shipped beside settings.json, outside the extraction cache. */
constexpr std::wstring_view kRewardFileSuffix = L"\\record_rewards.bin";
constexpr std::array<char, 8> kMagic{'S', 'N', 'R', 'S', 'R', 'W', 'D', '1'};

core::path::Buffer g_path{};
bool g_pathReady{};

void report_failure(const char* result, std::size_t rows) noexcept {
    if (!core::log::accepts(core::log::Channel::state, core::log::Level::warn)) {
        return;
    }
    std::array<char, 96> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=recrewards stage=load result=%s rows=%zu", result, rows);
    if (written > 0) {
        core::log::write(
            core::log::Channel::state,
            core::log::Level::warn,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

/** Versioned file header guarding row-layout changes. */
struct Header {
    std::array<char, 8> magic{};
    std::uint32_t rows{};
    std::uint32_t rowWidth{};
};

} // namespace

bool initialize(void* module) noexcept {
    (void)replace(std::span<const RewardRow>{});
    g_path = {};
    g_pathReady = false;
    if (!core::path::artifact_directory(module, g_path)
        || !core::path::append(g_path, kRewardFileSuffix)) {
        report_failure("path_fail", 0);
        return false;
    }
    g_pathReady = true;
    return true;
}

/** Loads the shipped table; a missing or empty file disables generated rewards. */
bool load_and_publish() noexcept {
    if (!g_pathReady) {
        return false;
    }
    const HANDLE file = CreateFileW(
        g_path.chars.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return replace(std::span<const RewardRow>{});
    }

    Header header{};
    DWORD read = 0;
    const BOOL headerCall = ReadFile(file, &header, sizeof header, &read, nullptr);
    if (headerCall != FALSE && read == 0) {
        CloseHandle(file);
        return replace(std::span<const RewardRow>{});
    }
    const bool headerRead = headerCall != FALSE && read == sizeof header;
    if (!headerRead || std::memcmp(header.magic.data(), kMagic.data(), kMagic.size()) != 0
        || header.rowWidth != sizeof(RewardRow) || header.rows > kRewardCapacity) {
        CloseHandle(file);
        report_failure(headerRead ? "rejected" : "header_fail", header.rows);
        return false;
    }
    if (header.rows == 0) {
        CloseHandle(file);
        return replace(std::span<const RewardRow>{});
    }

    std::vector<RewardRow> rows(header.rows);
    const auto expected = static_cast<DWORD>(rows.size() * sizeof(RewardRow));
    const bool rowsRead =
        ReadFile(file, rows.data(), expected, &read, nullptr) != FALSE && read == expected;
    CloseHandle(file);
    if (!rowsRead) {
        report_failure("read_fail", rows.size());
        return false;
    }
    if (!valid(std::span<const RewardRow>{rows}) || !replace(std::span<const RewardRow>{rows})) {
        report_failure("publish_fail", rows.size());
        return false;
    }
    return true;
}

} // namespace sunrise::state::build_data::records::rewards
