#include <Windows.h>

#include <mutex>

#include "../../../core/ui/busy/busy.h"
#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../items/packages/build.h"
#include "core/threading/srw_lock.h"
#include "internal.h"
#include "runtime.h"

namespace sunrise::client::content::investment {
namespace {

core::threading::SrwLock g_refreshLock{};

[[nodiscard]] bool ready() noexcept {
    return state::build_data::named_catalog_ready() && items::packages::ready();
}

/**
 * Runs the emote-collection canonicalization on the extraction path, where it is an opportunistic
 * head start rather than a precondition: the snapshot path runs the same step behind its own
 * preflight, so nothing here is the last chance to apply it.
 * @return False only when the account itself could not be updated, which is the one outcome that
 * says something is wrong rather than merely unfinished. A build that cannot carry the item, and
 * one whose data is still being extracted, both leave the cache worth writing.
 */
[[nodiscard]] bool emote_collection_settled() noexcept {
    return state::ensure_character_emote_collection() != state::EmoteCollectionOutcome::failed;
}

} // namespace

/** @return True when the next refresh slice needs a visible overlay for a package sweep. */
bool requires_package_sweep() noexcept {
    return !state::build_data::item_definitions_ready() && items::packages::readable();
}

/** Publishes every installed equipment mapping domain. */
bool refresh() noexcept {
    if (ready()) {
        // The same lock as the extraction path. A cache write holds its own lock across file
        // calls, so a held thread stopped inside one would deadlock the freeze below.
        const std::lock_guard lock(g_refreshLock);
        // The vendor catalog is deliberately not part of `ready()` - a boot without vendors is
        // still a boot - but a restored cache can carry every mapping domain and no catalog,
        // because the boot that wrote it lost the vendor pass. Every domain in `ready()` retries
        // through the pass below until it publishes; this is the one domain that gate skips, so
        // it gets one retry here. Once per session, because a pass that failed against these
        // packages will keep failing against them, and its own log lines already say why.
        static bool vendorRetryDone = false;
        if (!vendorRetryDone && !state::build_data::vendor_catalog_ready()
            && items::packages::readable()) {
            vendorRetryDone = true;
            (void)items::packages::build();
        }
        const bool persisted = state::ensure_profile_item_identities()
                               && state::ensure_character_subclasses()
                               && emote_collection_settled()
                               && state::build_data::persist();
        // Nothing reads a package again until the next boot, so the open files and the held
        // tables go back now rather than at process exit.
        middleware::content::packages::reader::release_caches();
        core::ui::busy::end(core::ui::busy::Task::contentExtraction);
        return persisted;
    }

    const std::lock_guard lock(g_refreshLock);
    // The package pass creates parallel readers. Suspending the client while those threads start
    // can block their DLL thread-attach work behind a suspended owner, so the visible preflight
    // runs one frame early and extraction proceeds with the process live.
    core::ui::busy::raise(core::ui::busy::Task::contentExtraction);
    // The package pass owns the item table and must not wait on runtime content lookups.
    (void)items::packages::build();
    const bool domainsReady = ready();
    const bool complete = domainsReady && state::ensure_profile_item_identities()
                          && state::ensure_character_subclasses()
                          && emote_collection_settled()
                          && state::build_data::persist();
    // The overlay ends with the work, not with the slice, so it spans every retry the pass needs.
    if (complete) {
        core::ui::busy::end(core::ui::busy::Task::contentExtraction);
    }
    return complete;
}

} // namespace sunrise::client::content::investment
