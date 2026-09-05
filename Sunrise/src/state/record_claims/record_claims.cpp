#include "record_claims.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "../build_data/nodes/definition.h"
#include "../build_data/nodes/node_catalog.h"
#include "../build_data/runtime.h"
#include "../unlocks/definition.h"
#include "objective_slot_table.h"
#include "parent_bar_table.h"

namespace sunrise::state::record_claims {
namespace {

/** One bit per account completion flag. */
constexpr std::size_t kIndexCapacity = unlocks::kAccountFlagCapacity;
constexpr std::size_t kWordBits = 64;
constexpr std::size_t kWordCount = (kIndexCapacity + kWordBits - 1) / kWordBits;

constexpr std::wstring_view kClaimFileSuffix = L"\\cache\\record_claims.bin";
constexpr std::array<char, 8> kMagic{'S', 'N', 'R', 'S', 'C', 'L', 'M', '1'};
constexpr std::size_t kEntrySize = 2 * sizeof(std::uint16_t);
constexpr std::uint32_t kMaximumEntries = static_cast<std::uint32_t>(kIndexCapacity);
constexpr std::wstring_view kWriteStageSuffix = L".tmp";

/**
 * Interval records have no completion flag. Their second reserved objective-value slot carries
 * intervalsRedeemedCount, while each successful redemption contributes that interval's score.
 * These are the versioned interval records in the installed Shadowkeep-through-Arrivals data.
 */
struct IntervalDefinition {
    std::uint16_t recordIndex;
    std::uint16_t redeemedCountSlot;
    std::uint32_t definitionHash;
    std::array<std::uint16_t, 7> scores;
    std::uint8_t intervalCount;
};

constexpr std::array<IntervalDefinition, 16> kIntervalDefinitions{{
    {1933U, 5260U, 1686327621U, {10U, 15U, 25U}, 3U},
    {1934U, 5262U, 684525211U, {10U, 15U, 25U, 50U}, 4U},
    {1958U, 5303U, 2972583416U, {10U, 15U, 25U}, 3U},
    {1959U, 5305U, 3791036271U, {10U, 15U, 25U, 50U}, 4U},
    {1960U, 5307U, 2703117203U, {5U, 5U, 10U, 30U}, 4U},
    {1961U, 5309U, 1339925682U, {5U, 5U, 10U, 30U}, 4U},
    {1962U, 5311U, 3115239680U, {5U, 5U, 10U, 30U}, 4U},
    {1963U, 5313U, 1286790825U, {5U, 5U, 10U, 30U}, 4U},
    {1976U, 5329U, 1839524615U, {5U, 5U, 10U, 30U}, 4U},
    {1980U, 5334U, 3558002660U, {5U, 10U, 15U, 25U}, 4U},
    {1981U, 5336U, 3393252660U, {5U, 10U, 15U, 50U}, 4U},
    {2029U, 5394U, 230421321U, {10U, 15U, 25U}, 3U},
    {2030U, 5396U, 2122679994U, {10U, 15U, 25U, 50U}, 4U},
    {2108U, 5486U, 2801882959U, {10U, 15U, 25U}, 3U},
    {2109U, 5488U, 3196543652U, {10U, 15U, 25U, 50U}, 4U},
    {2112U, 5492U, 1012728572U, {10U, 15U, 25U, 25U, 25U, 25U, 50U}, 7U},
}};

constexpr std::wstring_view kIntervalFileSuffix = L"\\cache\\record_intervals.bin";
constexpr std::array<char, 8> kIntervalMagic{'S', 'N', 'R', 'S', 'I', 'N', 'T', '1'};
constexpr std::size_t kIntervalEntrySize = 4;

/** Books where the shared counter itself grants chapters. */
constexpr std::array<std::uint16_t, 4> kCounterGrantedLoreNodes{
    823U, // Stolen Intelligence
    839U, // Unveiling
    850U, // A Man with No Name
    853U, // Revelation
};

[[nodiscard]] constexpr bool counter_granted_lore(std::uint16_t nodeIndex) noexcept {
    return std::find(kCounterGrantedLoreNodes.begin(), kCounterGrantedLoreNodes.end(), nodeIndex)
           != kCounterGrantedLoreNodes.end();
}

/** Separate files preserve the original claim format. */
constexpr std::wstring_view kClaimableFileSuffix = L"\\cache\\record_claimable.bin";
constexpr std::array<char, 8> kClaimableMagic{'S', 'N', 'R', 'S', 'C', 'M', 'P', '1'};
constexpr std::wstring_view kProgressFileSuffix = L"\\cache\\record_progress.bin";
constexpr std::array<char, 8> kProgressMagic{'S', 'N', 'R', 'S', 'P', 'R', 'G', '1'};
constexpr std::size_t kProgressEntrySize = sizeof(std::uint16_t) + sizeof(std::int32_t);

std::mutex g_lock;
std::array<std::uint64_t, kWordCount> g_claimed{};
std::array<std::uint64_t, kWordCount> g_claimable{};
std::array<std::uint64_t, kWordCount> g_counterGrantedChapters{};
std::array<std::int32_t, kIndexCapacity> g_progress{};
std::array<std::uint16_t, kIndexCapacity> g_scoreByIndex{};
std::size_t g_count{};
std::uint32_t g_score{};
std::array<std::uint8_t, kIntervalDefinitions.size()> g_intervalClaims{};
std::uint32_t g_intervalScore{};
core::path::Buffer g_path{};
bool g_pathReady{};
core::path::Buffer g_claimablePath{};
bool g_claimablePathReady{};
core::path::Buffer g_progressPath{};
bool g_progressPathReady{};
core::path::Buffer g_intervalPath{};
bool g_intervalPathReady{};
bool g_persistenceRequired{};
bool g_counterGrantedChaptersReady{};

void clear_counter_granted_chapters_locked() noexcept {
    g_counterGrantedChapters.fill(0);
    g_counterGrantedChaptersReady = false;
}

void clear_locked() noexcept {
    g_claimed.fill(0);
    g_claimable.fill(0);
    clear_counter_granted_chapters_locked();
    g_progress.fill(0);
    g_scoreByIndex.fill(0);
    g_count = 0;
    g_score = 0;
    g_intervalClaims.fill(0);
    g_intervalScore = 0;
}

/** Objective storage reserved by records whose installed definition exposes no objective rows. */
struct ReservedObjective {
    std::uint16_t flagIndex;
    std::uint16_t firstSlot;
    std::uint8_t slotCount;
    std::int32_t completionValue;
};

constexpr ReservedObjective kRememberYourMannersObjective{9448U, 3432U, 2U, 9};

[[nodiscard]] const ReservedObjective* find_reserved_objective(std::uint16_t flagIndex) noexcept {
    return flagIndex == kRememberYourMannersObjective.flagIndex ? &kRememberYourMannersObjective
                                                                : nullptr;
}

[[nodiscard]] std::size_t
write_reserved_objective(const ReservedObjective& objective,
                         std::int32_t value,
                         std::span<std::int32_t> objectiveValues) noexcept {
    std::size_t written = 0;
    for (std::uint8_t slot = 0; slot < objective.slotCount; ++slot) {
        const std::size_t valueSlot = static_cast<std::size_t>(objective.firstSlot) + slot;
        if (valueSlot < objectiveValues.size()) {
            objectiveValues[valueSlot] = std::min(value, objective.completionValue);
            ++written;
        }
    }
    return written;
}

[[nodiscard]] constexpr std::size_t flag_word(std::uint16_t flagIndex) noexcept {
    return static_cast<std::size_t>(flagIndex) / kWordBits;
}

[[nodiscard]] constexpr std::uint64_t flag_bit(std::uint16_t flagIndex) noexcept {
    return std::uint64_t{1} << (static_cast<std::size_t>(flagIndex) % kWordBits);
}

[[nodiscard]] bool claimed_locked(std::uint16_t flagIndex) noexcept {
    return static_cast<std::size_t>(flagIndex) < kIndexCapacity
           && (g_claimed[flag_word(flagIndex)] & flag_bit(flagIndex)) != 0;
}

[[nodiscard]] bool claimable_locked(std::uint16_t flagIndex) noexcept {
    return static_cast<std::size_t>(flagIndex) < kIndexCapacity
           && (g_claimable[flag_word(flagIndex)] & flag_bit(flagIndex)) != 0;
}

[[nodiscard]] bool pending_matches(std::uint16_t flagIndex, const PendingClaim* pending) noexcept {
    return pending != nullptr && pending->flagIndex == flagIndex
           && static_cast<std::size_t>(flagIndex) < kIndexCapacity;
}

[[nodiscard]] bool claimed_locked(std::uint16_t flagIndex, const PendingClaim* pending) noexcept {
    return claimed_locked(flagIndex) || pending_matches(flagIndex, pending);
}

[[nodiscard]] bool claimable_locked(std::uint16_t flagIndex, const PendingClaim* pending) noexcept {
    return !pending_matches(flagIndex, pending) && claimable_locked(flagIndex);
}

template <typename Value> void append_value(std::vector<char>& document, const Value& value);
[[nodiscard]] bool write_document(const core::path::Buffer& path,
                                  std::span<const char> document,
                                  const char* stage,
                                  std::size_t detail) noexcept;
[[nodiscard]] bool read_entries(const core::path::Buffer& path,
                                std::span<const char, 8> magic,
                                std::size_t entrySize,
                                const char* stage,
                                std::vector<char>& payload,
                                std::uint32_t& entries) noexcept;

[[nodiscard]] std::uint32_t total_score_locked(const PendingClaim* pending) noexcept {
    return g_score + g_intervalScore
           + static_cast<std::uint32_t>(pending != nullptr && !claimed_locked(pending->flagIndex)
                                                && static_cast<std::size_t>(pending->flagIndex)
                                                       < kIndexCapacity
                                            ? pending->scoreValue
                                            : 0U);
}

[[nodiscard]] bool store_intervals_locked() noexcept {
    if (!g_intervalPathReady) {
        return !g_persistenceRequired;
    }
    std::vector<char> document{kIntervalMagic.begin(), kIntervalMagic.end()};
    const std::size_t countOffset = document.size();
    document.resize(countOffset + sizeof(std::uint32_t));
    std::uint32_t count = 0;
    for (std::size_t index = 0; index < g_intervalClaims.size(); ++index) {
        if (g_intervalClaims[index] == 0) {
            continue;
        }
        append_value(document, kIntervalDefinitions[index].recordIndex);
        append_value(document, g_intervalClaims[index]);
        constexpr std::uint8_t kPadding = 0;
        append_value(document, kPadding);
        ++count;
    }
    std::memcpy(document.data() + countOffset, &count, sizeof count);
    return write_document(g_intervalPath, document, "store_intervals", count);
}

void load_intervals_locked() noexcept {
    std::vector<char> payload;
    std::uint32_t entries = 0;
    if (!read_entries(g_intervalPath,
                      kIntervalMagic,
                      kIntervalEntrySize,
                      "load_intervals",
                      payload,
                      entries)) {
        return;
    }
    for (std::uint32_t entry = 0; entry < entries; ++entry) {
        std::uint16_t recordIndex = 0;
        std::uint8_t redeemed = 0;
        const std::size_t at = static_cast<std::size_t>(entry) * kIntervalEntrySize;
        std::memcpy(&recordIndex, payload.data() + at, sizeof recordIndex);
        std::memcpy(&redeemed, payload.data() + at + sizeof recordIndex, sizeof redeemed);
        for (std::size_t index = 0; index < kIntervalDefinitions.size(); ++index) {
            const IntervalDefinition& definition = kIntervalDefinitions[index];
            if (definition.recordIndex != recordIndex || redeemed > definition.intervalCount) {
                continue;
            }
            g_intervalClaims[index] = redeemed;
            for (std::uint8_t interval = 0; interval < redeemed; ++interval) {
                g_intervalScore += definition.scores[interval];
            }
            break;
        }
    }
}

void report_failure(const char* stage, const char* result, std::size_t detail) noexcept {
    if (!core::log::accepts(core::log::Channel::state, core::log::Level::warn)) {
        return;
    }
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=claims stage=%s result=%s entries=%zu",
                                      stage,
                                      result,
                                      detail);
    if (written > 0) {
        core::log::write(
            core::log::Channel::state,
            core::log::Level::warn,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

template <typename Value> void append_value(std::vector<char>& document, const Value& value) {
    const auto* bytes = reinterpret_cast<const char*>(&value);
    document.insert(document.end(), bytes, bytes + sizeof value);
}

/** Durably stages a complete document, then atomically replaces its persistence file. */
[[nodiscard]] bool write_document(const core::path::Buffer& path,
                                  std::span<const char> document,
                                  const char* stage,
                                  std::size_t detail) noexcept {
    core::path::Buffer stagedPath = path;
    if (!core::path::append(stagedPath, kWriteStageSuffix)) {
        report_failure(stage, "path_fail", detail);
        return false;
    }
    const HANDLE file = CreateFileW(stagedPath.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        report_failure(stage, "open_fail", detail);
        return false;
    }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(document.size());
    bool complete =
        WriteFile(file, document.data(), size, &written, nullptr) != FALSE && written == size;
    complete = complete && FlushFileBuffers(file) != FALSE;
    complete = CloseHandle(file) != FALSE && complete;
    complete = complete
               && MoveFileExW(stagedPath.chars.data(),
                              path.chars.data(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                      != FALSE;
    if (!complete) {
        (void)DeleteFileW(stagedPath.chars.data());
    }
    if (!complete) {
        report_failure(stage, "write_fail", detail);
    }
    return complete;
}

/** Reads and validates one header plus a fixed-width entry payload. */
[[nodiscard]] bool read_entries(const core::path::Buffer& path,
                                std::span<const char, 8> magic,
                                std::size_t entrySize,
                                const char* stage,
                                std::vector<char>& payload,
                                std::uint32_t& entries) noexcept {
    payload.clear();
    entries = 0;
    const HANDLE file = CreateFileW(path.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::array<char, 12> header{};
    DWORD read = 0;
    if (ReadFile(file, header.data(), static_cast<DWORD>(header.size()), &read, nullptr) == FALSE
        || read != header.size() || std::memcmp(header.data(), magic.data(), magic.size()) != 0) {
        (void)CloseHandle(file);
        report_failure(stage, "header_fail", 0);
        return false;
    }
    std::memcpy(&entries, header.data() + magic.size(), sizeof entries);
    if (entries > kMaximumEntries) {
        (void)CloseHandle(file);
        report_failure(stage, "count_fail", entries);
        return false;
    }
    payload.resize(static_cast<std::size_t>(entries) * entrySize);
    read = 0;
    const bool complete =
        payload.empty()
        || (ReadFile(file, payload.data(), static_cast<DWORD>(payload.size()), &read, nullptr)
                != FALSE
            && read == payload.size());
    (void)CloseHandle(file);
    if (!complete) {
        report_failure(stage, "read_fail", entries);
    }
    return complete;
}

/** Writes every held claim. The caller holds the lock. */
[[nodiscard]] bool store_locked() noexcept {
    if (!g_pathReady) {
        return !g_persistenceRequired;
    }
    std::vector<char> document{};
    document.reserve(kMagic.size() + sizeof(std::uint32_t) + g_count * kEntrySize);
    document.insert(document.end(), kMagic.begin(), kMagic.end());
    const auto entries = static_cast<std::uint32_t>(g_count);
    append_value(document, entries);
    for (std::size_t word = 0; word < g_claimed.size(); ++word) {
        std::uint64_t bits = g_claimed[word];
        while (bits != 0) {
            const auto offset = static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const std::size_t index = word * kWordBits + offset;
            const auto packedIndex = static_cast<std::uint16_t>(index);
            const std::uint16_t packedScore = g_scoreByIndex[index];
            append_value(document, packedIndex);
            append_value(document, packedScore);
        }
    }

    return write_document(g_path, document, "store", g_count);
}

/** Reads every claim the file holds. The caller holds the lock. */
void load_locked() noexcept {
    std::vector<char> payload;
    std::uint32_t entries = 0;
    if (!read_entries(g_path, kMagic, kEntrySize, "load", payload, entries)) {
        return;
    }

    for (std::uint32_t entry = 0; entry < entries; ++entry) {
        std::uint16_t index = 0;
        std::uint16_t score = 0;
        std::memcpy(
            &index, payload.data() + static_cast<std::size_t>(entry) * kEntrySize, sizeof index);
        std::memcpy(&score,
                    payload.data() + static_cast<std::size_t>(entry) * kEntrySize + sizeof index,
                    sizeof score);
        if (static_cast<std::size_t>(index) >= kIndexCapacity) {
            continue;
        }
        const std::size_t word = flag_word(index);
        const std::uint64_t bit = flag_bit(index);
        if ((g_claimed[word] & bit) != 0) {
            continue;
        }
        g_claimed[word] |= bit;
        g_scoreByIndex[index] = score;
        g_score += score;
        ++g_count;
    }
}

/** Writes every completion that has not been claimed. The caller holds the lock. */
[[nodiscard]] bool store_claimable_locked() noexcept {
    if (!g_claimablePathReady) {
        return !g_persistenceRequired;
    }
    std::vector<char> document{};
    document.insert(document.end(), kClaimableMagic.begin(), kClaimableMagic.end());
    const std::size_t countOffset = document.size();
    document.resize(countOffset + sizeof(std::uint32_t));
    std::size_t held = 0;
    for (std::size_t word = 0; word < g_claimable.size(); ++word) {
        // Claimed state supersedes claimable state.
        std::uint64_t bits = g_claimable[word] & ~g_claimed[word];
        while (bits != 0) {
            const auto offset = static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const auto packedIndex = static_cast<std::uint16_t>(word * kWordBits + offset);
            constexpr std::uint16_t kUnscored = 0;
            append_value(document, packedIndex);
            append_value(document, kUnscored);
            ++held;
        }
    }
    const auto count = static_cast<std::uint32_t>(held);
    std::memcpy(document.data() + countOffset, &count, sizeof count);

    return write_document(g_claimablePath, document, "store_claimable", held);
}

/** Reads every unclaimed completion the file holds. The caller holds the lock. */
void load_claimable_locked() noexcept {
    std::vector<char> payload;
    std::uint32_t entries = 0;
    if (!read_entries(
            g_claimablePath, kClaimableMagic, kEntrySize, "load_claimable", payload, entries)) {
        return;
    }

    for (std::uint32_t entry = 0; entry < entries; ++entry) {
        std::uint16_t index = 0;
        std::memcpy(
            &index, payload.data() + static_cast<std::size_t>(entry) * kEntrySize, sizeof index);
        if (static_cast<std::size_t>(index) >= kIndexCapacity) {
            continue;
        }
        if (claimed_locked(index)) {
            continue;
        }
        g_claimable[flag_word(index)] |= flag_bit(index);
    }
}

/** Writes every nonzero partial objective value. The caller holds the lock. */
[[nodiscard]] bool store_progress_locked() noexcept {
    if (!g_progressPathReady) {
        return !g_persistenceRequired;
    }
    std::vector<char> document{};
    document.insert(document.end(), kProgressMagic.begin(), kProgressMagic.end());
    const std::size_t countOffset = document.size();
    document.resize(countOffset + sizeof(std::uint32_t));
    std::uint32_t count = 0;
    for (std::size_t index = 0; index < g_progress.size(); ++index) {
        if (g_progress[index] <= 0 || claimed_locked(static_cast<std::uint16_t>(index))
            || claimable_locked(static_cast<std::uint16_t>(index))) {
            continue;
        }
        const auto packedIndex = static_cast<std::uint16_t>(index);
        append_value(document, packedIndex);
        append_value(document, g_progress[index]);
        ++count;
    }
    std::memcpy(document.data() + countOffset, &count, sizeof count);

    return write_document(g_progressPath, document, "store_progress", count);
}

/** Reads persisted partial objective values. The caller holds the lock. */
void load_progress_locked() noexcept {
    std::vector<char> payload;
    std::uint32_t entries = 0;
    if (!read_entries(g_progressPath,
                      kProgressMagic,
                      kProgressEntrySize,
                      "load_progress",
                      payload,
                      entries)) {
        return;
    }

    for (std::uint32_t entry = 0; entry < entries; ++entry) {
        const std::size_t at = static_cast<std::size_t>(entry) * kProgressEntrySize;
        std::uint16_t index = 0;
        std::int32_t value = 0;
        std::memcpy(&index, payload.data() + at, sizeof index);
        std::memcpy(&value, payload.data() + at + sizeof index, sizeof value);
        if (static_cast<std::size_t>(index) >= kIndexCapacity || value <= 0 || claimed_locked(index)
            || claimable_locked(index)) {
            continue;
        }
        g_progress[index] = value;
    }
}

} // namespace

/** Derives the claim file path and loads any claims already held. */
bool initialize(void* module) noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    clear_locked();
    g_path = {};
    g_claimablePath = {};
    g_progressPath = {};
    g_intervalPath = {};
    g_pathReady = false;
    g_claimablePathReady = false;
    g_progressPathReady = false;
    g_intervalPathReady = false;
    g_persistenceRequired = module != nullptr;
    if (!g_persistenceRequired) {
        return true;
    }
    if (!core::path::artifact_directory(module, g_path)
        || !core::path::append(g_path, kClaimFileSuffix)) {
        report_failure("initialize", "path_fail", 0);
        return false;
    }
    g_pathReady = true;
    // Load claims first so stale claimable rows cannot supersede them.
    load_locked();

    if (core::path::artifact_directory(module, g_claimablePath)
        && core::path::append(g_claimablePath, kClaimableFileSuffix)) {
        g_claimablePathReady = true;
        load_claimable_locked();
    } else {
        report_failure("initialize", "claimable_path_fail", 0);
    }

    if (core::path::artifact_directory(module, g_progressPath)
        && core::path::append(g_progressPath, kProgressFileSuffix)) {
        g_progressPathReady = true;
        load_progress_locked();
    } else {
        report_failure("initialize", "progress_path_fail", 0);
    }
    if (core::path::artifact_directory(module, g_intervalPath)
        && core::path::append(g_intervalPath, kIntervalFileSuffix)) {
        g_intervalPathReady = true;
        load_intervals_locked();
    } else {
        report_failure("initialize", "interval_path_fail", 0);
    }
    return true;
}

/** Marks one account flag bank index claimed, adds its score, and writes the claim file. */
bool claim(std::uint16_t flagIndex, std::uint16_t scoreValue) noexcept {
    if (static_cast<std::size_t>(flagIndex) >= kIndexCapacity) {
        return false;
    }
    const std::size_t word = flag_word(flagIndex);
    const std::uint64_t bit = flag_bit(flagIndex);
    const std::lock_guard<std::mutex> guard(g_lock);
    if ((g_claimed[word] & bit) != 0) {
        return false;
    }
    const std::uint64_t previousClaimedWord = g_claimed[word];
    const std::uint64_t previousClaimableWord = g_claimable[word];
    const std::int32_t previousProgress = g_progress[flagIndex];
    const std::uint16_t previousScoreByIndex = g_scoreByIndex[flagIndex];
    const std::size_t previousCount = g_count;
    const std::uint32_t previousScore = g_score;
    g_claimed[word] |= bit;
    g_claimable[word] &= ~bit;
    g_progress[flagIndex] = 0;
    g_scoreByIndex[flagIndex] = scoreValue;
    ++g_count;
    g_score += scoreValue;
    if (store_locked() && store_claimable_locked() && store_progress_locked()) {
        return true;
    }

    g_claimed[word] = previousClaimedWord;
    g_claimable[word] = previousClaimableWord;
    g_progress[flagIndex] = previousProgress;
    g_scoreByIndex[flagIndex] = previousScoreByIndex;
    g_count = previousCount;
    g_score = previousScore;
    // A later document can fail after an earlier atomic replace. Re-publish the complete before
    // images so a rejected claim cannot become held after restart.
    (void)store_locked();
    (void)store_claimable_locked();
    (void)store_progress_locked();
    return false;
}

bool claim_interval(std::uint16_t recordIndex, std::uint32_t definitionHash) noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    for (std::size_t index = 0; index < kIntervalDefinitions.size(); ++index) {
        const IntervalDefinition& definition = kIntervalDefinitions[index];
        if (definition.recordIndex != recordIndex || definition.definitionHash != definitionHash) {
            continue;
        }
        std::uint8_t& redeemed = g_intervalClaims[index];
        if (redeemed >= definition.intervalCount) {
            return false;
        }
        const std::uint8_t previous = redeemed;
        const std::uint32_t previousScore = g_intervalScore;
        g_intervalScore += definition.scores[redeemed];
        ++redeemed;
        if (store_intervals_locked()) {
            return true;
        }
        redeemed = previous;
        g_intervalScore = previousScore;
        (void)store_intervals_locked();
        return false;
    }
    return false;
}

/** Lays every held claim over one account flag bank. */
static std::size_t apply_locked(std::span<std::uint8_t> accountFlags,
                                const PendingClaim* pending) noexcept {
    std::size_t changed = 0;
    for (std::size_t word = 0; word < g_claimed.size(); ++word) {
        std::uint64_t bits = g_claimed[word];
        while (bits != 0) {
            const auto offset = static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const std::size_t index = word * kWordBits + offset;
            // A bank shorter than the index space is not an error: the tail simply is not sent.
            if (index >= accountFlags.size()) {
                continue;
            }
            if (accountFlags[index] != unlocks::kFlagSet) {
                accountFlags[index] = unlocks::kFlagSet;
                ++changed;
            }
        }
    }

    if (pending != nullptr && !claimed_locked(pending->flagIndex)
        && static_cast<std::size_t>(pending->flagIndex) < accountFlags.size()) {
        if (accountFlags[pending->flagIndex] != unlocks::kFlagSet) {
            accountFlags[pending->flagIndex] = unlocks::kFlagSet;
            ++changed;
        }
    }

    // Claimable state is represented by objective values, not completion flags.
    return changed;
}

/** Clears the authored completion values of every record owned by a lore book. */
static std::size_t clear_lore_objectives(std::span<std::int32_t> objectiveValues) noexcept {
    struct ClearState {
        std::span<std::int32_t> values;
        std::size_t cleared{};
    } state{objectiveValues};
    build_data::nodes::for_each(
        &state, [](void* context, const build_data::nodes::Definition& node) noexcept {
            if (!build_data::nodes::lore_category(node.definitionIndex)) {
                return;
            }
            auto* clear = static_cast<ClearState*>(context);
            for (std::size_t child = 0; child < node.childCount; ++child) {
                build_data::records::Definition record{};
                if (!build_data::find_record_definition(node.children[child], record)
                    || record.completionFlagIndex == build_data::records::kUnavailableFlagIndex) {
                    continue;
                }
                const auto found = objective_slot_table::find_record(record.completionFlagIndex);
                if (!found) {
                    continue;
                }
                for (std::uint8_t objective = 0; objective < found.objectiveCount; ++objective) {
                    const std::size_t at =
                        static_cast<std::size_t>(found.firstObjective) + objective;
                    if (at >= objective_slot_table::kObjectiveCount) {
                        break;
                    }
                    const std::size_t slot = objective_slot_table::objective(at).slot;
                    if (slot >= clear->values.size()) {
                        continue;
                    }
                    clear->values[slot] = 0;
                    ++clear->cleared;
                }
            }
        });
    return state.cleared;
}

namespace {

/** Carries the bank and a tally through the node walk, which takes a plain function pointer. */
struct NodeProgress {
    std::span<std::int32_t> values;
    std::size_t written;
    const PendingClaim* pending;
};

/** One authored reward milestone at the head of a presentation node's child list. */
struct RewardMilestone {
    std::uint16_t completionFlagIndex{};
    std::uint16_t objectiveSlot{};
    std::int32_t completionValue{};
};

/**
 * Resolves the shape shared by node-level reward milestones: a zero-score reward record with one
 * positive objective. The node walk below additionally requires a descending consecutive cluster,
 * which distinguishes milestone bands from unrelated reward-bearing child records.
 */
[[nodiscard]] bool resolve_reward_milestone(std::uint16_t recordIndex,
                                            RewardMilestone& milestone) noexcept {
    milestone = {};
    build_data::records::Definition record{};
    if (!build_data::find_record_definition(recordIndex, record)
        || record.scoreValue != 0
        || record.completionFlagIndex == build_data::records::kUnavailableFlagIndex) {
        return false;
    }
    const auto entry = objective_slot_table::find_record(record.completionFlagIndex);
    if (!entry || entry.objectiveCount != 1
        || entry.firstObjective >= objective_slot_table::kObjectiveCount) {
        return false;
    }
    const auto objective = objective_slot_table::objective(entry.firstObjective);
    if (objective.completionValue <= 0) {
        return false;
    }
    std::array<build_data::records::rewards::ResolvedReward,
               build_data::records::rewards::kRewardPerRecordCapacity>
        rewards{};
    std::size_t rewardCount = 0;
    if (!build_data::find_generated_record_rewards(
            record.definitionHash, rewards, rewardCount)
        || rewardCount == 0) {
        return false;
    }
    milestone = {record.completionFlagIndex, objective.slot, objective.completionValue};
    return true;
}

/** Caches counter-granted chapter flags once the immutable node catalog is ready. */
[[nodiscard]] const std::array<std::uint64_t, kWordCount>&
counter_granted_chapter_mask_locked() noexcept {
    if (g_counterGrantedChaptersReady || !build_data::node_definitions_ready()
        || !build_data::record_definitions_ready()) {
        return g_counterGrantedChapters;
    }
    build_data::nodes::for_each(
        &g_counterGrantedChapters,
        [](void* context, const build_data::nodes::Definition& node) noexcept {
            if (!counter_granted_lore(node.definitionIndex)) {
                return;
            }
            auto* bits = static_cast<std::array<std::uint64_t, kWordCount>*>(context);
            for (std::size_t child = 0; child < node.childCount; ++child) {
                build_data::records::Definition record{};
                if (!build_data::find_record_definition(node.children[child], record)
                    || record.loreRow == build_data::records::kUnavailableLoreRow
                    || static_cast<std::size_t>(record.completionFlagIndex) >= kIndexCapacity) {
                    continue;
                }
                const std::size_t index = record.completionFlagIndex;
                (*bits)[index / kWordBits] |= std::uint64_t{1} << (index % kWordBits);
            }
        });
    g_counterGrantedChaptersReady = true;
    return g_counterGrantedChapters;
}

struct ChapterCounts {
    std::int32_t claimed{};
    std::int32_t collected{};
    std::int32_t allCollected{};
    std::int32_t claimedParent{};
    bool cumulative{};
};

/** Counts one lore node's chapters. The caller holds the claim lock. */
[[nodiscard]] ChapterCounts chapter_counts(const build_data::nodes::Definition& node,
                                           const PendingClaim* pending = nullptr) noexcept {
    ChapterCounts counts{};
    for (std::size_t child = 0; child < node.childCount; ++child) {
        build_data::records::Definition record{};
        if (!build_data::find_record_definition(node.children[child], record)
            || record.completionFlagIndex == build_data::records::kUnavailableFlagIndex) {
            continue;
        }
        const bool isClaimed = claimed_locked(record.completionFlagIndex, pending);
        const bool isCollected = isClaimed || claimable_locked(record.completionFlagIndex, pending);
        counts.allCollected += isCollected;
        if (record.loreRow == build_data::records::kUnavailableLoreRow) {
            counts.claimedParent += isClaimed && record.categoryValueIndex == node.valueIndex;
            continue;
        }
        counts.claimed += isClaimed;
        counts.collected += isCollected;
        const auto entry = objective_slot_table::find_record(record.completionFlagIndex);
        counts.cumulative =
            counts.cumulative
            || (entry && entry.firstObjective < objective_slot_table::kObjectiveCount
                && objective_slot_table::objective(entry.firstObjective).completionValue > 1);
    }
    return counts;
}

/** Measured Year 1 chapter gate: base plus record row. */
constexpr std::int32_t kChapterGateBase = 1935;
/** Earlier rows overlap parent bars and must retain their counts. */
constexpr std::int32_t kChapterGateFirstWritable = 1942;
constexpr std::uint16_t kChapterGateLastRow = 106;

} // namespace

/** Publishes the per-chapter visibility gate only for Year 1 lore chapters already collected. */
static std::size_t apply_chapter_visibility_gates_locked(std::span<std::int32_t> objectiveValues,
                                                         const PendingClaim* pending) noexcept {
    std::size_t written = 0;
    for (std::uint16_t row = 0; row <= kChapterGateLastRow; ++row) {
        build_data::records::Definition record{};
        if (!build_data::find_record_definition(row, record)
            || record.loreRow == build_data::records::kUnavailableLoreRow
            || record.completionFlagIndex == build_data::records::kUnavailableFlagIndex
            || (!claimed_locked(record.completionFlagIndex, pending)
                && !claimable_locked(record.completionFlagIndex, pending))) {
            continue;
        }
        const auto found = objective_slot_table::find_record(record.completionFlagIndex);
        if (!found) {
            continue;
        }
        const std::size_t objective = found.firstObjective;
        const std::size_t gate = static_cast<std::size_t>(kChapterGateBase) + row;
        if (row < static_cast<std::uint16_t>(kChapterGateFirstWritable - kChapterGateBase)
            || objective >= objective_slot_table::kObjectiveCount
            || gate >= objectiveValues.size()) {
            continue;
        }
        const std::int32_t completion = objective_slot_table::objective(objective).completionValue;
        if (objectiveValues[gate] < completion) {
            objectiveValues[gate] = completion;
        }
        ++written;
    }
    return written;
}

static std::size_t apply_node_progress_locked(std::span<std::int32_t> objectiveValues,
                                              const PendingClaim* pending) noexcept {
    NodeProgress progress{objectiveValues, 0, pending};
    build_data::nodes::for_each(
        &progress, [](void* context, const build_data::nodes::Definition& node) noexcept {
            auto* state = static_cast<NodeProgress*>(context);
            // Preserve authored values for non-lore categories.
            if (!build_data::nodes::lore_category(node.definitionIndex)) {
                return;
            }
            const ChapterCounts counts = chapter_counts(node, state->pending);
            const bool counterGranted = counter_granted_lore(node.definitionIndex);

            // Parent-bar slots come from the extracted table.
            std::int32_t parentSlot = -1;
            for (const auto& bar : parent_bar_table::kBars) {
                if (bar.nodeIndex != node.definitionIndex) {
                    continue;
                }
                // Category gates later restore shared zero-valued slots.
                if (static_cast<std::size_t>(bar.valueIndex) < state->values.size()) {
                    state->values[bar.valueIndex] =
                        counterGranted ? counts.collected : counts.claimed;
                    parentSlot = static_cast<std::int32_t>(bar.valueIndex);
                    ++state->written;
                }
                break;
            }
            // Never overwrite record-objective slots that happen to be named by a node.
            if (node.valueIndex != build_data::nodes::kUnavailableValueIndex
                && static_cast<std::int32_t>(node.valueIndex)
                       < objective_slot_table::kRecordObjectiveRangeStart
                && static_cast<std::size_t>(node.valueIndex) < state->values.size()) {
                if (counterGranted) {
                    state->values[node.valueIndex] = counts.collected;
                    ++state->written;
                } else if (counts.cumulative && counts.collected > 0) {
                    // Keep a cumulative collection counter distinct from its claim bar.
                    std::uint16_t counterSlot = node.valueIndex;
                    if (parentSlot == static_cast<std::int32_t>(node.valueIndex)
                        && node.parentValueIndex != build_data::nodes::kUnavailableValueIndex) {
                        counterSlot = node.parentValueIndex;
                    }
                    if (static_cast<std::int32_t>(counterSlot)
                            < objective_slot_table::kRecordObjectiveRangeStart
                        && static_cast<std::size_t>(counterSlot) < state->values.size()) {
                        state->values[counterSlot] = counts.collected;
                        ++state->written;
                    }
                }
            }
            // Some books expose only the derived parent slot.
            if (parentSlot < 0 && node.parentValueIndex != build_data::nodes::kUnavailableValueIndex
                && static_cast<std::size_t>(node.parentValueIndex) < state->values.size()) {
                state->values[node.parentValueIndex] = counts.claimed;
                ++state->written;
            }
        });
    return progress.written;
}

/**
 * Publishes the count read by authored node-level reward milestones.
 *
 * These records are a descending reward band at the head of a node's children (for example the
 * MMXIX 15/10/5/1 shirt, ship, sparrow and emblem rows). Their cards can derive presentation from
 * completed siblings, but their action binding still reads the authoritative objective bank. A
 * zero bank therefore looks claimable while remaining inert. The installed node ordering,
 * reward table, objective slots and thresholds identify the band without season-specific ids.
 */
static std::size_t
apply_reward_milestone_progress_locked(std::span<std::int32_t> objectiveValues,
                                       const PendingClaim* pending) noexcept {
    struct Projection {
        std::span<std::int32_t> values;
        const PendingClaim* pending;
        std::size_t written{};
    } projection{objectiveValues, pending};

    build_data::nodes::for_each(
        &projection, [](void* context, const build_data::nodes::Definition& node) noexcept {
            auto* state = static_cast<Projection*>(context);
            std::array<RewardMilestone, build_data::nodes::kChildCapacity> milestones{};
            std::size_t milestoneCount = 0;
            for (; milestoneCount < node.childCount; ++milestoneCount) {
                RewardMilestone milestone{};
                if (!resolve_reward_milestone(node.children[milestoneCount], milestone)) {
                    break;
                }
                if (milestoneCount != 0) {
                    const RewardMilestone& previous = milestones[milestoneCount - 1];
                    if (milestone.completionValue >= previous.completionValue
                        || static_cast<std::uint32_t>(milestone.objectiveSlot) + 1U
                               != previous.objectiveSlot) {
                        break;
                    }
                }
                milestones[milestoneCount] = milestone;
            }
            // A single reward record can have an ordinary objective. Two or more descending,
            // consecutive rows are the authored milestone-band signature.
            if (milestoneCount < 2) {
                return;
            }

            std::int32_t completedChildren = 0;
            for (std::size_t child = milestoneCount; child < node.childCount; ++child) {
                build_data::records::Definition record{};
                if (build_data::find_record_definition(node.children[child], record)
                    && record.completionFlagIndex
                           != build_data::records::kUnavailableFlagIndex
                    && claimed_locked(record.completionFlagIndex, state->pending)) {
                    ++completedChildren;
                }
            }
            for (std::size_t index = 0; index < milestoneCount; ++index) {
                const RewardMilestone& milestone = milestones[index];
                if (claimed_locked(milestone.completionFlagIndex, state->pending)
                    || static_cast<std::size_t>(milestone.objectiveSlot)
                           >= state->values.size()) {
                    continue;
                }
                state->values[milestone.objectiveSlot] =
                    (std::min)(completedChildren, milestone.completionValue);
                ++state->written;
            }
        });
    return projection.written;
}

/**
 * Publishes partial and complete-but-unclaimed objective values. Completion flags remain clear
 * until claim, and multi-objective rows occupy consecutive mapped slots.
 */
static std::size_t apply_claimable_objectives_locked(std::span<std::int32_t> objectiveValues,
                                                     const PendingClaim* pending) noexcept {
    std::size_t written = 0;
    const auto& counterGrantedChapters = counter_granted_chapter_mask_locked();
    for (std::size_t index = 0; index < g_progress.size(); ++index) {
        if (g_progress[index] <= 0 || claimed_locked(static_cast<std::uint16_t>(index), pending)
            || claimable_locked(static_cast<std::uint16_t>(index), pending)) {
            continue;
        }
        const auto flagIndex = static_cast<std::uint16_t>(index);
        const auto found = objective_slot_table::find_record(flagIndex);
        if (!found || found.objectiveCount != 1
            || static_cast<std::size_t>(found.firstObjective)
                   >= objective_slot_table::kObjectiveCount) {
            const ReservedObjective* reserved = find_reserved_objective(flagIndex);
            if (reserved == nullptr) {
                continue;
            }
            written += write_reserved_objective(*reserved, g_progress[index], objectiveValues);
            continue;
        }
        const auto objective = objective_slot_table::objective(found.firstObjective);
        if (static_cast<std::size_t>(objective.slot) >= objectiveValues.size()) {
            continue;
        }
        objectiveValues[objective.slot] = std::min(g_progress[index], objective.completionValue);
        ++written;
    }
    for (std::size_t word = 0; word < g_claimable.size(); ++word) {
        // Claimed flags already surface; rewriting their shared objective slots corrupts lore
        // gates.
        std::uint64_t bits = g_claimable[word] & ~g_claimed[word] & ~counterGrantedChapters[word];
        if (pending != nullptr && flag_word(pending->flagIndex) == word) {
            bits &= ~flag_bit(pending->flagIndex);
        }
        while (bits != 0) {
            const auto offset = static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const auto flagIndex = static_cast<std::uint16_t>(word * kWordBits + offset);
            const auto found = objective_slot_table::find_record(flagIndex);
            if (!found) {
                const ReservedObjective* reserved = find_reserved_objective(flagIndex);
                if (reserved != nullptr) {
                    written += write_reserved_objective(
                        *reserved, reserved->completionValue, objectiveValues);
                }
                continue;
            }
            for (std::uint8_t slot = 0; slot < found.objectiveCount; ++slot) {
                const std::size_t objectiveIndex =
                    static_cast<std::size_t>(found.firstObjective) + slot;
                if (objectiveIndex >= objective_slot_table::kObjectiveCount) {
                    break;
                }
                const auto objective = objective_slot_table::objective(objectiveIndex);
                // A bank shorter than the slot space is not an error: the tail simply is not sent.
                if (static_cast<std::size_t>(objective.slot) >= objectiveValues.size()) {
                    continue;
                }
                objectiveValues[objective.slot] = objective.completionValue;
                ++written;
            }
        }
    }
    return written;
}

void apply_account_projection(std::span<std::uint8_t> accountFlags,
                              std::span<std::int32_t> objectiveValues,
                              const PendingClaim* pending) noexcept {
    (void)clear_lore_objectives(objectiveValues);
    const std::lock_guard<std::mutex> guard(g_lock);
    (void)apply_locked(accountFlags, pending);
    (void)build_data::nodes::apply_visibility(accountFlags);
    if (build_data::records::kTriumphScoreValueIndex < objectiveValues.size()) {
        objectiveValues[build_data::records::kTriumphScoreValueIndex] +=
            static_cast<std::int32_t>(total_score_locked(pending));
    }
    for (std::size_t index = 0; index < kIntervalDefinitions.size(); ++index) {
        const std::size_t slot = kIntervalDefinitions[index].redeemedCountSlot;
        if (slot < objectiveValues.size()) {
            objectiveValues[slot] =
                (std::max)(objectiveValues[slot], static_cast<std::int32_t>(g_intervalClaims[index]));
        }
    }
    (void)apply_node_progress_locked(objectiveValues, pending);
    (void)apply_reward_milestone_progress_locked(objectiveValues, pending);
    (void)apply_chapter_visibility_gates_locked(objectiveValues, pending);
    (void)apply_claimable_objectives_locked(objectiveValues, pending);
}

/** Writes each category's claimed-child count into the character value slot its bar reads. */
std::size_t apply_character_node_progress(std::span<std::int32_t> characterValues,
                                          const PendingClaim* pending) noexcept {
    NodeProgress progress{characterValues, 0, pending};
    // Same lock order as the account pass: claims first, catalog inside the walk.
    const std::lock_guard<std::mutex> guard(g_lock);
    build_data::nodes::for_each(
        &progress, [](void* context, const build_data::nodes::Definition& node) noexcept {
            auto* state = static_cast<NodeProgress*>(context);
            if (!build_data::nodes::lore_category(node.definitionIndex)) {
                return;
            }
            const ChapterCounts counts = chapter_counts(node, state->pending);
            if (node.characterValueIndex != build_data::nodes::kUnavailableValueIndex
                && static_cast<std::size_t>(node.characterValueIndex) < state->values.size()) {
                state->values[node.characterValueIndex] = counts.allCollected;
                ++state->written;
            }
            // The character-scoped books need their parent bar fed here too: their category
            // counts from this bank, and their parent's bar is character-scoped as well. The
            // parent index was resolved through the character value map at extraction.
            if (node.parentCharacterValueIndex != build_data::nodes::kUnavailableValueIndex
                && static_cast<std::size_t>(node.parentCharacterValueIndex)
                       < state->values.size()) {
                state->values[node.parentCharacterValueIndex] =
                    counts.allCollected - counts.claimedParent;
                ++state->written;
            }
        });
    return progress.written;
}

/** @return True when this index is already held. */
bool claimed(std::uint16_t flagIndex) noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    return claimed_locked(flagIndex);
}

/** Marks one record complete but unclaimed. */
bool mark_claimable(std::uint16_t flagIndex) noexcept {
    if (static_cast<std::size_t>(flagIndex) >= kIndexCapacity) {
        return false;
    }
    const std::lock_guard<std::mutex> guard(g_lock);
    if (claimed_locked(flagIndex) || claimable_locked(flagIndex)) {
        return false;
    }
    const std::uint64_t previousClaimableWord = g_claimable[flag_word(flagIndex)];
    const std::int32_t previousProgress = g_progress[flagIndex];
    g_progress[flagIndex] = 0;
    g_claimable[flag_word(flagIndex)] |= flag_bit(flagIndex);
    if (store_claimable_locked() && store_progress_locked()) {
        return true;
    }

    g_claimable[flag_word(flagIndex)] = previousClaimableWord;
    g_progress[flagIndex] = previousProgress;
    (void)store_claimable_locked();
    (void)store_progress_locked();
    return false;
}

/** Advances one persisted objective and promotes it to claimable at its authored threshold. */
ObjectiveAdvance advance_single_objective(std::uint16_t flagIndex) noexcept {
    if (static_cast<std::size_t>(flagIndex) >= kIndexCapacity) {
        return ObjectiveAdvance::unavailable;
    }
    const auto found = objective_slot_table::find_record(flagIndex);
    std::int32_t completion = 0;
    if (found && found.objectiveCount == 1
        && static_cast<std::size_t>(found.firstObjective) < objective_slot_table::kObjectiveCount) {
        completion = objective_slot_table::objective(found.firstObjective).completionValue;
    } else {
        const ReservedObjective* reserved = find_reserved_objective(flagIndex);
        if (reserved == nullptr) {
            return ObjectiveAdvance::unavailable;
        }
        completion = reserved->completionValue;
    }
    if (completion <= 0) {
        return ObjectiveAdvance::unavailable;
    }

    const std::lock_guard<std::mutex> guard(g_lock);
    if (claimed_locked(flagIndex) || claimable_locked(flagIndex)) {
        return ObjectiveAdvance::alreadyHeld;
    }
    const std::uint64_t previousClaimableWord = g_claimable[flag_word(flagIndex)];
    const std::int32_t previousProgress = g_progress[flagIndex];
    const std::int32_t next =
        g_progress[flagIndex] >= completion - 1 ? completion : g_progress[flagIndex] + 1;
    if (next >= completion) {
        g_progress[flagIndex] = 0;
        g_claimable[flag_word(flagIndex)] |= flag_bit(flagIndex);
        if (store_progress_locked() && store_claimable_locked()) {
            return ObjectiveAdvance::completed;
        }
        g_progress[flagIndex] = previousProgress;
        g_claimable[flag_word(flagIndex)] = previousClaimableWord;
        (void)store_progress_locked();
        (void)store_claimable_locked();
        return ObjectiveAdvance::unavailable;
    }
    g_progress[flagIndex] = next;
    if (store_progress_locked()) {
        return ObjectiveAdvance::advanced;
    }
    g_progress[flagIndex] = previousProgress;
    (void)store_progress_locked();
    return ObjectiveAdvance::unavailable;
}

/** @return True when this index is marked claimable. */
bool claimable(std::uint16_t flagIndex) noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    return claimable_locked(flagIndex);
}

/** @return Total score of every held claim. */
std::uint32_t total_score() noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    return total_score_locked(nullptr);
}

/** @return Number of distinct indices held. */
std::size_t count() noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    return g_count;
}

void invalidate_build_data_cache() noexcept {
    const std::lock_guard<std::mutex> guard(g_lock);
    clear_counter_granted_chapters_locked();
}

} // namespace sunrise::state::record_claims
