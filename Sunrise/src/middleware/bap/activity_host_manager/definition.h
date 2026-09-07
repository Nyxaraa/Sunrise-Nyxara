#pragma once

#include <cstddef>
#include <span>

namespace sunrise::middleware::bap::activity_host_manager {

/** Checked service-6 request fields, borrowed from the caller's body. */
struct Request final {
    /** Holds credentials. Never keep, log, capture or save this view. */
    std::span<const std::byte> protobuf{};
    /** Reserved producer bytes after protobuf. They share the same sensitive lifetime. */
    std::span<const std::byte> padding{};
};

} // namespace sunrise::middleware::bap::activity_host_manager
