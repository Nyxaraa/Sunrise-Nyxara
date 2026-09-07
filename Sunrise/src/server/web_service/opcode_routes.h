#pragma once

#include <cstdint>

#include "../../middleware/web_service/web_service_envelope.h"

namespace sunrise::server::web_service {

/**
 * Finds reusable stateless layouts outside the special message codecs.
 * Every opcode gets one. An unlisted one takes the correlated echo.
 * @param opcode Request opcode from the Web Service envelope.
 * @param shape Gets the reusable response layout.
 */
void resolve_response_shape(std::uint16_t opcode,
                            middleware::web_service::ResponseShape& shape) noexcept;

/**
 * Reports whether a reply's status value feeds the Client's Family-4 version wait.
 * Only these response definitions have it read as a revision barrier. The rest are given -1 by
 * the Client itself, so their value has no reader and keeps the descriptor's zero.
 * @param opcode Request opcode from the Web Service envelope.
 * @return True when the status value must name a revision or the no-publication constant.
 */
[[nodiscard]] bool awaits_family4_version(std::uint16_t opcode) noexcept;

} // namespace sunrise::server::web_service
