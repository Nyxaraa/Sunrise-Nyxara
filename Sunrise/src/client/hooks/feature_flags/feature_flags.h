#pragma once

namespace sunrise::client::hooks::feature_flags {

/**
 * Registers the named client feature flags this build asks for, once.
 * Diagnostic only. It answers whether the ownerless channel close is on the path to the fast-travel
 * freeze, and comes out again once that question is settled.
 */
void apply_once() noexcept;

} // namespace sunrise::client::hooks::feature_flags
