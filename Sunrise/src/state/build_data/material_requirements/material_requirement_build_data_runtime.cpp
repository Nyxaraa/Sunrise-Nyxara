#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"
#include "material_requirement_catalog.h"

namespace sunrise::state::build_data {
namespace {

[[nodiscard]] bool valid_material_publication(
    std::span<const material_requirements::Definition> definitions) noexcept {
    // Shape only. The extractor bounded every material index, and the cache validator repeats
    // the cross-domain check before anything is persisted.
    return item_definitions_ready() && material_requirements::valid(definitions);
}

} // namespace

bool material_requirement_sets_ready() noexcept {
    return material_requirements::count() != 0;
}

bool publish_material_requirement_sets(
    std::span<const material_requirements::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && valid_material_publication(definitions)
           && transaction.finish(material_requirements::replace(definitions),
                                 material_requirements::clear);
}

bool find_material_requirement_set(std::uint16_t requirementSetIndex,
                                   material_requirements::Definition& definition) noexcept {
    definition = {};
    return material_requirement_sets_ready()
           && material_requirements::find(requirementSetIndex, definition)
           && definition.requirementSetIndex == requirementSetIndex;
}

} // namespace sunrise::state::build_data
