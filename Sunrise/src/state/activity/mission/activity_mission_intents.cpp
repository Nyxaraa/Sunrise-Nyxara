#include <Windows.h>

#include <cstdint>

#include "../../runtime/storage/internal.h"
#include "internal.h"
#include "runtime.h"

namespace sunrise::state::activity::mission {
namespace {

/** Retires an exact durable head that never owns a Host output revision. */
[[nodiscard]] Status retire_unassigned_intent(const SessionBinding& binding,
                                              const ProgramKey& program,
                                              std::uint64_t expectedMissionRevision,
                                              std::uint64_t expectedIntentSequence,
                                              Snapshot& output) noexcept {
    output = {};
    if (!valid_program(program)) {
        return Status::invalidProgram;
    }
    if (expectedIntentSequence == kAbsentIntentSequence) {
        return Status::invalidTransition;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& activity = runtime::storage::g_state.activity;
    SessionRecord* const record = find_record(activity, binding);
    Status status = record == nullptr ? Status::invalidBinding : Status::ready;
    PendingIntent* pending = nullptr;
    if (status == Status::ready) {
        status = checked_head(
            *record, program, expectedMissionRevision, expectedIntentSequence, pending);
    }
    if (status == Status::ready && pending->hostOutputRevision != kAbsentHostOutputRevision) {
        status = Status::hostRevisionMismatch;
    }
    if (status == Status::ready) {
        MissionState candidate{};
        if (!copy_mission_state(record->mission, candidate)) {
            status = Status::outOfMemory;
        } else {
            candidate.pendingIntents.erase(candidate.pendingIntents.begin());
            if (!publish(activity, *record)) {
                status = Status::revisionExhausted;
            } else {
                record->mission = std::move(candidate);
            }
        }
    }
    if (status == Status::ready && !copy_snapshot(activity, binding, *record, output)) {
        status = Status::outOfMemory;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return status;
}

} // namespace

/** Assigns the exact next Host output revision to the durable head intent. */
Status assign_intent_output(const SessionBinding& binding,
                            const ProgramKey& program,
                            std::uint64_t expectedMissionRevision,
                            std::uint64_t expectedIntentSequence,
                            std::uint64_t hostOutputRevision,
                            Snapshot& output) noexcept {
    output = {};
    if (!valid_program(program)) {
        return Status::invalidProgram;
    }
    if (expectedIntentSequence == kAbsentIntentSequence
        || hostOutputRevision == kAbsentHostOutputRevision) {
        return Status::invalidTransition;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& activity = runtime::storage::g_state.activity;
    SessionRecord* const record = find_record(activity, binding);
    Status status = record == nullptr ? Status::invalidBinding : Status::ready;
    PendingIntent* pending = nullptr;
    if (status == Status::ready) {
        status = checked_head(
            *record, program, expectedMissionRevision, expectedIntentSequence, pending);
    }
    if (status == Status::ready && record->mission.faulted) {
        status = Status::invalidTransition;
    }
    if (status == Status::ready && pending->hostOutputRevision != kAbsentHostOutputRevision
        && pending->hostOutputRevision != hostOutputRevision) {
        status = Status::hostRevisionMismatch;
    }
    // A fresh assignment must leave one Activity State revision for its exact release or ack.
    if (status == Status::ready && pending->hostOutputRevision == kAbsentHostOutputRevision
        && activity.stateRevision >= kMaximumRevision - 1) {
        status = Status::revisionExhausted;
    }
    if (status == Status::ready && pending->hostOutputRevision == kAbsentHostOutputRevision) {
        MissionState candidate{};
        if (!copy_mission_state(record->mission, candidate)) {
            status = Status::outOfMemory;
        } else {
            candidate.pendingIntents[0].hostOutputRevision = hostOutputRevision;
            if (!publish(activity, *record)) {
                status = Status::revisionExhausted;
            } else {
                record->mission = std::move(candidate);
            }
        }
    }
    if (status == Status::ready && !copy_snapshot(activity, binding, *record, output)) {
        status = Status::outOfMemory;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return status;
}

/** Reads whether the exact durable head still owns one queued Host output revision. */
bool intent_output_assigned(const SessionBinding& binding,
                            std::uint64_t expectedIntentSequence,
                            std::uint64_t expectedHostOutputRevision) noexcept {
    if (expectedIntentSequence == kAbsentIntentSequence
        || expectedHostOutputRevision == kAbsentHostOutputRevision) {
        return false;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& activity = runtime::storage::g_state.activity;
    const SessionRecord* const record = find_record(activity, binding);
    const bool assigned =
        record != nullptr && record->mission.programBound && !record->mission.pendingIntents.empty()
        && record->mission.pendingIntents[0].sequence == expectedIntentSequence
        && record->mission.pendingIntents[0].hostOutputRevision == expectedHostOutputRevision;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return assigned;
}

/** Releases an unstaged Host output assignment while retaining the durable intent. */
Status release_intent_output(const SessionBinding& binding,
                             const ProgramKey& program,
                             std::uint64_t expectedMissionRevision,
                             std::uint64_t expectedIntentSequence,
                             std::uint64_t expectedHostOutputRevision,
                             Snapshot& output) noexcept {
    output = {};
    if (!valid_program(program)) {
        return Status::invalidProgram;
    }
    if (expectedIntentSequence == kAbsentIntentSequence
        || expectedHostOutputRevision == kAbsentHostOutputRevision) {
        return Status::invalidTransition;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& activity = runtime::storage::g_state.activity;
    SessionRecord* const record = find_record(activity, binding);
    Status status = record == nullptr ? Status::invalidBinding : Status::ready;
    PendingIntent* pending = nullptr;
    if (status == Status::ready) {
        status = checked_head(
            *record, program, expectedMissionRevision, expectedIntentSequence, pending);
    }
    if (status == Status::ready && pending->hostOutputRevision != expectedHostOutputRevision) {
        status = Status::hostRevisionMismatch;
    }
    if (status == Status::ready) {
        MissionState candidate{};
        if (!copy_mission_state(record->mission, candidate)) {
            status = Status::outOfMemory;
        } else {
            candidate.pendingIntents[0].hostOutputRevision = kAbsentHostOutputRevision;
            if (!publish(activity, *record)) {
                status = Status::revisionExhausted;
            } else {
                record->mission = std::move(candidate);
            }
        }
    }
    if (status == Status::ready && !copy_snapshot(activity, binding, *record, output)) {
        status = Status::outOfMemory;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return status;
}

/** Removes the durable head only after its exact Host output revision was staged. */
Status acknowledge_intent_output(const SessionBinding& binding,
                                 const ProgramKey& program,
                                 std::uint64_t expectedMissionRevision,
                                 std::uint64_t expectedIntentSequence,
                                 std::uint64_t expectedHostOutputRevision,
                                 Snapshot& output) noexcept {
    output = {};
    if (!valid_program(program)) {
        return Status::invalidProgram;
    }
    if (expectedIntentSequence == kAbsentIntentSequence
        || expectedHostOutputRevision == kAbsentHostOutputRevision) {
        return Status::invalidTransition;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& activity = runtime::storage::g_state.activity;
    SessionRecord* const record = find_record(activity, binding);
    Status status = record == nullptr ? Status::invalidBinding : Status::ready;
    PendingIntent* pending = nullptr;
    if (status == Status::ready) {
        status = checked_head(
            *record, program, expectedMissionRevision, expectedIntentSequence, pending);
    }
    if (status == Status::ready && pending->hostOutputRevision != expectedHostOutputRevision) {
        status = Status::hostRevisionMismatch;
    }
    if (status == Status::ready) {
        MissionState candidate{};
        if (!copy_mission_state(record->mission, candidate)) {
            status = Status::outOfMemory;
        } else {
            candidate.pendingIntents.erase(candidate.pendingIntents.begin());
            if (!publish(activity, *record)) {
                status = Status::revisionExhausted;
            } else {
                record->mission = std::move(candidate);
            }
        }
    }
    if (status == Status::ready && !copy_snapshot(activity, binding, *record, output)) {
        status = Status::outOfMemory;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return status;
}

/** Removes one successfully applied local effect that owns no Host output revision. */
Status acknowledge_intent(const SessionBinding& binding,
                          const ProgramKey& program,
                          std::uint64_t expectedMissionRevision,
                          std::uint64_t expectedIntentSequence,
                          Snapshot& output) noexcept {
    return retire_unassigned_intent(
        binding, program, expectedMissionRevision, expectedIntentSequence, output);
}

/** Drops the durable head after a request was refused. */
Status discard_intent(const SessionBinding& binding,
                      const ProgramKey& program,
                      std::uint64_t expectedMissionRevision,
                      std::uint64_t expectedIntentSequence,
                      Snapshot& output) noexcept {
    return retire_unassigned_intent(
        binding, program, expectedMissionRevision, expectedIntentSequence, output);
}
} // namespace sunrise::state::activity::mission
