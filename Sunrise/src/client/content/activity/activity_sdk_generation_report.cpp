#include "activity_sdk_generation_report.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"

namespace sunrise::client::content::activity::sdk_generation {

/**
 * Emits one bounded completion record for an SDK generation run.
 * The detail is free text from the builder, so it is quoted and placed last.
 */
void report_result(const PassResult& result) noexcept {
    std::array<char, 384> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=sdk_generation result=%s scenarios=%zu activity_roots=%zu "
                      "activities=%zu built=%zu reused=%zu scenario=0x%08X detail=\"%s\"",
                      result.complete ? "ok" : "fail",
                      result.scenarios,
                      result.activityRoots,
                      result.activityVariants,
                      result.built,
                      result.reused,
                      static_cast<unsigned>(result.failureScenario),
                      result.detail != nullptr ? result.detail : "none");
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            result.complete ? core::log::Level::info : core::log::Level::error,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1)});
    }
}

} // namespace sunrise::client::content::activity::sdk_generation
