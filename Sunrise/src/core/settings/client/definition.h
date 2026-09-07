#pragma once

#include "../../ui/runtime/settings.h"
#include "external/definition.h"

namespace sunrise::core::settings::client {

/** Read-only Client settings parsed by Core. */
struct Settings {
    /** In-game UI visibility and input policy. */
    ui::runtime::Settings userInterface;
    /** Points the Client at a server outside this process. Off answers everything in process. */
    external::Settings externalServer;
    /** Replaces stock bootflow textures that have matching DDS assets embedded in Sunrise. */
    bool customBootflowTextures{true};
    /** Moves the four Arrivals leg mods into the leg plug set, so the leg mod menu lists them. */
    bool socketMenuRouting{false};
    /**
     * Clears the visibility gates on the loaded lore presentation nodes.
     * On by default; a client stand-in until the unlock banks carry every gate the nodes read.
     */
    bool revealLoreBooks{true};
    /**
     * Reports a public region as private to the region transition.
     * On, a public region loads solo. Off, it waits for a public activity host, which is the
     * route to the citizen join. A forced destination loads solo either way.
     */
    bool regionPrivate{false};
    /**
     * Answers the orbit destination hold as released without calling the game's predicate.
     * The predicate waits for an armed destination or a starting cinematic, so skipping it
     * suppresses the orbit-side entry cinematic.
     */
    bool skipOrbitCinematicWait{false};
    /**
     * Pins the participation record to the replicated snapshot at `comp + 496`.
     * The msg-5 spawn hold reaches no other record.
     */
    bool pinReplicatedRecord{true};
};

} // namespace sunrise::core::settings::client
