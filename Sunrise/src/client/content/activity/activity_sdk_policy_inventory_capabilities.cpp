#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "activity_sdk_policy_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal {

/**
 * Emits one compiled host surface using its implemented adapter gate set.
 * @param model The host declaration and sorted subject index.
 * @return True when the capability, gates, and optional refusal fit their bounds.
 */
bool Builder::build_host_capability(const HostModel& model) {
    const HostSurface& input = *model.surface;
    const bool squad = input.operation == "squad.place";
    const bool routed = input.operation == "scene.activate" || input.operation == "slot.apply_auth"
                        || input.operation == "slot.set_channel";
    const bool implemented = squad || routed;
    Capability capability{};
    std::string capabilityId{};
    std::uint32_t capabilityIndex = 0;
    if (!begin_capability(input.id,
                          input.operation,
                          {},
                          format::SubjectKind::hostApi,
                          model.subjectIndex,
                          implemented ? format::kExposureMask : format::kInspectExposure,
                          0,
                          capability,
                          capabilityId,
                          capabilityIndex)) {
        return false;
    }
    const std::size_t firstGate = output_.gates.size();
    FailureReasons failures{};
    if (squad) {
        if (!add_gate("authored_mapping", true, {}, {}, failures)
            || !add_gate("host_role", true, {}, {}, failures)
            || !add_gate("route", true, {}, {}, failures)
            || !add_gate("runtime_adapter", true, {}, {}, failures)) {
            return false;
        }
    } else if (routed) {
        if (!add_gate("authored_mapping", true, {}, {}, failures)
            || !add_gate("meaning", true, {}, {}, failures)
            || !add_gate("panel_adapter", true, {}, {}, failures)
            || !add_gate("reader", true, {}, {}, failures)
            || !add_gate("route_contract", true, {}, {}, failures)
            || !add_gate("runtime_adapter", true, {}, {}, failures)
            || !add_gate("schema_encode", true, {}, {}, failures)) {
            return false;
        }
    } else if (!add_gate("host_role",
                         false,
                         "unsupported_host_role",
                         "An authoritative activity host",
                         failures)
               || !add_gate("live_effect",
                            false,
                            "live_effect_unverified",
                            "One visible end-to-end effect",
                            failures)
               || !add_gate("meaning",
                            input.operation == "sleep_until",
                            "meaning_unverified",
                            "A verified retail action mapping",
                            failures)
               || !add_gate("runtime_adapter",
                            false,
                            "runtime_adapter_missing",
                            "The script VM adapter",
                            failures)) {
        return false;
    }
    const std::size_t firstRefusal = output_.refusals.size();
    if (!implemented && !add_refusal(capabilityId, kScript, kRefused, failures, capabilityIndex)) {
        return false;
    }
    return finish_capability(capability, firstGate, firstRefusal);
}

/** @return True after host subjects and operations are emitted in byte-sorted order. */
bool Builder::build_host_capabilities() {
    std::vector<std::string_view> subjects{};
    subjects.reserve(inputs_.hostSurfaces.size());
    for (const HostSurface& row : inputs_.hostSurfaces) {
        subjects.push_back(row.id);
    }
    std::sort(subjects.begin(), subjects.end(), byte_less);
    subjects.erase(std::unique(subjects.begin(), subjects.end()), subjects.end());
    if (subjects.size() > kMaximumHostSurfaceCount) {
        return false;
    }
    output_.hostSubjects.resize(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        if (!intern(subjects[index], output_.hostSubjects[index].id)) {
            return false;
        }
    }

    std::vector<HostModel> models{};
    models.reserve(inputs_.hostSurfaces.size());
    for (const HostSurface& row : inputs_.hostSurfaces) {
        const auto found = std::lower_bound(subjects.begin(), subjects.end(), row.id, byte_less);
        if (found == subjects.end() || *found != row.id) {
            return false;
        }
        models.push_back({&row, static_cast<std::uint32_t>(found - subjects.begin())});
    }
    std::sort(
        models.begin(), models.end(), [](const HostModel& first, const HostModel& second) noexcept {
            if (first.subjectIndex != second.subjectIndex) {
                return first.subjectIndex < second.subjectIndex;
            }
            if (first.surface->operation != second.surface->operation) {
                return byte_less(first.surface->operation, second.surface->operation);
            }
            return byte_less(first.surface->id, second.surface->id);
        });

    std::size_t nextModel = 0;
    for (std::size_t subjectIndex = 0; subjectIndex < subjects.size(); ++subjectIndex) {
        const std::size_t firstCapability = output_.capabilities.size();
        while (nextModel < models.size() && models[nextModel].subjectIndex == subjectIndex) {
            if (!build_host_capability(models[nextModel])) {
                return false;
            }
            ++nextModel;
        }
        if (!make_range(firstCapability,
                        output_.capabilities.size() - firstCapability,
                        output_.hostSubjects[subjectIndex].capabilities)) {
            return false;
        }
    }
    return nextModel == models.size();
}

} // namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal
