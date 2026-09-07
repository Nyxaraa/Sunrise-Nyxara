#include "../collectibles/collectible_catalog.h"
#include "../items/item_catalog.h"
#include "../runtime.h"
#include "vendor_catalog.h"

namespace sunrise::state::build_data {
namespace {

/** Seasonal artifact vendor. Its sale rows are the five mod columns plus the reset row. */
constexpr std::uint32_t kArtifactVendorHash = 0xAC82564EU;

} // namespace

/** Lists the artifact's sale rows with the unlock each one buys. */
bool artifact_sale_rows(std::span<ArtifactSaleRow> output, std::size_t& count) noexcept {
    count = 0;
    vendors::Definition definition{};
    if (!vendor_catalog_ready() || !vendors::find(kArtifactVendorHash, definition)
        || definition.saleCount == 0 || output.size() < definition.saleCount) {
        return false;
    }
    for (std::size_t row = 0; row < definition.saleCount; ++row) {
        vendors::SaleRow sale{};
        items::Definition item{};
        if (!vendors::sale_row(definition, row, sale) || !items::find_index(sale.itemIndex, item)) {
            count = 0;
            return false;
        }
        ArtifactSaleRow& entry = output[row];
        entry = {};
        entry.itemHash = item.definitionHash;
        entry.itemIndex = sale.itemIndex;
        entry.categoryIndex = sale.categoryIndex;
        // The reset row sells no mod, so it owns no collectible and stays at its sentinels.
        std::uint16_t collectibleIndex = 0;
        collectibles::Definition collectible{};
        if (collectibles::find_granting(sale.itemIndex, collectibleIndex)
            && collectibles::find(collectibleIndex, collectible)) {
            entry.unlockFlagSlot = collectible.acquiredFlagSlot;
            entry.characterFlagIndex = collectible.acquiredFlagIndex;
        }
    }
    count = definition.saleCount;
    return true;
}

} // namespace sunrise::state::build_data
