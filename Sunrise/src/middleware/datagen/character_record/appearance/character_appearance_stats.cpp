#include <algorithm>
#include <atomic>

#include "../../../../core/logging/log.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/equipment/light/definition.h"
#include "../../../../state/progression/seasonal_experience.h"
#include "internal.h"

namespace sunrise::middleware::datagen::character_record::appearance {
namespace {

namespace constants = state::build_data::constants;
namespace seasonal = state::progression::seasonal_experience;

/**
 * Sums one definition's declared contribution to a single stat row.
 * @param definitionIndex Native item or plug index.
 * @param row Stat table row.
 * @return The declared value, or 0 when the definition declares none.
 */
[[nodiscard]] std::int32_t definition_total(std::uint16_t definitionIndex,
                                            std::uint8_t row) noexcept {
    details::Definition detail{};
    if (definitionIndex == details::kUnavailableItemIndex
        || !state::build_data::find_configured_item_detail(definitionIndex, detail)) {
        return 0;
    }
    std::int32_t total = 0;
    const std::size_t stats =
        detail.statCount < detail.stats.size() ? detail.statCount : detail.stats.size();
    for (std::size_t entry = 0; entry < stats; ++entry) {
        if (detail.stats[entry].row == row) {
            total += detail.stats[entry].value;
        }
    }
    return total;
}

/**
 * Sums one equipped item's own roll plus every plug its sockets hold.
 * An armour piece keeps only a token value on its own definition, so counting the definition alone
 * understates it by its whole roll.
 * @param equipped Effective plug lanes.
 * @param row Stat table row.
 * @return The item's total for that row.
 */
[[nodiscard]] std::int32_t item_total(const Equipped& equipped, std::uint8_t row) noexcept {
    std::int32_t total = definition_total(equipped.definitionIndex, row);
    for (std::size_t lane = 0; lane < equipped.laneCount; ++lane) {
        total += definition_total(resolve_effective_plug(equipped, lane), row);
    }
    return total;
}

/**
 * Collects every stat row one equipped item or its plugs declare.
 * @param equipped Effective plug lanes.
 * @param count Occupied entries, advanced per distinct row.
 * @return False for invalid stat rows
 * or insufficient storage.
 */
[[nodiscard]] bool
collect_rows(const Equipped& equipped, std::span<std::uint8_t> rows, std::size_t& count) noexcept {
    const std::size_t lanes = equipped.laneCount + 1;
    for (std::size_t source = 0; source < lanes; ++source) {
        const std::uint16_t definitionIndex =
            source == 0 ? equipped.definitionIndex : resolve_effective_plug(equipped, source - 1);
        details::Definition detail{};
        if (definitionIndex == details::kUnavailableItemIndex
            || !state::build_data::find_configured_item_detail(definitionIndex, detail)) {
            continue;
        }
        const std::size_t stats =
            detail.statCount < detail.stats.size() ? detail.statCount : detail.stats.size();
        for (std::size_t entry = 0; entry < stats; ++entry) {
            const std::uint8_t row = detail.stats[entry].row;
            if (row == details::kEmptyStatRow) {
                continue;
            }
            if (row >= constants::kStatRowCount) {
                return false;
            }
            if (std::find(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(count), row)
                == rows.begin() + static_cast<std::ptrdiff_t>(count)) {
                if (count >= rows.size()) {
                    return false;
                }
                rows[count++] = row;
            }
        }
    }
    return true;
}

/**
 * Appends one row to a stat table.
 * The client writes only rows that contribute, so a row worth 0 is omitted rather than written as
 * a live key asserting the stat is exactly 0.
 * @param row Stat table row.
 * @param value Signed total.
 * @param count Occupied rows, advanced when the row is written.
 */
void append(std::uint8_t row,
            std::int32_t value,
            std::array<layout::StatRow, layout::kStatRowCapacity>& table,
            std::size_t& count) noexcept {
    if (value <= 0 || count >= table.size()) {
        return;
    }
    table[count].key = static_cast<std::int8_t>(row);
    table[count].value = value;
    ++count;
}

/** Writes item Power and definition stats, rejecting invalid rows or insufficient space. */
[[nodiscard]] bool
apply_weapon_table(const Equipped& equipped,
                   std::uint8_t powerRow,
                   std::int32_t power,
                   std::array<layout::StatRow, layout::kStatRowCapacity>& table) noexcept {
    std::array<std::uint8_t, constants::kStatRowCount> rows{powerRow};
    std::size_t rowCount = 1;
    if (!collect_rows(equipped, rows, rowCount)) {
        return false;
    }
    std::sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rowCount));
    std::array<layout::StatRow, layout::kStatRowCapacity> staged{};
    std::size_t written = 0;
    for (std::size_t entry = 0; entry < rowCount; ++entry) {
        const std::uint8_t row = rows[entry];
        const std::int32_t value = row == powerRow ? power : item_total(equipped, row);
        if (value > 0 && written >= staged.size()) {
            return false;
        }
        append(row, value, staged, written);
    }
    table = staged;
    return true;
}

/** Diagnostic latch: the constants are a boot-time domain, so one line settles their absence. */
std::atomic<bool> g_reportedMissingConstants{};

/** Reports once that the installed investment constants are unavailable or invalid. */
void report_missing_constants() noexcept {
    if (g_reportedMissingConstants.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     "ev=char_stats stage=constants result=unavailable_or_invalid");
}

} // namespace

/** Fills the character stat table and the three per-weapon stat tables. */
bool apply_stats(const family4::loadout::ResolvedInstances& instances,
                 std::int32_t light,
                 layout::Appearance& appearance) noexcept {
    constants::InvestmentConstants named{};
    if (!state::build_data::find_investment_constants(named) || !constants::valid(named)) {
        // Publishing an absent or unusable weapon Power row silently selects the damage floor.
        report_missing_constants();
        return false;
    }
    std::array<std::uint8_t, constants::kCharacterStatRowCount> rows = named.characterStatRows;
    std::sort(rows.begin(), rows.end());

    std::size_t written = 0;
    append(named.lightStatRow, light, appearance.characterStats, written);
    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        details::Definition detail{};
        Equipped equipped{};
        if (!resolve_equipped(instances.items[index], detail, equipped)
            || detail.definitionHash != seasonal::kSeedOfSilverWingsHash
            || detail.statCount == 0
            || detail.stats.front().row == details::kEmptyStatRow) {
            continue;
        }
        append(detail.stats.front().row,
               seasonal::artifact_power_bonus(),
               appearance.characterStats,
               written);
        break;
    }
    for (const std::uint8_t row : rows) {
        std::int32_t total = 0;
        for (std::size_t index = 0; index < instances.itemCount; ++index) {
            details::Definition detail{};
            Equipped equipped{};
            if (resolve_equipped(instances.items[index], detail, equipped)) {
                total += item_total(equipped, row);
            }
        }
        append(row, total, appearance.characterStats, written);
    }

    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        const std::uint8_t slot = instances.items[index].equipmentSlot;
        const auto weapon = static_cast<std::size_t>(slot - kFirstWeaponSlot);
        if (slot < kFirstWeaponSlot || weapon >= appearance.weaponStats.size()) {
            continue;
        }
        details::Definition detail{};
        Equipped equipped{};
        std::int32_t power = 0;
        if (!resolve_equipped(instances.items[index], detail, equipped)
            || !state::equipment::light::item_power(instances.items[index].instance.level, power)
            || !apply_weapon_table(
                equipped, named.weaponPowerStatRow, power, appearance.weaponStats[weapon])) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::middleware::datagen::character_record::appearance
