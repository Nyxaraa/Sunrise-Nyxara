#pragma once

namespace sunrise::server::bap::encrypted::push::queuez_report {

/** Reports one subscription step that produced no frame. */
void subscription_failure(const char* step) noexcept;

/** Reports one subscription step that did not record. */
void subscription_state(const char* step) noexcept;

} // namespace sunrise::server::bap::encrypted::push::queuez_report
