#include "queuez_family_staging.h"

#include <cstddef>
#include <limits>

#include "../queuez_state_validation.h"

namespace sunrise::server::bap::encrypted::queuez {

/** @return True for a logical match between two resident rows. */
bool staging::same_resident(const ResidentObject& left, const ResidentObject& right) noexcept {
    return left.objectSoid == right.objectSoid && left.definitionId == right.definitionId;
}

/**
 * Compares two canonical peer states field by field.
 * @return True when both are valid and every fixed Family-4 field matches.
 */
bool staging::same_state(const SessionState& left, const SessionState& right) noexcept {
    if (!valid(left) || !valid(right) || left.family4RootSoid != right.family4RootSoid
        || left.family3RootSoid != right.family3RootSoid
        || left.family4Version != right.family4Version
        || left.family3Version != right.family3Version
        || left.family0Version != right.family0Version
        || left.family0Character != right.family0Character
        || left.family4ResidentCount != right.family4ResidentCount
        || left.family3Phase != right.family3Phase || left.family4Active != right.family4Active
        || left.family3Active != right.family3Active || left.family0Active != right.family0Active) {
        return false;
    }
    for (std::size_t index = 0; index < left.family4Residents.size(); ++index) {
        if (!staging::same_resident(left.family4Residents[index], right.family4Residents[index])) {
            return false;
        }
    }
    return true;
}

/** Stages a first Family-4 manifest, or adopts the manifest a re-snapshot resets the peer to. */
bool stage_family4_snapshot(const SessionState& before,
                            const middleware::queuez::Family& family,
                            SessionState& after) noexcept {
    after = before;
    if (!valid(before) || family.type != kAccountFamilyType || family.rootSoid == 0
        || family.version != kInitialFamilyVersion
        || family.flags != middleware::queuez::kFullSnapshotFlag || family.objects.empty()
        || family.objects.size() > kResidentCapacity
        || family.objects.size()
               > static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())) {
        return false;
    }

    // The Family-3 full snapshot may have been appended immediately before its Family-4 companion.
    // Preserve that independently published ladder while replacing only the Family-4 manifest.
    SessionState candidate = before;
    candidate.family4Residents = {};
    candidate.family4RootSoid = family.rootSoid;
    candidate.family4Version = family.version;
    candidate.family4ResidentCount = static_cast<std::uint16_t>(family.objects.size());
    candidate.family4Active = true;
    for (std::size_t index = 0; index < family.objects.size(); ++index) {
        const middleware::queuez::Object& object = family.objects[index];
        if (object.id == 0 || object.version == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (family.objects[prior].version == object.version) {
                return false;
            }
        }
        candidate.family4Residents[index] = ResidentObject{object.version, object.id};
    }
    if (candidate.family4Residents.front().objectSoid != family.rootSoid) {
        return false;
    }
    if (!valid(candidate)) {
        return false;
    }
    if (!before.family4Active && before.family3Phase != Family3Phase::normal) {
        return false;
    }
    // A full snapshot resets the Client's record to the version it carries, so a re-snapshot has to
    // move our mirror there too. Holding the old version leaves the next incremental one ahead of
    // the record, which the Client refuses with queuez error 6.
    after = candidate;
    return true;
}

/** Stages the family-zero publication policy. */
bool stage_family0_subscription(const SessionState& before,
                                std::uint64_t selectedCharacter,
                                bool& publish,
                                bool& incremental,
                                SessionState& after) noexcept {
    publish = false;
    incremental = false;
    after = before;
    if (!valid(before) || selectedCharacter == 0) {
        return false;
    }
    if (!before.family0Active) {
        publish = true;
        after.family0Active = true;
        after.family0Character = selectedCharacter;
        after.family0Version = kInitialFamilyVersion;
        return true;
    }
    if (before.family0Character == selectedCharacter) {
        return true;
    }
    if (before.family0Version == (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    publish = true;
    incremental = true;
    after.family0Character = selectedCharacter;
    after.family0Version = before.family0Version + 1;
    return true;
}

/** Stages the measured Family-3 subscription reset: full first, then response-only. */
bool stage_family3_subscription(const SessionState& before,
                                const middleware::queuez::Subscription& subscription,
                                bool& publish,
                                SessionState& after) noexcept {
    publish = false;
    after = before;
    if (!valid(before) || subscription.familyType != kRosterFamilyType
        || subscription.familyRootSoid == 0) {
        return false;
    }
    if ((before.family4Active && subscription.familyRootSoid != before.family4RootSoid)
        || (before.family3Active && subscription.familyRootSoid != before.family3RootSoid)) {
        return false;
    }
    if (!before.family3Active) {
        // Publication is transactional: the caller installs this seed only after the full frame is
        // copied. Until then the before-image remains inactive and version zero has no meaning.
        publish = true;
        after.family3RootSoid = subscription.familyRootSoid;
        after.family3Version = kInitialFamilyVersion;
        after.family3Active = true;
        return valid(after);
    }
    if (before.family3Phase == Family3Phase::normal) {
        publish = true;
        // An explicit subscription establishes a fresh client-side store. Its current full body is
        // version zero even when the prior subscribed store had consumed incrementals.
        after.family3Version = kInitialFamilyVersion;
        return valid(after);
    }
    if (!before.family4Active) {
        return false;
    }
    if (before.family3Phase == Family3Phase::publishOnce) {
        publish = true;
        after.family3Version = kInitialFamilyVersion;
        after.family3Phase = Family3Phase::responseOnly;
        return valid(after);
    }
    return before.family3Phase == Family3Phase::responseOnly;
}

/**
 * Clears the one named family from this peer's mirror.
 * @param before Mirror visible to the peer.
 * @param familyType Family the request named, at the same body offset svc 12 uses.
 * @param familyRootSoid Root the request named.
 * @param after Receives the mirror with that family released.
 */
void stage_unsubscription(const SessionState& before,
                          std::uint32_t familyType,
                          std::uint64_t familyRootSoid,
                          SessionState& after) noexcept {
    after = before;
    // Families 0, 3 and 4 all key on the account soid, so the root alone does not name a family.
    // Releasing one must not take the other two with it. Family zero has no root of its own: it is
    // published under the account root family four retains, so that root is what names its record.
    if (familyType == kAccountFamilyType && before.family4Active
        && familyRootSoid == before.family4RootSoid) {
        after.family4Active = false;
        after.family4RootSoid = 0;
        after.family4Version = kInitialFamilyVersion;
        after.family4Residents = {};
        after.family4ResidentCount = 0;
    } else if (familyType == kRosterFamilyType && before.family3Active
               && familyRootSoid == before.family3RootSoid) {
        after.family3Active = false;
        after.family3RootSoid = 0;
        after.family3Version = kInitialFamilyVersion;
        after.family3Phase = Family3Phase::normal;
    } else if (familyType == kBannerFamilyType && before.family0Active && familyRootSoid != 0
               && familyRootSoid == before.family4RootSoid) {
        after.family0Active = false;
        after.family0Version = kInitialFamilyVersion;
        after.family0Character = 0;
        after.pendingBannerRoot = 0;
    }
}

} // namespace sunrise::server::bap::encrypted::queuez
