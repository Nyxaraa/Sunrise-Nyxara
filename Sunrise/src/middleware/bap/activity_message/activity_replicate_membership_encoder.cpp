#include "replicate_membership.h"

namespace sunrise::middleware::bap::activity_message::replicate_membership {
namespace {

/** Both bodies end mid-byte, so the sizes are the meaningful bits rounded up. */
static_assert(kEncodedSize * 8U >= kMeaningfulBitCount
              && kEncodedSize * 8U - kMeaningfulBitCount < 8U);
static_assert(kRemoteHostEncodedSize * 8U >= kRemoteHostMeaningfulBitCount
              && kRemoteHostEncodedSize * 8U - kRemoteHostMeaningfulBitCount < 8U);
static_assert(kRemoteHostMeaningfulBitCount == kMeaningfulBitCount + kRemoteMemberBitDelta);

} // namespace

/** Encodes one fixed full-player membership snapshot without allocation. */
bool encode_replicate_membership(const MembershipSnapshot& snapshot,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    written = 0;
    const std::size_t size = encoded_size(snapshot);
    if (output.size() < size || !valid(snapshot)) {
        return false;
    }

    encoding::bits::Writer writer(output.first(size));
    const bool encoded = writer.write(1, 1) && writer.write(snapshot.revision, 32)
                         && writer.write(snapshot.epoch, 32) && write_member_table(writer, snapshot)
                         && writer.write(1, 1) && write_region_block(writer, snapshot)
                         && writer.write(1, 1) && writer.write(occupied_member_mask(snapshot), 32)
                         && writer.write(1, 1) && writer.write(occupied_member_mask(snapshot), 32)
                         // Field 6 drives native view +178 (entity send enabled). An absent
                         // delta preserves zero and strands every deletion awaiting this host.
                         && writer.write(1, 1)
                         && writer.write(entity_replication_member_mask(snapshot), 32)
                         && writer.write(0, 1) && writer.write(0, 1);
    std::size_t encodedSize = 0;
    const std::size_t meaningfulBits = meaningful_bit_count(snapshot);
    if (!encoded || writer.bit_count() != meaningfulBits || !writer.finish(encodedSize)
        || encodedSize != size) {
        return false;
    }

    written = encodedSize;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::replicate_membership
