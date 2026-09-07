#pragma once

#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::group {

/** The soid pair a 232-byte player block carries at its `+192` field. */
struct PlayerBlockSoids {
    bool present{};
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
};

/**
 * Reads a full-mode 232-byte player block up to and including its soid pair.
 * Fields after the pair stay unread, so the reader does not end at the block's end.
 * @param reader Reader positioned at the block.
 * @param output Receives the pair, with `present` clear when the block omits it.
 * @return True when every presence bit and present field up to the pair was available.
 */
[[nodiscard]] bool read_player_block_soids(encoding::bits::Reader& reader,
                                           PlayerBlockSoids& output) noexcept;

/**
 * Writes a full-mode 232-byte player block carrying the soid pair and nothing else.
 * Every other field stays absent, which leaves the receiver's cleared row untouched.
 * @param writer Open writer.
 * @param soids Pair to publish.
 * @return True when every field fit.
 */
[[nodiscard]] bool write_player_block_soids(encoding::bits::Writer& writer,
                                            const PlayerBlockSoids& soids) noexcept;

/** Writes a 136-byte player delta with every field absent. @return True when it fit. */
[[nodiscard]] bool write_player_block_delta_absent(encoding::bits::Writer& writer) noexcept;

/** Writes a 20-byte player tail with every field cleared. @return True when it fit. */
[[nodiscard]] bool write_player_tail_cleared(encoding::bits::Writer& writer) noexcept;

} // namespace sunrise::middleware::gameplay::group
