#pragma once

#include <cstdint>

#include "../../ui/runtime/settings.h"
#include "external/definition.h"

namespace sunrise::core::settings::client {

/** A load this long has stopped making progress, so the spawn stops waiting for it. */
inline constexpr std::uint64_t kDefaultSpawnHoldMs = 30'000;
/** A load past this is a hang, not a slow machine, and holding the spawn would never end. */
inline constexpr std::uint64_t kMaximumSpawnHoldMs = 600'000;

/** Read-only Client settings parsed by Core. */
struct Settings {
    /** In-game UI visibility and input policy. */
    ui::runtime::Settings userInterface;
    /** Points the Client at a server outside this process. Off answers everything in process. */
    external::Settings externalServer;
    /** Replaces stock bootflow textures that have matching DDS assets embedded in Sunrise. */
    bool customBootflowTextures{true};
    /**
     * Releases the world-transition fade channel at the in-world step.
     * The client only releases it on the player spawn, so this covers a spawn that never runs
     * and leaves the world black. On by default.
     */
    bool fadeRelease{true};
    /**
     * Skips the one-time profile setup bootflow screens.
     * Off by default: the server-authored account completion byte should drive normal behavior.
     * Keep this only as a fallback for incomplete profile-state implementations.
     */
    bool skipProfileSetup{false};
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
     * Forces the peer channel to connect directly instead of through a NAT relay.
     * The stock client always relays the gameplay peer channel, which cannot complete against a
     * loopback host with no relay server. On by default; a client stand-in for the peer relay.
     */
    bool suppressPeerRelay{true};
    /**
     * Pins the participation record to the replicated snapshot at `comp + 496`.
     * Off, the record is the local one at `comp + 1256`, whose spawn-gate byte no wire field
     * reaches.
     */
    bool pinReplicatedRecord{true};
    /**
     * Runs the player spawn after the world-transition fade is armed.
     * A spawn before the arm releases nothing, so the screen stays black. Settable because it is
     * the only thing that can turn an allowed spawn into a refusal.
     */
    bool holdSpawn{true};
    /** How long the spawn waits for a load. `hold_spawn` decides whether it waits at all. */
    std::uint64_t spawnHoldMs{kDefaultSpawnHoldMs};
    /**
     * Writes the game's decrypted mapped image to `Sunrise\dumps` during activation.
     * The packed executable on disk cannot be disassembled, so this is the only way to read the
     * code that decodes the activity wire format. Off by default: the file is the whole image and
     * writing it costs a second or two of every boot that enables it.
     */
    bool dumpGameImage{false};
    /**
     * Stock the client's entity free-slot bitmap when it is left entirely unstocked.
     * The client fills that bitmap itself only when a role global reads zero; here it reads 3, so
     * the fill never runs and every entity creation fails from the first frame. On, Sunrise writes
     * the same bytes the client would have. Off restores the previous behaviour with no rebuild.
     */
    bool stockEntityPool{true};
    /**
     * Also refill the entity pool once it has drained, not only when it was never stocked.
     * The pool empties from 7935 free to zero inside a minute, and a drained pool makes an
     * encounter bubble kick to orbit again. Refilling anyway gets past that, but it re-frees
     * indices that are still owned, so one index can reach two entities — that crashed a respawn.
     * Off by default: the kick is recoverable, the corruption is not.
     */
    bool restockDrainedEntityPool{false};
};

} // namespace sunrise::core::settings::client
