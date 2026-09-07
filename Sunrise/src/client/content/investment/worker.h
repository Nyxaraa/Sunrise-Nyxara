#pragma once

#include <cstdint>

namespace sunrise::client::content::investment::worker {

/** Allows cooperative investment refresh slices on the caller-owned game thread. */
void activate() noexcept;

/**
 * Runs one due bounded refresh slice on the caller-owned game thread.
 * @param nowMilliseconds Current monotonic process tick in milliseconds.
 */
void service(std::uint64_t nowMilliseconds) noexcept;

/** Stops taking refresh slices and clears the pending overlay. */
void reset() noexcept;

/**
 * Makes the next `service` pump take another refresh slice even though a prior one completed.
 * A committed mutation that invalidates an already-published build-data domain needs this: the
 * completion latch would otherwise never notice the domain came back stale.
 */
void request_slice() noexcept;

} // namespace sunrise::client::content::investment::worker
