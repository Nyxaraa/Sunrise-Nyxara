#pragma once

#include "../../../state/activity/definition.h"
#include "../../activity/host_runtime.h"

namespace sunrise::server::ui::activity_host::incident_editor {

/** Draws named incident status and read-only metadata for one activity generation. */
void draw(const state::activity::SessionBinding& binding,
          const server::activity::host::InstanceSnapshot& instance,
          const server::activity::host::DiagnosticsSnapshot& snapshot) noexcept;

} // namespace sunrise::server::ui::activity_host::incident_editor
