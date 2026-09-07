#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"
#include "bounty_catalog.h"

namespace sunrise::state::build_data {

/** @return True when the repeatable bounty table is published. */
bool repeatable_bounties_ready() noexcept {
    return bounties::count() != 0;
}

/** Publishes every repeatable bounty and the item-type its pool is keyed by. */
bool publish_repeatable_bounties(std::span<const bounties::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active()
           && transaction.finish(bounties::replace(definitions), bounties::clear);
}

/** Lists the item indices one repeatable vendor category rolls from. */
bool repeatable_bounty_pool(const bounties::ItemType& itemType,
                            std::span<std::uint16_t> output,
                            std::size_t& count) noexcept {
    count = 0;
    return repeatable_bounties_ready() && bounties::pool(itemType, output, count);
}

} // namespace sunrise::state::build_data
