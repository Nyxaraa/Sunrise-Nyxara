#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::activity::sdk_generation {

/** One finished generation pass, in the exact terms its log line reports. */
struct PassResult final {
    std::size_t scenarios{};
    std::size_t activityRoots{};
    std::size_t activityVariants{};
    std::size_t built{};
    std::size_t reused{};
    /** Short stable token naming the stage that refused. Empty on success. */
    const char* detail{};
    std::uint32_t failureScenario{};
    bool complete{};
};

/** Writes one bounded completion line without exposing package-key material. */
void report_result(const PassResult& result) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation
