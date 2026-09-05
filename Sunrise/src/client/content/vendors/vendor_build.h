#pragma once

#include <cstdint>
#include <span>

#include "../../../middleware/content/packages/reader/reader.h"

namespace sunrise::client::content::vendors {

/**
 * Extracts the vendor catalog from the installed packages, once.
 *
 * The whole index is read. A definition is read only for a vendor asked for by hash, as each is
 * over 100 KiB and the banks hold nowhere near all 511. The named hashes are checked against the
 * index the pass has just read - one it does not carry is a mistyped rule and is logged - and
 * whatever room is left is filled from the head of the index, so a short list still gets the
 * vendors the old leading window would have covered.
 *
 * @param source Package directory and borrowed block keys.
 * @param scratch Lock-owned block storage shared with the other content passes.
 * @param namedHashes Vendor definition hashes named by `vendor_catalog.txt`, in priority order.
 * @return True when State already holds the catalog or a full pass publishes it.
 */
[[nodiscard]] bool build(const middleware::content::packages::reader::Source& source,
                         middleware::content::packages::reader::Scratch& scratch,
                         std::span<const std::uint32_t> namedHashes) noexcept;

} // namespace sunrise::client::content::vendors
