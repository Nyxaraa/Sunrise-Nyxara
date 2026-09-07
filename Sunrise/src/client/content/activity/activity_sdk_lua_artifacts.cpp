#include "activity_sdk_lua_artifacts.h"

#include <cstdio>
#include <utility>

#include "activity_sdk_lua_artifacts_internal.h"

namespace sunrise::client::content::activity::sdk_generation::lua_artifacts {

/** Maps every public status to its stable token. */
const char* status_name(Status value) noexcept {
    switch (value) {
    case Status::ready:
        return "ready";
    case Status::invalidInput:
        return "invalid_input";
    case Status::unsupportedAbi:
        return "unsupported_abi";
    case Status::buildFailure:
        return "build_failure";
    case Status::directoryFailure:
        return "directory_failure";
    case Status::writeFailure:
        return "write_failure";
    }
    return "invalid_input";
}

/** Replaces output only after every generated artifact is complete. */
Status build(const Source& source, Bundle& output) noexcept {
    try {
        Bundle pending{};
        // Name the stage, because one silent false here hides which projection drifted.
        const auto refused = [](const char* stage) noexcept {
            std::printf("ev=activity_sdk_lua_artifacts stage=%s result=refused\n", stage);
            return Status::buildFailure;
        };
        if (!internal::render_contract_files(source, pending)) {
            return refused("contract_files");
        }
        if (!internal::render_activity_files(source, pending)) {
            return refused("activity_files");
        }
        output = std::move(pending);
        return Status::ready;
    } catch (...) {
        return Status::buildFailure;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::lua_artifacts
