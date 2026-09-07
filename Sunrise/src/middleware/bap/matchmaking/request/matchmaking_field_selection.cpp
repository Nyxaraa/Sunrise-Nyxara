#include <array>

#include "field_selection.h"
#include "internal.h"

namespace sunrise::middleware::bap::matchmaking::request {
namespace {

using protobuf::Field;
using protobuf::Reader;
using protobuf::WireType;

/** Service-42 root protobuf fields the responder reads. */
enum class RootField : std::uint32_t {
    /** Varint selector. It picks one of 8 request shapes. */
    kind = 2,
    /** Length-delimited advertisement request, used only by kind 2. */
    advertisement = 8,
    /** Length-delimited launch target, used only by kind 4. */
    activity = 11,
};

/** Index of each root pick in the selection array. */
constexpr std::size_t kKindPick = 0;
constexpr std::size_t kAdvertisementPick = 1;
constexpr std::size_t kActivityPick = 2;
constexpr std::size_t kRootPickCount = 3;

/**
 * Converts one raw selector. Values outside the supported set are rejected.
 * @param value Unsigned service-42 field-two selector.
 * @return Request kind for a supported value, else none.
 */
[[nodiscard]] RequestKind request_kind(std::uint64_t value) noexcept {
    // Selectors 1 to 8 are contiguous, so an in-range value names its own kind.
    if (value < static_cast<std::uint64_t>(RequestKind::sessionSearch)
        || value > static_cast<std::uint64_t>(RequestKind::liveStats)) {
        return RequestKind::none;
    }
    return static_cast<RequestKind>(value);
}

} // namespace

/** Records the first occurrence of each wanted field in one whole protobuf message. */
bool select_first_fields(std::span<const std::byte> input, std::span<FieldPick> picks) noexcept {
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field;
        if (!reader.next(field)) {
            return false;
        }
        for (FieldPick& pick : picks) {
            if (pick.fieldNumber != field.fieldNumber) {
                continue;
            }
            // Mark a field before checking its wire type. A later duplicate then cannot replace
            // the first occurrence.
            if (!pick.seen) {
                pick.seen = true;
                if (field.wireType == pick.wireType) {
                    pick.has = true;
                    pick.value = field.value;
                    pick.bytes = field.bytes;
                }
            }
            break;
        }
    }
    return true;
}

/** Picks the fields we need and checks the whole service-42 root. */
bool parse_root(std::span<const std::byte> input, Root& selected) noexcept {
    selected = {};
    std::array<FieldPick, kRootPickCount> picks{
        FieldPick{static_cast<std::uint32_t>(RootField::kind), WireType::varint},
        FieldPick{static_cast<std::uint32_t>(RootField::advertisement), WireType::lengthDelimited},
        FieldPick{static_cast<std::uint32_t>(RootField::activity), WireType::lengthDelimited},
    };
    if (!select_first_fields(input, picks)) {
        return false;
    }
    if (picks[kKindPick].has) {
        selected.kind = request_kind(picks[kKindPick].value);
    }
    selected.hasAdvertisementMessage = picks[kAdvertisementPick].has;
    selected.advertisementMessage = picks[kAdvertisementPick].bytes;
    selected.hasActivityMessage = picks[kActivityPick].has;
    selected.activityMessage = picks[kActivityPick].bytes;
    return true;
}

} // namespace sunrise::middleware::bap::matchmaking::request
