#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"
#include "sobject_catalog.h"

namespace sunrise::state::build_data {

/** @return True when the whole incident-target table is published. */
bool sobject_definitions_ready() noexcept {
    return sobjects::count() != 0;
}

/** Publishes one complete incident-target table in one step. */
bool publish_sobject_definitions(std::span<const sobjects::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && sobjects::valid(definitions)
           && transaction.finish(sobjects::replace(definitions), sobjects::clear);
}

} // namespace sunrise::state::build_data
