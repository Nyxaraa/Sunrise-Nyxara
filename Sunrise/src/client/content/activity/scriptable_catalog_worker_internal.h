#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "../../../middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "scriptable_catalog_authored_placements.h"
#include "scriptable_catalog_builder.h"
#include "scriptable_catalog_names.h"
#include "scriptable_catalog_reference_scan.h"
#include "scriptable_catalog_trigger_volumes.h"
#include "source.h"

namespace sunrise::client::content::activity::scriptables::worker_internal {

namespace catalog = state::build_data::scriptables;
namespace package_reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

/** One object declares at most this many slots; a larger layout is refused. */
inline constexpr std::size_t kSlotCapacity = 262'144;

struct AnalysisSlot final {
    std::uint32_t nameHash{};
    std::uint16_t type{};
};

/** One validated descriptor plus its class-specific optional placement identifier. */
struct AnalysisDescriptor final {
    tables::SlotDescriptor descriptor{};
    std::uint64_t placementIdentifier{};
    bool placementIdentifierRead{};
};

/** One exact inline string found in a source tag used by an object analysis. */
struct AnalysisInlineName final {
    std::uint32_t hash{};
    std::string value{};
};

/** Inline strings from one first-seen source tag, kept in package encounter order. */
struct AnalysisTagEvidence final {
    std::uint32_t tag{};
    std::vector<AnalysisInlineName> names{};
};

/** Everything one scenario analysis produced, keyed and shared between build passes. */
struct Analysis final {
    std::vector<AnalysisSlot> slots{};
    std::vector<AnalysisDescriptor> descriptors{};
    std::vector<internal::RawReference> references{};
    internal::AuthoredPlacementAnalysis authored{};
    std::vector<std::uint32_t> observedConfigs{};
    std::vector<std::uint32_t> resolvedConfigs{};
    std::vector<catalog::PlacedSubblock> placedSubblocks{};
    std::vector<catalog::PlacedLeaf> placedLeaves{};
    std::vector<catalog::PlacedHop> placedHops{};
    std::vector<catalog::PlacedConfigOccurrence> placedConfigOccurrences{};
    std::vector<catalog::PlacedBareTarget> placedBareTargets{};
    std::vector<AnalysisTagEvidence> tagEvidence{};
    std::uint32_t configCount{};
    /** Authored placements whose flags and class definition let the game replicate them. */
    std::uint32_t replicatedPlacementCount{};
    bool readComplete{true};
};

using AnalysisMap = std::unordered_map<std::uint64_t, std::shared_ptr<const Analysis>>;

/** Inputs and owned scratch of one scenario build pass. */
struct BuildContext final {
    const package_reader::Source* source{};
    internal::BuilderCancelCheck cancel{};
    ScenarioSource scenarioSource{};
    std::vector<std::byte> scenario{};
    std::vector<std::byte> chain{};
    std::vector<std::byte> classBytes{};
    /** Replication bit per placed class definition, read once per tag. */
    std::unordered_map<std::uint32_t, bool> classReplication{};
    std::vector<internal::InlineName> inlineNames{};
    std::vector<internal::TriggerVolumeInput> triggerVolumeInputs{};
    AnalysisMap analyses{};
    AnalysisMap* sharedAnalyses{};
    Analysis* recordingAnalysis{};
    std::unordered_set<std::uint32_t> recordedAnalysisTags{};
    bool recordingCacheable{};
    /** Tags whose inline strings were already banked, so no blob is scanned twice. */
    std::unordered_set<std::uint32_t> scannedTags{};
    /** Total tag reads this scenario asked for, including every revisit. */
    std::size_t tagReads{};
    std::size_t analysisHits{};
    std::size_t analysisMisses{};
    std::shared_ptr<catalog::Snapshot> output{};
    tables::WalkResult walk{};
    bool failed{};
};

/** @return True when the caller asked this build to stop. */
[[nodiscard]] bool cancelled(internal::BuilderCancelCheck check) noexcept;

/** Reads one tag and offers its inline names to the current build. */
[[nodiscard]] bool read_tag(BuildContext& context,
                            std::uint32_t tag,
                            std::vector<std::byte>& bytes,
                            std::uint32_t& classId) noexcept;

/** Reads one object layout and its reachable descriptor/config records. */
[[nodiscard]] bool analyze_object(BuildContext& context,
                                  const tables::Placement& placement,
                                  Analysis& output) noexcept;

} // namespace sunrise::client::content::activity::scriptables::worker_internal
