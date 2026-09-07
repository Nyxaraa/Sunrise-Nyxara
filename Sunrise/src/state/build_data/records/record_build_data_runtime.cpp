#include "../items/item_catalog.h"
#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"
#include "record_catalog.h"

namespace sunrise::state::build_data {

/** @return True when the whole native record table is published. */
bool record_definitions_ready() noexcept {
    return records::count() != 0;
}

/** Publishes one complete record catalog and its objective, interval and reward banks. */
bool publish_record_definitions(std::span<const records::Definition> definitions,
                                std::span<const records::Objective> objectives,
                                std::span<const records::Interval> intervals,
                                std::span<const records::Reward> rewards) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && records::valid(definitions, objectives, intervals, rewards)
           && transaction.finish(records::replace(definitions, objectives, intervals, rewards),
                                 records::clear);
}

/** Resolves the native record row an opcode-1801 claim names. */
bool find_record_definition(std::uint16_t definitionIndex,
                            records::Definition& definition) noexcept {
    return records::find(definitionIndex, definition);
}

/** Reads the items one record grants when it is claimed. */
bool find_record_rewards(std::uint16_t definitionIndex,
                         std::array<records::Reward, records::kRewardPerRecordCapacity>& rewards,
                         std::size_t& rewardCount) noexcept {
    rewards = {};
    rewardCount = 0;
    records::Definition definition{};
    std::array<records::Reward, records::kRewardPerRecordCapacity> published{};
    std::size_t publishedCount = 0;
    if (!records::find(definitionIndex, definition)
        || !records::rewards(definition, published, publishedCount)) {
        return false;
    }
    // A reward naming an item this build does not install is dropped, never granted as a zero.
    for (std::size_t row = 0; row < publishedCount; ++row) {
        items::Definition item{};
        if (items::find_index(published[row].itemIndex, item)) {
            rewards[rewardCount++] = published[row];
        }
    }
    return true;
}

} // namespace sunrise::state::build_data
