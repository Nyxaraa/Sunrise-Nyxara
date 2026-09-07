#include "type23_placement_identifier_reader.h"

#include <limits>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {

/** Reads the class-specific opaque identifier used for an exact package equality join. */
bool type23_placement_identifier(std::span<const std::byte> blob,
                                 const SlotDescriptor& descriptor,
                                 std::uint64_t& output) noexcept {
    output = 0;
    if (descriptor.componentClass != kType23ComponentClass || descriptor.slotType != 23
        || descriptor.descriptorOffset
               > (std::numeric_limits<std::size_t>::max)() - kType23PlacementIdentifierOffset) {
        return false;
    }
    return read(blob,
                static_cast<std::size_t>(descriptor.descriptorOffset)
                    + kType23PlacementIdentifierOffset,
                output);
}

} // namespace sunrise::middleware::content::packages::tables
