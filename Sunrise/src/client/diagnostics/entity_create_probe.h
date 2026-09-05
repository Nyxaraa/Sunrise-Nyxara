#pragma once

namespace sunrise::client::diagnostics {

/**
 * Reports which half of the client's entity creation refuses.
 * The client logs `failed to create '<type>' entity` and nothing else, and Sunrise's own note at
 * `server/bap/encrypted/transactions/service_outcome_commit.cpp` reads that as "it has no free
 * index". That reading is an assumption, and acting on it once already cost a build-and-run cycle:
 * a lease top-up landed and changed nothing.
 *
 * The creator calls two things in order — an index allocator that answers -1 when it has nothing
 * to give, then an initialiser that answers false when it refuses the entity it was handed. Both
 * end at the same log line, so the line cannot tell them apart. These two detours can: each
 * reports its own outcome, so one run says which half is failing and the guessing stops.
 *
 * Diagnostic only. Neither replacement changes an argument or a result, and both are found with
 * an independent scan rather than through the shared target registry, so a signature that no
 * longer matches this build costs the probe and nothing else.
 * @param stockUnstockedPool Refill a bitmap that is entirely unstocked, which is what the
 *        client's own initialiser would have done had its role global read zero.
 * @param restockAlways Also refill a pool that has drained, not only one never stocked. Needed to
 *        get past an encounter bubble the drained pool would otherwise refuse. Safe: the refill
 *        spares every index the probe watched the allocator hand out, so it cannot re-free one that
 *        is still owned the way the earlier blanket fill did.
 * @return True when the probe attached.
 */
[[nodiscard]] bool install_entity_create_probe(bool stockUnstockedPool,
                                               bool restockAlways) noexcept;

/** Detaches the entity-creation probes. */
void uninstall_entity_create_probe() noexcept;

} // namespace sunrise::client::diagnostics
