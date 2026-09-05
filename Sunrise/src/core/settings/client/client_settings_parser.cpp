#include "../parser.h"

namespace sunrise::core::settings::parser {

/** Parses Client-owned configuration over deterministic defaults. */
bool Parser::client_settings(client::Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    client::Settings candidate = output;
    bool hasUserInterface = false;
    bool hasExternalServer = false;
    bool hasCustomBootflowTextures = false;
    bool hasFadeRelease = false;
    bool hasSkipProfileSetup = false;
    bool hasRegionPrivate = false;
    bool hasSkipOrbitCinematicWait = false;
    bool hasSuppressPeerRelay = false;
    bool hasPinReplicatedRecord = false;
    bool hasHoldSpawn = false;
    bool hasSpawnHoldMs = false;
    bool hasDumpGameImage = false;
    bool hasStockEntityPool = false;
    bool hasRestockDrained = false;
    if (consume('}')) {
        return true;
    }
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "ui") {
            if (hasUserInterface || !client_ui_settings(candidate.userInterface)) {
                return false;
            }
            hasUserInterface = true;
        } else if (key == "external_server") {
            if (hasExternalServer || !client_external_settings(candidate.externalServer)) {
                return false;
            }
            hasExternalServer = true;
        } else if (key == "custom_bootflow_textures") {
            if (hasCustomBootflowTextures || !boolean(candidate.customBootflowTextures)) {
                return false;
            }
            hasCustomBootflowTextures = true;
        } else if (key == "fade_release") {
            if (hasFadeRelease || !boolean(candidate.fadeRelease)) {
                return false;
            }
            hasFadeRelease = true;
        } else if (key == "skip_profile_setup") {
            if (hasSkipProfileSetup || !boolean(candidate.skipProfileSetup)) {
                return false;
            }
            hasSkipProfileSetup = true;
        } else if (key == "region_private") {
            if (hasRegionPrivate || !boolean(candidate.regionPrivate)) {
                return false;
            }
            hasRegionPrivate = true;
        } else if (key == "skip_orbit_cinematic_wait") {
            if (hasSkipOrbitCinematicWait || !boolean(candidate.skipOrbitCinematicWait)) {
                return false;
            }
            hasSkipOrbitCinematicWait = true;
        } else if (key == "suppress_peer_relay") {
            if (hasSuppressPeerRelay || !boolean(candidate.suppressPeerRelay)) {
                return false;
            }
            hasSuppressPeerRelay = true;
        } else if (key == "pin_replicated_record") {
            if (hasPinReplicatedRecord || !boolean(candidate.pinReplicatedRecord)) {
                return false;
            }
            hasPinReplicatedRecord = true;
        } else if (key == "hold_spawn") {
            if (hasHoldSpawn || !boolean(candidate.holdSpawn)) {
                return false;
            }
            hasHoldSpawn = true;
        } else if (key == "spawn_hold_ms") {
            std::uint64_t value = 0;
            if (hasSpawnHoldMs || !unsigned_integer(value) || value == 0
                || value > client::kMaximumSpawnHoldMs) {
                return false;
            }
            candidate.spawnHoldMs = value;
            hasSpawnHoldMs = true;
        } else if (key == "dump_game_image") {
            if (hasDumpGameImage || !boolean(candidate.dumpGameImage)) {
                return false;
            }
            hasDumpGameImage = true;
        } else if (key == "stock_entity_pool") {
            if (hasStockEntityPool || !boolean(candidate.stockEntityPool)) {
                return false;
            }
            hasStockEntityPool = true;
        } else if (key == "restock_drained_entity_pool") {
            if (hasRestockDrained || !boolean(candidate.restockDrainedEntityPool)) {
                return false;
            }
            hasRestockDrained = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            output = candidate;
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace sunrise::core::settings::parser
