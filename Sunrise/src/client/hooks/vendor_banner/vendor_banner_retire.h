#pragma once

#include <cstddef>
#include <string_view>

#include "../../patterns/image_scan.h"

namespace sunrise::client::hooks::vendor_banner {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * The vendor picker's per-interaction retire test.
 *
 * The picker keeps the highest-priority interaction this test does not reject, so answering true
 * for a row makes it skip to the next. It is the game's own mechanism for dropping an answered
 * banner, driven by a list nothing appends to offline. `state::vendors` is the list Sunrise keeps
 * instead: this hook reads it, the Server writes it when a quest grant commits. The prologue is
 * clean and non-Arxan, and the signature is unique in the image.
 */
inline constexpr std::string_view kRetireSignatureText =
    "48 89 5C 24 ? 48 89 6C 24 ? 56 48 83 EC ? 44 8B 49 ? 33 ED 0F B7 DA 48 8B F1";
/** Compiled pattern bytes of the signature text above. */
inline constexpr auto kRetireSignature =
    signature<signature_length(kRetireSignatureText)>(kRetireSignatureText);

/** Fields of one vendor's picker state, as byte offsets from its base. */
struct StateLayout {
    /** Vendor index row, which is the index the wire carries. */
    static constexpr std::size_t vendorIndex = 0;
    /** Interaction the vendor is showing right now, or -1 while it shows none. */
    static constexpr std::size_t selectedInteraction = 2;
};

/**
 * Attaches the retire gate.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install() noexcept;

/** Detaches the gate and forgets every answer. */
void uninstall() noexcept;

} // namespace sunrise::client::hooks::vendor_banner
