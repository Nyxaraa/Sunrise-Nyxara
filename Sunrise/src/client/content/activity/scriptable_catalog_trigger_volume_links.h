#pragma once

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using TriggerVolumeLinkCancelCheck = bool (*)() noexcept;

/** Normalizes exact incoming type-31 TypedReferences for every type-60 trigger owner. */
[[nodiscard]] bool
append_trigger_volume_incoming_references(state::build_data::scriptables::Snapshot& output,
                                          TriggerVolumeLinkCancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
