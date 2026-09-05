/**
 * The current-status overlay. Each line says where the player is, and each has its own switch.
 * Every value comes from published State.
 */

#include "ui_hud_status_overlay.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <imgui.h>
#include <string_view>

#include "../../../../client/hooks/bootflow/bootflow_hook_lifecycle.h"
#include "../../../../client/player/player_position.h"
#include "../../../../middleware/content/packages/tables/region_reader.h"
#include "../../../../middleware/content/packages/tables/spawn_reader.h"
#include "../../../../server/bap/runtime.h"
#include "../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../logging/log.h"
#include "../overlay.h"

namespace sunrise::core::ui::hud::overlays::status {
namespace {

namespace activity = state::activity;
namespace layouts = state::build_data::scenarios;
namespace tables = middleware::content::packages::tables;

/** Room for the widest value line this overlay builds. */
constexpr std::size_t kValueCapacity = 96;
/** Widest label, which sets the value column for every row. */
constexpr char kWidestLabel[] = "Closest spawn";
/** Shown while no destination is loaded. */
constexpr char kOutOfWorld[] = "not in world";
/** Shown for a value the published State does not name. */
constexpr char kUnknown[] = "unknown";
/** Shown while the player's position has not been read yet. */
constexpr char kNoPosition[] = "waiting for a position";
/** Shown when every line of this overlay is switched off. */
constexpr char kNoLines[] = "every status line is off";
/** How long a closest-spawn result is kept before the bank is searched again. */
constexpr std::uint64_t kSpawnSearchIntervalMs = 250;

/** One value line, built once per frame. */
using Value = std::array<char, kValueCapacity>;

/** Everything the overlay draws, read in one pass so the lines cannot disagree. */
struct Status {
    Value activity{};
    Value bubble{};
    Value sliceSet{};
    Value spawn{};
    bool inWorld{};
};

/** The last point search. It is kept between frames because the bank is large. */
struct SpawnResult {
    Value text{};
    std::uint64_t searchedTick{};
    bool valid{};
};

SpawnResult g_spawn{};

/** @param value Text to store. @param output Receives it with a null. */
void assign(std::string_view value, Value& output) noexcept {
    output = {};
    (void)std::snprintf(
        output.data(), output.size(), "%.*s", static_cast<int>(value.size()), value.data());
}

/** Names one hash. @param storage Gets the found row. @return The name, or an empty view. */
[[nodiscard]] std::string_view resolve_name(std::uint32_t hash,
                                            state::build_data::hash_names::Name& storage) noexcept {
    if (!state::build_data::find_hash_name(hash, storage)) {
        return {};
    }
    return {storage.name.data(), storage.nameLength};
}

/** Names one spawn set. The packages name few, so the two the client defines are named here. */
[[nodiscard]] std::string_view
spawn_set_name(std::uint32_t hash, state::build_data::hash_names::Name& storage) noexcept {
    if (hash == tables::kDefaultSpawnNameHash) {
        return "default";
    }
    if (hash == tables::kUnnamedSpawnNameHash) {
        return "unnamed";
    }
    return resolve_name(hash, storage);
}

/** @param selection Committed destination. @return Its package name as a bounded view. */
[[nodiscard]] std::string_view
name_of(const activity::destination::DestinationSelection& selection) noexcept {
    return {reinterpret_cast<const char*>(selection.packageName.data()),
            selection.packageNameLength};
}

/** Fills the bubble line from the destination layout the region belongs to. */
void build_bubble(const layouts::Definition& layout,
                  std::int32_t region,
                  bool hostRegion,
                  Value& output) noexcept {
    const auto bubble = static_cast<std::size_t>(region) / tables::kSliceSetIndexFactor;
    if (region < 0 || bubble >= layout.bubbleCount) {
        assign(kUnknown, output);
        return;
    }
    state::build_data::hash_names::Name storage{};
    const std::string_view named = resolve_name(layout.bubbleHashes[bubble], storage);
    (void)std::snprintf(output.data(),
                        output.size(),
                        "%zu  0x%08X%s%.*s%s",
                        bubble,
                        layout.bubbleHashes[bubble],
                        named.empty() ? "" : "  ",
                        static_cast<int>(named.size()),
                        named.data(),
                        hostRegion ? "  host region" : "");
}

/** Fills the slice-set line, which is the region index and the state inside its bubble. */
void build_slice_set(std::int32_t region, bool hostRegion, Value& output) noexcept {
    if (region < 0) {
        assign(kUnknown, output);
        return;
    }
    const auto index = static_cast<std::uint32_t>(region);
    (void)std::snprintf(output.data(),
                        output.size(),
                        "%u  state %u%s",
                        index,
                        index % tables::kSliceSetIndexFactor,
                        hostRegion ? "  host region" : "");
}

/**
 * Finds the spawn nearest the player, at most once per interval.
 * The bank holds every installed point, so a search costs more than one frame should.
 * @param stem Map stem of the loaded destination. @param output Receives the kept line.
 */
void build_spawn(std::string_view stem, Value& output) noexcept {
    const client::player::position::Snapshot player = client::player::position::snapshot();
    const std::uint64_t now = GetTickCount64();
    if (!player.present || stem.empty()) {
        assign(kNoPosition, output);
        return;
    }
    if (g_spawn.valid && now - g_spawn.searchedTick < kSpawnSearchIntervalMs) {
        output = g_spawn.text;
        return;
    }
    state::build_data::spawn_sets::Point point{};
    float distance = 0.0F;
    g_spawn = {};
    g_spawn.searchedTick = now;
    if (!state::build_data::find_nearest_spawn_point(stem, player.position, point, distance)) {
        assign(kUnknown, output);
        return;
    }
    state::build_data::hash_names::Name storage{};
    const std::string_view named = spawn_set_name(point.nameHash, storage);
    (void)std::snprintf(g_spawn.text.data(),
                        g_spawn.text.size(),
                        "0x%08X%s%.*s  %.1f units",
                        point.nameHash,
                        named.empty() ? "" : "  ",
                        static_cast<int>(named.size()),
                        named.data(),
                        static_cast<double>(distance));
    g_spawn.valid = true;
    output = g_spawn.text;
}

/** @return Every line's text, read from published State in one pass. */
[[nodiscard]] Status read_status() noexcept {
    Status status{};
    const client::hooks::bootflow::CurrentSliceSet localSliceSet =
        client::hooks::bootflow::current_slice_set();
    server::bap::CurrentActivityLinkView link{};
    bool hasLink = server::bap::current_activity_link_view(
        localSliceSet.present ? localSliceSet.index : -1, link);
    // Public ActivityClients are replaced during every citizen-host handoff. The destination and
    // region are also committed in Activity State, so use the newest session that has actually
    // reported a region when no connection-scoped link survives the handoff.
    if (!hasLink) {
        const std::uint64_t sessionId = activity::membership::live_region_session(
            activity::kAbsentSessionId);
        activity::SessionBinding binding{};
        if (sessionId != activity::kAbsentSessionId
            && activity::snapshot_binding(sessionId, binding)) {
            link.binding = binding;
            link.effectiveRegion = activity::membership::player_region(sessionId);
            hasLink = true;
        }
    }
    // Keep one transition-level diagnostic. These values otherwise exist only inside the HUD, so
    // an `unknown` report cannot distinguish a missing local slice from invalid activity links.
    static std::int32_t lastSlice = -3;
    static std::size_t lastActive = static_cast<std::size_t>(-1);
    static std::size_t lastMatching = static_cast<std::size_t>(-1);
    static bool lastHasLink = false;
    const std::int32_t diagnosticSlice = localSliceSet.present
                                             ? localSliceSet.index
                                             : localSliceSet.available ? -1 : -2;
    if (diagnosticSlice != lastSlice || link.activeLinks != lastActive
        || link.matchingRegions != lastMatching || hasLink != lastHasLink) {
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=hud stage=activity_link result=%s slice=%d active=%zu "
                                          "matching=%zu selected=%d public=%u package=%.*s",
                                          hasLink ? "ok" : "missing",
                                          diagnosticSlice,
                                          link.activeLinks,
                                          link.matchingRegions,
                                          link.effectiveRegion,
                                          link.publicTarget ? 1U : 0U,
                                          static_cast<int>(link.binding.destination.packageNameLength),
                                          reinterpret_cast<const char*>(
                                              link.binding.destination.packageName.data()));
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
        lastSlice = diagnosticSlice;
        lastActive = link.activeLinks;
        lastMatching = link.matchingRegions;
        lastHasLink = hasLink;
    }
    // The client's own step, published every frame. The world phase only moves on the spawn gate,
    // which stops being polled once the player is in, so it stays `arrived` in orbit.
    status.inWorld = client::hooks::bootflow::in_world();
    if (!status.inWorld) {
        g_spawn = {};
        // The next destination has its own map, so a position from this one must not carry over.
        client::player::position::reset();
        return status;
    }
    if (!hasLink) {
        assign(kUnknown, status.activity);
        assign(kUnknown, status.bubble);
        assign(kUnknown, status.spawn);
        if (localSliceSet.present) {
            build_slice_set(localSliceSet.index, false, status.sliceSet);
        } else {
            assign(localSliceSet.available ? "switching" : kUnknown, status.sliceSet);
        }
        return status;
    }
    const activity::destination::DestinationSelection& selection = link.binding.destination;
    const std::string_view name = name_of(selection);
    assign(name.empty() ? std::string_view(kUnknown) : name, status.activity);

    if (localSliceSet.available && !localSliceSet.present) {
        assign("switching", status.bubble);
        assign("switching", status.sliceSet);
        assign(kUnknown, status.spawn);
        return status;
    }
    const bool hostRegion = !localSliceSet.available;
    const std::int32_t region = localSliceSet.present ? localSliceSet.index : link.effectiveRegion;
    layouts::Definition layout{};
    if (!state::build_data::find_scenario_layout(name, layout)) {
        assign(kUnknown, status.bubble);
        assign(kUnknown, status.spawn);
        build_slice_set(region, hostRegion, status.sliceSet);
        return status;
    }
    build_bubble(layout, region, hostRegion, status.bubble);
    build_slice_set(region, hostRegion, status.sliceSet);
    build_spawn({layout.spawnStem.data(), layout.spawnStemLength}, status.spawn);
    return status;
}

/** Draws one line as a label and a value. @param valueColumn Left edge of the value column. */
void draw_line(StatusLine line, const Value& value, float valueColumn) noexcept {
    if (!enabled(line)) {
        return;
    }
    ImGui::TextDisabled("%s", display_name(line));
    ImGui::SameLine(valueColumn);
    ImGui::TextUnformatted(value.data());
}

} // namespace

/** Draws the current-status lines inside the overlay window the stack has already started. */
void draw() noexcept {
    if (!enabled(StatusLine::activity) && !enabled(StatusLine::bubble)
        && !enabled(StatusLine::sliceSet) && !enabled(StatusLine::closestSpawn)) {
        // With every line off the overlay would draw an empty box and say nothing.
        ImGui::TextDisabled("%s", kNoLines);
        return;
    }
    const Status status = read_status();
    if (!status.inWorld) {
        // One line rather than the same stand-in on every row.
        ImGui::TextDisabled("%s", kOutOfWorld);
        return;
    }
    const float valueColumn =
        ImGui::CalcTextSize(kWidestLabel).x + (ImGui::GetStyle().ItemSpacing.x * 2.0F);
    draw_line(StatusLine::activity, status.activity, valueColumn);
    draw_line(StatusLine::bubble, status.bubble, valueColumn);
    draw_line(StatusLine::sliceSet, status.sliceSet, valueColumn);
    draw_line(StatusLine::closestSpawn, status.spawn, valueColumn);
}

} // namespace sunrise::core::ui::hud::overlays::status
