#pragma once

#include "mission_script_vm.h"

namespace sunrise::server::activity::mission::message_catalog {

/** Installs only read-only activity-message resolvers on an existing SDK bridge. */
void attach(lua_vm::DefinitionApi& output) noexcept;

} // namespace sunrise::server::activity::mission::message_catalog
