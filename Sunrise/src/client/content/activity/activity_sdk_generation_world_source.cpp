#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "activity_sdk_generation_worker_internal.h"

namespace sunrise::client::content::activity::sdk_generation::worker_internal {
namespace {

/** @return One selected hash name, or an empty view when the name is unresolved. */
[[nodiscard]] std::string_view selected_name(const catalog::Snapshot& snapshot,
                                             std::uint32_t row) noexcept {
    if (row >= snapshot.names.size()) {
        return {};
    }
    const catalog::Name& name = snapshot.names[row];
    if (name.selectedCandidate >= snapshot.nameCandidates.size()) {
        return {};
    }
    const catalog::NameCandidate& candidate = snapshot.nameCandidates[name.selectedCandidate];
    return {candidate.value.data(), candidate.length};
}

/** @return One selected package tag name, or an empty view when it is unresolved. */
[[nodiscard]] std::string_view selected_tag_name(const catalog::Snapshot& snapshot,
                                                 std::uint32_t row) noexcept {
    if (row >= snapshot.tagNames.size()) {
        return {};
    }
    const catalog::TagName& name = snapshot.tagNames[row];
    if (name.selectedCandidate >= snapshot.nameCandidates.size()) {
        return {};
    }
    const catalog::NameCandidate& candidate = snapshot.nameCandidates[name.selectedCandidate];
    return {candidate.value.data(), candidate.length};
}

/** Appends a deterministic Lua string literal. */
void append_lua_string(std::string& output, std::string_view value) {
    output.push_back('"');
    // Escaped bytes are emitted as uppercase hex.
    constexpr char digits[] = "0123456789ABCDEF";
    for (const unsigned char byte : value) {
        if (byte == '\\' || byte == '"') {
            output.push_back('\\');
            output.push_back(static_cast<char>(byte));
        } else if (byte == '\n') {
            output.append("\\n");
        } else if (byte < 0x20U || byte == 0x7FU) {
            output.append("\\x");
            output.push_back(digits[byte >> 4U]);
            output.push_back(digits[byte & 0xFU]);
        } else {
            output.push_back(static_cast<char>(byte));
        }
    }
    output.push_back('"');
}

void append_format(std::string& output, const char* format, auto... values) {
    std::array<char, 512> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), format, values...);
    if (length > 0) {
        output.append(buffer.data(),
                      (std::min)(static_cast<std::size_t>(length), buffer.size() - 1));
    }
}

/** Converts an extracted trigger name into a declaration key. */
[[nodiscard]] std::string lua_constant(std::string_view value) {
    std::string output;
    bool separator = false;
    for (const unsigned char byte : value) {
        if (std::isalnum(byte) != 0) {
            if (separator && !output.empty()) {
                output.push_back('_');
            }
            output.push_back(static_cast<char>(std::toupper(byte)));
            separator = false;
        } else {
            separator = true;
        }
    }
    if (output.empty()) {
        output = "UNNAMED";
    }
    if (std::isdigit(static_cast<unsigned char>(output.front())) != 0) {
        output.insert(output.begin(), '_');
    }
    return output;
}

} // namespace

/** Emits transparent positional trigger declarations for one mission module. */
bool build_world_source(const catalog::Snapshot& snapshot,
                        lua_artifacts::ScenarioWorldSource& result) {
    result = {};
    result.scenarioTag = snapshot.scenarioTag;
    std::string source = "\n---@type SunriseTriggerVolume[]\n"
                         "mission.trigger_volumes = {\n";
    std::vector<std::pair<std::string, std::uint32_t>> constants;
    std::vector<std::string_view> names;
    std::uint32_t emitted = 0;
    for (const catalog::TriggerVolumeOwner& owner : snapshot.triggerVolumeOwners) {
        if (owner.tableRow >= snapshot.triggerVolumeTables.size()
            || owner.objectRow >= snapshot.objects.size()) {
            continue;
        }
        const catalog::TriggerVolumeTable& table = snapshot.triggerVolumeTables[owner.tableRow];
        const std::size_t first = table.firstInstance;
        const std::size_t count = table.instanceCount;
        if (first > snapshot.triggerVolumeInstances.size()
            || count > snapshot.triggerVolumeInstances.size() - first) {
            continue;
        }
        names.clear();
        const std::size_t incomingFirst = owner.firstIncomingReference;
        const std::size_t incomingCount = owner.incomingReferenceCount;
        if (incomingFirst <= snapshot.triggerVolumeIncomingReferences.size()
            && incomingCount <= snapshot.triggerVolumeIncomingReferences.size() - incomingFirst) {
            for (std::size_t offset = 0; offset < incomingCount; ++offset) {
                const auto& incoming =
                    snapshot.triggerVolumeIncomingReferences[incomingFirst + offset];
                if (incoming.sourceSlotRow < snapshot.slots.size()) {
                    const std::string_view name =
                        selected_name(snapshot, snapshot.slots[incoming.sourceSlotRow].nameRow);
                    if (!name.empty()
                        && std::find(names.begin(), names.end(), name) == names.end()) {
                        names.push_back(name);
                    }
                }
            }
        }
        if (names.empty() && owner.slotRow < snapshot.slots.size()) {
            const std::string_view name =
                selected_name(snapshot, snapshot.slots[owner.slotRow].nameRow);
            if (!name.empty()) {
                names.push_back(name);
            }
        }
        if (names.empty()) {
            const catalog::Object& object = snapshot.objects[owner.objectRow];
            // Three fallback name rows are tried in order.
            constexpr std::size_t kFallbackCount = 3;
            const std::array<std::uint32_t, kFallbackCount> fallbackRows{
                table.configNameRow, object.objectNameRow, object.registryNameRow};
            for (const std::uint32_t row : fallbackRows) {
                const std::string_view name = selected_tag_name(snapshot, row);
                if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
        for (std::size_t offset = 0; offset < count; ++offset) {
            const catalog::TriggerVolumeInstance& instance =
                snapshot.triggerVolumeInstances[first + offset];
            if (!instance.complete) {
                continue;
            }
            std::vector<std::string_view> instanceNames = names;
            if (instanceNames.empty()) {
                const std::array<std::uint32_t, 2> fallbackRows{instance.classDefinitionNameRow,
                                                                instance.shapeResourceNameRow};
                for (const std::uint32_t row : fallbackRows) {
                    const std::string_view name = selected_tag_name(snapshot, row);
                    if (!name.empty()
                        && std::find(instanceNames.begin(), instanceNames.end(), name)
                               == instanceNames.end()) {
                        instanceNames.push_back(name);
                    }
                }
            }
            ++emitted;
            source.append("    { names = {");
            for (const std::string_view name : instanceNames) {
                append_lua_string(source, name);
                source.append(", ");
            }
            append_format(source,
                          "}, registry_key = 0x%08X, slot_type = %u, slot_index = %u, "
                          "position = { x = %.9g, y = %.9g, z = %.9g }, "
                          "minimum = { x = %.9g, y = %.9g, z = %.9g }, "
                          "maximum = { x = %.9g, y = %.9g, z = %.9g }, "
                          "shape_tag = 0x%08X, shape_index = %u, active = %u },\n",
                          table.registryKey,
                          static_cast<unsigned>(table.slotType),
                          static_cast<unsigned>(table.slotIndex),
                          static_cast<double>(instance.position[0]),
                          static_cast<double>(instance.position[1]),
                          static_cast<double>(instance.position[2]),
                          static_cast<double>(instance.minimum[0]),
                          static_cast<double>(instance.minimum[1]),
                          static_cast<double>(instance.minimum[2]),
                          static_cast<double>(instance.maximum[0]),
                          static_cast<double>(instance.maximum[1]),
                          static_cast<double>(instance.maximum[2]),
                          instance.shapeResourceTag,
                          instance.shapeIndex,
                          static_cast<unsigned>(instance.active));
            for (const std::string_view name : instanceNames) {
                std::string key = lua_constant(name);
                const std::string base = key;
                std::uint32_t suffix = 1;
                while (std::any_of(constants.begin(), constants.end(), [&key](const auto& entry) {
                    return entry.first == key;
                })) {
                    key = base;
                    append_format(key, "_%u", suffix++);
                }
                constants.emplace_back(std::move(key), emitted);
            }
        }
    }
    source.append("}\nmission.TriggerVolume = {\n");
    for (const auto& [key, index] : constants) {
        source.append("    ");
        source.append(key);
        append_format(source, " = mission.trigger_volumes[%u],\n", index);
    }
    source.append("}\n");
    result.source = std::move(source);
    return true;
}

} // namespace sunrise::client::content::activity::sdk_generation::worker_internal
