#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
enum class OrbitAction { none, select, cleanup, orbitSetup, canceled, timedOut };
enum class OrbitStage { idle, completion, selection, leaving, finished };
struct OrbitObservation {
    bool exactOwner{}, emberSelected{}, orbitSelected{}, complete{}, ready{};
    int region{-1}, step{-1}, pendingStep{-1};
    bool sameWorld{};
};
struct OrbitReturn {
    OrbitStage stage{OrbitStage::idle};
    std::uint64_t began{};
    void arm(std::uint64_t now) { stage=OrbitStage::completion; began=now; }
    bool active() const { return stage!=OrbitStage::idle && stage!=OrbitStage::finished; }
    OrbitAction observe(std::uint64_t now, const OrbitObservation& s) {
        if (!active()) return OrbitAction::none;
        auto end=[this](OrbitAction a) { stage=OrbitStage::finished; return a; };
        if (now-began>90000) return end(OrbitAction::timedOut);
        if (stage==OrbitStage::completion) {
            if (!s.exactOwner || !s.emberSelected || s.region!=0 || s.step!=38)
                return end(OrbitAction::canceled);
            // Return on the first ready frame after completion, with no banner delay.
            if (!s.complete || !s.ready || s.pendingStep!=38) return OrbitAction::none;
            stage=OrbitStage::selection;
            return OrbitAction::select;
        }
        if (s.step==29 && s.orbitSelected) return end(OrbitAction::orbitSetup);
        if (stage==OrbitStage::selection) {
            // Native selection/commit can initiate cleanup itself. Never override it.
            if (s.step!=38) { stage=OrbitStage::leaving; return OrbitAction::none; }
            if (!s.sameWorld || (!s.emberSelected && !s.orbitSelected))
                return end(OrbitAction::canceled);
            if (s.orbitSelected && s.ready && s.pendingStep==38) {
                stage=OrbitStage::leaving;
                return OrbitAction::cleanup;
            }
        } else if (s.step==38 && !s.sameWorld) return end(OrbitAction::canceled);
        // Cleanup can temporarily have neither an ActivityClient nor a selected activity.
        // Keep observing that native transition; absence is not an arrival or a failure.
        return OrbitAction::none;
    }
};
}
