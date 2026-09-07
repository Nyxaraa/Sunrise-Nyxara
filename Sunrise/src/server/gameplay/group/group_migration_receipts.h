#pragma once

#include <cstdint>

#include "../../../middleware/encoding/bit_reader.h"
#include "../../../state/gameplay/definition.h"

namespace sunrise::server::gameplay::group::migration {

/**
 * Reads one host-migration or election message and preserves the private logical host.
 * An election is answered only for the exact endpoint that owns the private group.
 * @param from Peer endpoint in host order.
 * @param id Registry message id.
 * @param reader Reader positioned at the body.
 * @return True when the id is one of these messages and its body was completely read.
 */
[[nodiscard]] bool consume(const state::gameplay::Endpoint& from,
                           std::uint8_t id,
                           middleware::encoding::bits::Reader& reader) noexcept;

} // namespace sunrise::server::gameplay::group::migration
