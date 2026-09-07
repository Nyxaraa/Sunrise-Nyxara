#include "activity_sdk_behavior_inventory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/reader/parallel.h"

namespace sunrise::client::content::activity::sdk_generation::behavior_inventory {
namespace {

namespace reader = middleware::content::packages::reader;
namespace parallel = middleware::content::packages::reader::parallel;

// Package class ids of the behavior graph nodes this pass reads.
constexpr std::uint32_t kRootClass = 0x8080941EU;
constexpr std::uint32_t kStorageChannelClass = 0x80802A6FU;
constexpr std::uint32_t kConditionClass = 0x80804D7DU;
constexpr std::uint32_t kFilterClass = 0x80804E3DU;
constexpr std::uint32_t kTargetClass = 0x80804E10U;
constexpr std::uint32_t kConditionGroupClass = 0x80809310U;
// Extraction bounds; one malformed blob must not exhaust memory.
constexpr std::uint32_t kMaximumRows = 1U << 20U;
constexpr std::uint32_t kMaximumDepth = 256U;
// Array header marker and the element classes it may carry.
constexpr std::uint32_t kArrayMarker = 0x80809FBDU;
constexpr std::uint32_t kBuildElementClass = 0x80809C04U;
constexpr std::uint32_t kDescriptorElementClass = 0x80809C22U;
constexpr std::uint32_t kMetadataElementClass = 0x80809C20U;
// All-ones marks an absent row index in every extracted table.
constexpr std::uint32_t kAbsentIndex = (std::numeric_limits<std::uint32_t>::max)();

enum class InputRole : std::uint8_t {
    action = 1,
    conditionLeft = 2,
    conditionRight = 3,
};

struct NodeShape final {
    std::uint32_t classId{};
    std::uint16_t size{};
    std::array<std::uint8_t, 3> roots{};
    std::uint8_t rootCount{};
    std::uint8_t expressionOffset{};
};

// Class id, blob size, root field offsets and expression offset of each node.
constexpr std::array kNodeShapes{
    NodeShape{0x80804E0FU, 0x58, {}, 0, 0x10},
    NodeShape{0x80804E10U, 0x10},
    NodeShape{0x80804E17U, 0x20},
    NodeShape{0x80804E19U, 0x20},
    NodeShape{0x80804E23U, 0x30, {0x10}, 1},
    NodeShape{0x80804E27U, 0x10},
    NodeShape{0x80804E30U, 0x0C},
    NodeShape{0x80804E31U, 0x20, {0x08}, 1},
    NodeShape{0x80804E35U, 0xA8},
    NodeShape{0x80804E38U, 0x08},
    NodeShape{0x80804E39U, 0x38, {0x08, 0x20}, 2},
    NodeShape{0x80804E3AU, 0x01},
    NodeShape{0x80804E3BU, 0x04},
    NodeShape{0x80804E3CU, 0x28, {0x08}, 1},
    NodeShape{0x80804E3DU, 0x68, {0x28, 0x48}, 2},
    NodeShape{0x80804E3EU, 0x10},
    NodeShape{0x80804E3FU, 0x18, {0x00}, 1},
    NodeShape{0x80804E40U, 0x50, {}, 0, 0x10},
    NodeShape{0x80804E41U, 0x10},
    NodeShape{0x80802A6EU, 0x01},
    NodeShape{0x80802A6FU, 0x50, {}, 0, 0x10},
    NodeShape{0x80802DA7U, 0x38},
    NodeShape{0x80804D7DU, 0x90},
    NodeShape{0x80804D5EU, 0x10},
    NodeShape{0x80804D71U, 0x08},
    NodeShape{0x80804D72U, 0x04},
    NodeShape{0x80804D73U, 0x60},
    NodeShape{0x80804D74U, 0x0C},
    NodeShape{0x80804D75U, 0x04},
    NodeShape{0x80804D76U, 0x01},
    NodeShape{0x80804D78U, 0x10},
    NodeShape{0x80804D7AU, 0x08},
    NodeShape{0x80804D7BU, 0x01},
    NodeShape{0x80804D7CU, 0x01},
    NodeShape{0x80804D80U, 0x01},
    NodeShape{0x80804D81U, 0x04},
    NodeShape{0x80804D82U, 0x0C},
    NodeShape{0x80804D83U, 0x04},
    NodeShape{0x80809310U, 0x18},
    NodeShape{0x80804E0EU, 0x01},
    NodeShape{0x80804E12U, 0x20, {0x08}, 1},
    NodeShape{0x80804E13U, 0x01},
    NodeShape{0x80804E14U, 0x01},
    NodeShape{0x80804E15U, 0x01},
    NodeShape{0x80804E1CU, 0x60, {0x18, 0x30, 0x48}, 3},
    NodeShape{0x80802907U, 0x58, {}, 0, 0x10},
    NodeShape{0x80802DA6U, 0x0C},
};

template <typename Value>
[[nodiscard]] bool
read_value(std::span<const std::byte> bytes, std::size_t offset, Value& output) noexcept {
    if (offset > bytes.size() || sizeof(Value) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&output, bytes.data() + offset, sizeof(Value));
    return true;
}

/** Resolves one signed self-relative field only when its target stays inside the blob. */
[[nodiscard]] bool
relative_target(std::span<const std::byte> bytes, std::size_t field, std::size_t& output) noexcept {
    std::int64_t relative = 0;
    if (!read_value(bytes, field, relative) || relative == 0) {
        return false;
    }
    if ((relative > 0
         && static_cast<std::uint64_t>(relative) > bytes.size() - (std::min)(field, bytes.size()))
        || (relative < 0 && static_cast<std::uint64_t>(-(relative + 1)) + 1U > field)) {
        return false;
    }
    output = static_cast<std::size_t>(static_cast<std::int64_t>(field) + relative);
    return output <= bytes.size();
}

[[nodiscard]] const NodeShape* shape(std::uint32_t classId) noexcept {
    const auto found = std::find_if(kNodeShapes.begin(),
                                    kNodeShapes.end(),
                                    [classId](const auto& row) { return row.classId == classId; });
    return found == kNodeShapes.end() ? nullptr : &*found;
}

/** Walks one behavior program's bytes, collecting its inputs and channel writes. */
class Parser final {
public:
    Parser(std::span<const std::byte> bytes,
           std::uint32_t programRow,
           std::vector<Input>& inputs,
           std::vector<ChannelWrite>& writes) noexcept
        : bytes_(bytes), programRow_(programRow), inputs_(inputs), writes_(writes) {}

    /** Walks one root and retains the channel edges exposed by known node layouts. */
    [[nodiscard]] bool parse(Program& output) {
        output.firstInput = static_cast<std::uint32_t>(inputs_.size());
        output.firstWrite = static_cast<std::uint32_t>(writes_.size());
        if (!root(0, 0)) {
            return false;
        }
        output.inputCount = static_cast<std::uint32_t>(inputs_.size()) - output.firstInput;
        output.writeCount = static_cast<std::uint32_t>(writes_.size()) - output.firstWrite;
        output.nodeCount = static_cast<std::uint32_t>(nodes_.size());
        output.expressionCount = static_cast<std::uint32_t>(expressions_.size());
        return true;
    }

private:
    /** Reads one bounded resource array with its repeated count. */
    [[nodiscard]] bool array(std::size_t countField,
                             std::size_t relativeField,
                             std::size_t stride,
                             std::uint64_t& count,
                             std::size_t& data) const noexcept {
        std::size_t header = 0;
        std::uint64_t repeated = 0;
        if (!read_value(bytes_, countField, count) || count > kMaximumRows) {
            return false;
        }
        if (count == 0) {
            data = 0;
            return true;
        }
        if (!relative_target(bytes_, relativeField, header) || !read_value(bytes_, header, repeated)
            || repeated != count || header > bytes_.size() - 16U
            || count > (bytes_.size() - header - 16U) / stride) {
            return false;
        }
        data = header + 16U;
        return true;
    }

    /** Visits one behavior-node array once within the depth bound. */
    [[nodiscard]] bool root(std::size_t offset, std::uint32_t depth) {
        if (depth > kMaximumDepth || offset > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        if (!roots_.insert(static_cast<std::uint32_t>(offset)).second) {
            return true;
        }
        std::uint64_t count = 0;
        std::size_t data = 0;
        if (!array(offset + 8U, offset + 16U, 16U, count, data)) {
            return false;
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            const std::size_t field = data + static_cast<std::size_t>(index) * 16U + 8U;
            std::size_t child = 0;
            if (!relative_target(bytes_, field, child) || !node(child, depth + 1U)) {
                return false;
            }
        }
        return true;
    }

    /** Retains every named channel input from one compiled expression. */
    [[nodiscard]] bool expression(std::size_t offset, std::uint32_t nodeOffset, InputRole role) {
        if (offset > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        const auto [_, inserted] = expressions_.insert(static_cast<std::uint32_t>(offset));
        if (!inserted) {
            return true;
        }
        std::uint64_t count = 0;
        std::size_t data = 0;
        std::uint64_t inputOrMode = 0;
        std::uint32_t activeField = 0;
        std::int32_t nativeOverride = 0;
        if (!array(offset, offset + 8U, 8U, count, data)
            || !read_value(bytes_, offset + 0x30U, inputOrMode)
            || !read_value(bytes_, offset + 0x38U, activeField)
            || !read_value(bytes_, offset + 0x3CU, nativeOverride)) {
            return false;
        }
        if (inputs_.size() > kMaximumRows - count) {
            return false;
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            const std::size_t row = data + static_cast<std::size_t>(index) * 8U;
            std::uint8_t selector = 0;
            std::uint32_t hash = 0;
            if (!read_value(bytes_, row, selector) || !read_value(bytes_, row + 4U, hash)) {
                return false;
            }
            inputs_.push_back({programRow_,
                               nodeOffset,
                               static_cast<std::uint32_t>(offset),
                               hash,
                               inputOrMode,
                               nativeOverride,
                               activeField,
                               selector,
                               static_cast<std::uint8_t>(role)});
        }
        return true;
    }

    /** Visits every polymorphic node referenced by one resource array. */
    [[nodiscard]] bool
    node_array(std::size_t countField, std::size_t relativeField, std::uint32_t depth) {
        std::uint64_t count = 0;
        std::size_t data = 0;
        if (!array(countField, relativeField, 16U, count, data)) {
            return false;
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            const std::size_t field = data + static_cast<std::size_t>(index) * 16U + 8U;
            std::size_t child = 0;
            if (!relative_target(bytes_, field, child) || !node(child, depth + 1U)) {
                return false;
            }
        }
        return true;
    }

    /** Walks one known node layout and its nested roots. */
    [[nodiscard]] bool node(std::size_t offset, std::uint32_t depth) {
        if (depth > kMaximumDepth || offset < 4U
            || offset > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        if (!nodes_.insert(static_cast<std::uint32_t>(offset)).second) {
            return true;
        }
        std::uint32_t classId = 0;
        if (!read_value(bytes_, offset - 4U, classId)) {
            return false;
        }
        const NodeShape* const spec = shape(classId);
        if (spec == nullptr || offset > bytes_.size() || spec->size > bytes_.size() - offset) {
            return false;
        }
        if (spec->expressionOffset != 0
            && !expression(offset + spec->expressionOffset,
                           static_cast<std::uint32_t>(offset),
                           InputRole::action)) {
            return false;
        }
        if (classId == kStorageChannelClass) {
            std::uint32_t hash = 0;
            if (!read_value(bytes_, offset, hash) || writes_.size() >= kMaximumRows) {
                return false;
            }
            writes_.push_back({programRow_, static_cast<std::uint32_t>(offset), hash});
        }
        if (classId == kConditionClass
            && (!expression(
                    offset + 8U, static_cast<std::uint32_t>(offset), InputRole::conditionLeft)
                || !expression(offset + 0x48U,
                               static_cast<std::uint32_t>(offset),
                               InputRole::conditionRight))) {
            return false;
        }
        if (classId == kFilterClass && !node_array(offset + 0x18U, offset + 0x20U, depth)) {
            return false;
        }
        if (classId == kConditionGroupClass && !node_array(offset + 8U, offset + 0x10U, depth)) {
            return false;
        }
        if (classId == kTargetClass) {
            std::size_t child = 0;
            std::int64_t relative = 0;
            if (!read_value(bytes_, offset + 8U, relative)) {
                return false;
            }
            if (relative != 0
                && (!relative_target(bytes_, offset + 8U, child) || !node(child, depth + 1U))) {
                return false;
            }
        }
        for (std::uint8_t index = 0; index < spec->rootCount; ++index) {
            if (!root(offset + spec->roots[index], depth + 1U)) {
                return false;
            }
        }
        return true;
    }

    std::span<const std::byte> bytes_{};
    std::uint32_t programRow_{};
    std::vector<Input>& inputs_;
    std::vector<ChannelWrite>& writes_;
    std::unordered_set<std::uint32_t> roots_{};
    std::unordered_set<std::uint32_t> nodes_{};
    std::unordered_set<std::uint32_t> expressions_{};
};

struct ParsedProgram final {
    Program program{};
    std::vector<Input> inputs{};
    std::vector<ChannelWrite> writes{};
    bool graphParsed{};
};

struct BuildOwner final {
    std::uint32_t actorClassIndex{};
    std::uint32_t configTag{};
    std::uint32_t buildOrdinal{};
    std::uint32_t descriptorOrdinal{kAbsentIndex};
    std::uint32_t submitterSubtype{};
};

/** Maps one proved submitter subtype without guessing unresolved rows. */
[[nodiscard]] SubmissionKind submission_kind(std::uint32_t subtype) noexcept {
    switch (subtype) {
    case 0x80804DBAU:
    case 0x80804DA6U:
    case 0x80804DD1U:
    case 0x80804DC2U:
    case 0x80804DACU:
    case 0x80806779U:
    case 0x80804DBEU:
    case 0x80804D8DU:
    case 0x80804DCCU:
    case 0x80804DC0U:
    case 0x80804DEBU:
    case 0x80802F92U:
    case 0x80804FBBU:
        return SubmissionKind::activeNative;
    case 0x80804DE4U:
    case 0x80804FB0U:
    case 0x80806785U:
    case 0x808031D1U:
    case 0x808072C0U:
    case 0x80803E70U:
    case 0x80805FB3U:
    case 0x80805FB0U:
    case 0x80804DB2U:
    case 0x80804491U:
    case 0x80803926U:
    case 0x808092A4U:
    case 0x80802FF8U:
    case 0x80806782U:
    case 0x808072CBU:
    case 0x808084D7U:
        return SubmissionKind::passive;
    default:
        return SubmissionKind::unresolved;
    }
}

/** Reads one typed actor-definition array within the source blob. */
[[nodiscard]] bool array(std::span<const std::byte> bytes,
                         std::size_t countField,
                         std::size_t relativeField,
                         std::size_t stride,
                         std::uint32_t elementClass,
                         std::uint64_t& count,
                         std::size_t& data) noexcept {
    std::int64_t relative = 0;
    if (!read_value(bytes, countField, count) || !read_value(bytes, relativeField, relative)
        || count > kMaximumRows) {
        return false;
    }
    if (count == 0 && relative == 0) {
        data = 0;
        return true;
    }
    if (count == 0 || relative == 0) {
        return false;
    }
    const std::int64_t headerValue = static_cast<std::int64_t>(relativeField) + relative;
    if (headerValue < 4) {
        return false;
    }
    const std::size_t header = static_cast<std::size_t>(headerValue);
    std::uint32_t marker = 0;
    std::uint64_t repeated = 0;
    std::uint32_t actualClass = 0;
    if (!read_value(bytes, header - 4U, marker) || !read_value(bytes, header, repeated)
        || !read_value(bytes, header + 8U, actualClass) || marker != kArrayMarker
        || repeated != count || actualClass != elementClass || bytes.size() < 16U
        || header > bytes.size() - 16U || count > (bytes.size() - header - 16U) / stride) {
        return false;
    }
    data = header + 16U;
    return true;
}

/** Projects the behavior-owner candidates from one actor definition. */
[[nodiscard]] bool actor_builds(std::span<const std::byte> bytes,
                                std::uint32_t actorClassIndex,
                                std::vector<BuildOwner>& output,
                                std::vector<std::uint32_t>& configTags) {
    std::uint64_t buildCount = 0;
    std::int64_t buildRelative = 0;
    if (!read_value(bytes, 0x10U, buildCount) || !read_value(bytes, 0x18U, buildRelative)
        || buildCount > kMaximumRows) {
        return false;
    }
    if (buildCount == 0 && buildRelative == 0) {
        return true;
    }
    if (buildCount == 0 || buildRelative < 0) {
        return false;
    }
    const std::size_t header = static_cast<std::size_t>(buildRelative);
    std::uint32_t marker = 0;
    std::uint64_t repeated = 0;
    std::uint32_t elementClass = 0;
    if (!read_value(bytes, header + 20U, marker) || !read_value(bytes, header + 24U, repeated)
        || !read_value(bytes, header + 32U, elementClass) || marker != kArrayMarker
        || repeated != buildCount || elementClass != kBuildElementClass || bytes.size() < 40U
        || header > bytes.size() - 40U || buildCount > (bytes.size() - header - 40U) / 12U) {
        return false;
    }
    std::uint64_t descriptorCount = 0;
    std::uint64_t metadataCount = 0;
    std::size_t descriptorData = 0;
    std::size_t metadataData = 0;
    if (!array(bytes, 0x58U, 0x60U, 40U, kDescriptorElementClass, descriptorCount, descriptorData)
        || !array(bytes, 0x68U, 0x70U, 24U, kMetadataElementClass, metadataCount, metadataData)) {
        return false;
    }
    const std::uint64_t pairedDescriptorCount = (std::min)(descriptorCount, metadataCount);
    const std::size_t buildData = header + 40U;
    for (std::uint64_t build = 0; build < buildCount; ++build) {
        std::uint32_t configTag = 0;
        if (!read_value(bytes, buildData + static_cast<std::size_t>(build) * 12U, configTag)) {
            return false;
        }
        if (configTag == 0 || configTag == kAbsentIndex) {
            continue;
        }
        configTags.push_back(configTag);
        bool exposed = false;
        for (std::uint64_t descriptor = 0; descriptor < pairedDescriptorCount; ++descriptor) {
            const std::size_t row = metadataData + static_cast<std::size_t>(descriptor) * 24U;
            const std::size_t descriptorRow =
                descriptorData + static_cast<std::size_t>(descriptor) * 40U;
            std::uint32_t metadataConfig = 0;
            std::uint32_t subtype = 0;
            std::uint32_t metadataBuild = 0;
            std::uint32_t descriptorSubtype = 0;
            std::uint32_t descriptorConfig = 0;
            if (!read_value(bytes, row, metadataConfig) || !read_value(bytes, row + 4U, subtype)
                || !read_value(bytes, row + 16U, metadataBuild)
                || !read_value(bytes, descriptorRow + 12U, descriptorSubtype)
                || !read_value(bytes, descriptorRow + 16U, descriptorConfig)) {
                return false;
            }
            if (metadataConfig == configTag && metadataBuild == build
                && descriptorConfig == configTag && descriptorSubtype == subtype) {
                output.push_back({actorClassIndex,
                                  configTag,
                                  static_cast<std::uint32_t>(build),
                                  static_cast<std::uint32_t>(descriptor),
                                  subtype});
                exposed = true;
            }
        }
        if (!exposed) {
            output.push_back(
                {actorClassIndex, configTag, static_cast<std::uint32_t>(build), kAbsentIndex, 0});
        }
    }
    return true;
}

/** Joins readable actor configs to installed roots without requiring optional coverage. */
[[nodiscard]] bool build_owners(const reader::Source& source,
                                std::span<const std::uint32_t> actorTags,
                                CancelProbe cancel,
                                void* cancelContext,
                                Snapshot& output) {
    if (actorTags.empty()) {
        return true;
    }
    std::vector<parallel::Held> actors{};
    if (!parallel::read_kept(source, actorTags, actors)) {
        parallel::release();
        return false;
    }
    output.unreadActors = static_cast<std::uint32_t>(actorTags.size() - actors.size());
    std::vector<BuildOwner> builds{};
    std::vector<std::uint32_t> configTags{};
    for (const parallel::Held& actor : actors) {
        if (cancel != nullptr && cancel(cancelContext)) {
            parallel::release();
            return false;
        }
        const auto actorTag = std::lower_bound(actorTags.begin(), actorTags.end(), actor.tag);
        if (actorTag == actorTags.end() || *actorTag != actor.tag) {
            ++output.skippedActors;
            continue;
        }
        std::vector<BuildOwner> actorBuilds{};
        std::vector<std::uint32_t> actorConfigs{};
        if (!actor_builds(actor.blob,
                          static_cast<std::uint32_t>(actorTag - actorTags.begin()),
                          actorBuilds,
                          actorConfigs)) {
            ++output.skippedActors;
            continue;
        }
        builds.insert(builds.end(), actorBuilds.begin(), actorBuilds.end());
        configTags.insert(configTags.end(), actorConfigs.begin(), actorConfigs.end());
    }
    std::sort(configTags.begin(), configTags.end());
    configTags.erase(std::unique(configTags.begin(), configTags.end()), configTags.end());
    std::vector<parallel::Held> configs{};
    if (!parallel::read_kept(source, configTags, configs)) {
        parallel::release();
        return false;
    }
    output.unreadConfigs = static_cast<std::uint32_t>(configTags.size() - configs.size());
    std::unordered_map<std::uint32_t, std::uint32_t> programs{};
    programs.reserve(output.programs.size());
    for (std::size_t index = 0; index < output.programs.size(); ++index) {
        programs.emplace(output.programs[index].rootTag, static_cast<std::uint32_t>(index));
    }
    std::unordered_multimap<std::uint32_t, const BuildOwner*> owners{};
    owners.reserve(builds.size());
    for (const BuildOwner& build : builds) {
        owners.emplace(build.configTag, &build);
    }
    for (const parallel::Held& config : configs) {
        const auto [first, last] = owners.equal_range(config.tag);
        for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= config.blob.size();
             offset += sizeof(std::uint32_t)) {
            std::uint32_t rootTag = 0;
            std::memcpy(&rootTag, config.blob.data() + offset, sizeof rootTag);
            const auto program = programs.find(rootTag);
            if (program == programs.end()) {
                continue;
            }
            for (auto owner = first; owner != last; ++owner) {
                const BuildOwner& build = *owner->second;
                output.owners.push_back({program->second,
                                         build.actorClassIndex,
                                         config.tag,
                                         static_cast<std::uint32_t>(offset),
                                         build.buildOrdinal,
                                         build.descriptorOrdinal,
                                         build.submitterSubtype,
                                         submission_kind(build.submitterSubtype)});
            }
        }
    }
    parallel::release();
    std::sort(
        output.owners.begin(), output.owners.end(), [](const Owner& first, const Owner& second) {
            return std::tie(first.programRow,
                            first.actorClassIndex,
                            first.configTag,
                            first.configFieldOffset,
                            first.buildOrdinal,
                            first.descriptorOrdinal)
                   < std::tie(second.programRow,
                              second.actorClassIndex,
                              second.configTag,
                              second.configFieldOffset,
                              second.buildOrdinal,
                              second.descriptorOrdinal);
        });
    return true;
}

struct BuildContext final {
    CancelProbe cancel{};
    void* cancelContext{};
    std::vector<std::vector<ParsedProgram>>* workers{};
    std::atomic<bool> failed{};
};

/** Collects the fixed class scan before any encrypted package reads begin. */
[[nodiscard]] bool collect_tag(void* opaque, std::uint32_t tag) noexcept {
    auto& tags = *static_cast<std::vector<std::uint32_t>*>(opaque);
    try {
        tags.push_back(tag);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool cancelled(const BuildContext& context) noexcept {
    return context.cancel != nullptr && context.cancel(context.cancelContext);
}

/** Reports row-local omissions without turning them into a publication gate. */
void log_summary(const Snapshot& snapshot) noexcept {
    std::array<char, 320> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "activity_sdk_behaviors programs=%zu parsed=%u unparsed=%u "
                      "unread=%u inputs=%zu writes=%zu owners=%zu actors_skipped=%u "
                      "actors_unread=%u configs_unread=%u",
                      snapshot.programs.size(),
                      snapshot.parsedPrograms,
                      snapshot.unparsedPrograms,
                      snapshot.unreadPrograms,
                      snapshot.inputs.size(),
                      snapshot.writes.size(),
                      snapshot.owners.size(),
                      snapshot.skippedActors,
                      snapshot.unreadActors,
                      snapshot.unreadConfigs);
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

/** Parses one readable root on the reader thread and keeps failures row-local. */
void collect(void* opaque,
             std::size_t worker,
             std::uint32_t tag,
             std::span<const std::byte> bytes) noexcept {
    auto& context = *static_cast<BuildContext*>(opaque);
    if (context.failed.load(std::memory_order_relaxed) || cancelled(context)
        || context.workers == nullptr || worker >= context.workers->size()) {
        context.failed.store(true, std::memory_order_relaxed);
        return;
    }
    try {
        ParsedProgram parsed{};
        parsed.program.rootTag = tag;
        Parser parser(bytes, 0, parsed.inputs, parsed.writes);
        parsed.graphParsed = parser.parse(parsed.program);
        if (!parsed.graphParsed) {
            parsed.inputs.clear();
            parsed.writes.clear();
            parsed.program = {};
            parsed.program.rootTag = tag;
        }
        (*context.workers)[worker].push_back(std::move(parsed));
    } catch (...) {
        context.failed.store(true, std::memory_order_relaxed);
    }
}

} // namespace

/** Builds every scanned root and all safely recoverable channel and owner edges. */
bool build(const reader::Source& source,
           std::span<const std::uint32_t> actorTags,
           CancelProbe cancel,
           void* cancelContext,
           Snapshot& output) noexcept {
    output = {};
    if (source.directory.empty() || source.keys == nullptr) {
        return false;
    }
    try {
        std::vector<std::uint32_t> tags{};
        reader::ScanResult scan{};
        if (!reader::scan_class(source.directory, kRootClass, &collect_tag, &tags, scan)
            || (cancel != nullptr && cancel(cancelContext))) {
            output = {};
            return false;
        }
        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        if (tags.size() > kMaximumRows) {
            return false;
        }
        std::vector<std::vector<ParsedProgram>> workers(parallel::worker_count());
        BuildContext context{cancel, cancelContext, &workers};
        if (!parallel::read_tags(source, tags, &collect, &context)
            || context.failed.load(std::memory_order_relaxed)) {
            parallel::release();
            return false;
        }
        parallel::release();
        std::vector<ParsedProgram> parsed{};
        for (auto& worker : workers) {
            for (ParsedProgram& row : worker) {
                parsed.push_back(std::move(row));
            }
        }
        std::sort(parsed.begin(), parsed.end(), [](const auto& first, const auto& second) {
            return first.program.rootTag < second.program.rootTag;
        });
        std::size_t parsedIndex = 0;
        for (const std::uint32_t tag : tags) {
            ParsedProgram missing{};
            missing.program.rootTag = tag;
            ParsedProgram* row = &missing;
            if (parsedIndex < parsed.size() && parsed[parsedIndex].program.rootTag == tag) {
                row = &parsed[parsedIndex++];
            }
            if (output.inputs.size() > kMaximumRows - row->inputs.size()
                || output.writes.size() > kMaximumRows - row->writes.size()) {
                output = {};
                return false;
            }
            const std::uint32_t programIndex = static_cast<std::uint32_t>(output.programs.size());
            row->program.firstInput = static_cast<std::uint32_t>(output.inputs.size());
            row->program.firstWrite = static_cast<std::uint32_t>(output.writes.size());
            for (Input& input : row->inputs) {
                input.programRow = programIndex;
                output.inputs.push_back(input);
            }
            for (ChannelWrite& write : row->writes) {
                write.programRow = programIndex;
                output.writes.push_back(write);
            }
            output.programs.push_back(row->program);
            if (row->graphParsed) {
                ++output.parsedPrograms;
            } else if (row == &missing) {
                ++output.unreadPrograms;
            } else {
                ++output.unparsedPrograms;
            }
        }
        if (!build_owners(source, actorTags, cancel, cancelContext, output)) {
            output = {};
            return false;
        }
        output.ready = true;
        log_summary(output);
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::behavior_inventory
