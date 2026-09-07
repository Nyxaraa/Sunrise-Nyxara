#pragma once

#include "definition.h"

namespace sunrise::server::bap::encrypted {

struct ServiceOutcome;

namespace transactions {

/**
 * Commits at most one delayed State transaction.
 * @param outcome Checked service result whose pending transaction is used up.
 * @param publication Gets connection fields to publish after the output copy.
 * @param reason Names the branch that refused, so a discarded frame says which one.
 * @return True when there is no mutation, or the one mutation commits.
 */
[[nodiscard]] bool
commit(ServiceOutcome& outcome, Publication& publication, const char*& reason) noexcept;

} // namespace transactions

} // namespace sunrise::server::bap::encrypted
