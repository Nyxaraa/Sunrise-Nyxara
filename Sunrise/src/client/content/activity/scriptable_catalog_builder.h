#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "scriptable_catalog_container_index.h"

namespace sunrise::client::content::activity::scriptables::internal {

using BuilderCancelCheck = bool (*)() noexcept;

/**
 * Immutable source analyses shared by scenario builds from one installed package estate.
 * Use one instance for one sequential pass. Destroy it before changing the source estate.
 */
class ScenarioAnalysisCache final {
public:
    ScenarioAnalysisCache() noexcept;
    ~ScenarioAnalysisCache();

    ScenarioAnalysisCache(const ScenarioAnalysisCache&) = delete;
    ScenarioAnalysisCache& operator=(const ScenarioAnalysisCache&) = delete;
    ScenarioAnalysisCache(ScenarioAnalysisCache&&) = delete;
    ScenarioAnalysisCache& operator=(ScenarioAnalysisCache&&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};

    friend std::shared_ptr<state::build_data::scriptables::Snapshot>
    build_scenario_catalog(const middleware::content::packages::reader::Source& source,
                           const ContainerIndex& containers,
                           middleware::content::packages::reader::Scratch& scratch,
                           ScenarioAnalysisCache& analyses,
                           std::uint32_t scenarioTag,
                           std::string_view scenarioName,
                           BuilderCancelCheck cancel);
};

/**
 * Builds one immutable package-derived scenario catalog without publishing it.
 * The caller owns package-key lifetime for the complete call and may attach its own request or
 * revision identity after the returned snapshot is complete.
 */
[[nodiscard]] std::shared_ptr<state::build_data::scriptables::Snapshot>
build_scenario_catalog(const middleware::content::packages::reader::Source& source,
                       const ContainerIndex& containers,
                       middleware::content::packages::reader::Scratch& scratch,
                       std::uint32_t scenarioTag,
                       std::string_view scenarioName,
                       BuilderCancelCheck cancel);

/** Builds one scenario while reusing source-only object analysis from earlier pass rows. */
[[nodiscard]] std::shared_ptr<state::build_data::scriptables::Snapshot>
build_scenario_catalog(const middleware::content::packages::reader::Source& source,
                       const ContainerIndex& containers,
                       middleware::content::packages::reader::Scratch& scratch,
                       ScenarioAnalysisCache& analyses,
                       std::uint32_t scenarioTag,
                       std::string_view scenarioName,
                       BuilderCancelCheck cancel);

} // namespace sunrise::client::content::activity::scriptables::internal
