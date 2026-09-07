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
 * Runs the emote-collection canonicalization early; the snapshot path runs it again behind its
 * own preflight.
 * @return False only when the account itself could not be updated.
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
        const bool persisted = state::ensure_profile_item_identities()
                               && state::ensure_character_subclasses() && emote_collection_settled()
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
                          && state::ensure_character_subclasses() && emote_collection_settled()
                          && state::build_data::persist();
    // The overlay ends with the work, not with the slice, so it spans every retry the pass needs.
    if (complete) {
        core::ui::busy::end(core::ui::busy::Task::contentExtraction);
    }
    return complete;
}

} // namespace sunrise::client::content::investment
