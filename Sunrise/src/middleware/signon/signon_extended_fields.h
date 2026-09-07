#pragma once

#include "internal.h"

namespace sunrise::middleware::signon::extended {

/**
 * Appends the optional SignOn success extended sub-message, which carries the network id.
 * Fields 14 to 16 are city, country and ISP text on a real server, not URLs, and nothing reads
 * them, so they are not sent.
 * @param success Writer bound to the success sub-message storage.
 * @return True when the sub-message fits.
 */
[[nodiscard]] bool append(Writer& success) noexcept;

} // namespace sunrise::middleware::signon::extended
