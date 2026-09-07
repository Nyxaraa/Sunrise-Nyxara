#pragma once

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::server::ui::activity_host::scriptable_browser {

/** Requests current package-derived world data for one selected activity. */
void prepare(const server::activity::host::InstanceSnapshot* instance, bool force = false) noexcept;

/** Draws named world objects: every slot with a position or a shape. */
void draw_objects(const server::activity::host::InstanceSnapshot* instance) noexcept;

/** Draws type-23 device slots and the set-channel action for the highlighted row. */
void draw_devices(const server::activity::host::InstanceSnapshot* instance) noexcept;

/** Draws type-60 trigger volumes and their authored prism edges. */
void draw_triggers(const server::activity::host::InstanceSnapshot* instance) noexcept;

/** Draws package positions that no slot claims. */
void draw_positions(const server::activity::host::InstanceSnapshot* instance) noexcept;

} // namespace sunrise::server::ui::activity_host::scriptable_browser
