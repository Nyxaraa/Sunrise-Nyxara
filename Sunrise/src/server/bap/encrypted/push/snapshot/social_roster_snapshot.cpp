/**
 * Family-two social roster snapshot: the directory and the member record it links to.
 * The panel resolves the emblem through both objects, and a full snapshot prunes every object it
 * does not name, so the pair goes out in one message.
 */

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../state/account/inventory/inventory_state.h"
#include "../../../../../state/build_data/items/item_catalog.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace sunrise::server::bap::encrypted::push::snapshot {
namespace {

/** One line carries the soid, the object count and the encoded size. */
constexpr std::size_t kReportCapacity = 160;

/** Where the member record carries the emblem the panel row reads. */
constexpr std::size_t kEmblemDefinitionOffset = 36;
constexpr std::size_t kEmblemVariantOffset = 38;

/**
 * Absent definition index. The variant field always takes it, because a real value there indexes
 * a table the client then walks with a bogus entry.
 */
constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFFU;

/**
 * Resolves the selected character's equipped emblem to a native definition index.
 * The client reads this object as the account's emblem, so it must track the live loadout.
 * @param account Account snapshot, already read under the lock by the caller.
 * @param index Receives the native definition index of the equipped emblem.
 * @param definitionHash Receives the equipped emblem hash.
 * @return False when nothing is selected, the emblem slot is empty, or the hash is unknown.
 */
[[nodiscard]] bool selected_emblem_definition_index(const state::AccountState& account,
                                                    std::uint16_t& index,
                                                    std::uint32_t& definitionHash) noexcept {
    for (const state::CharacterState& character : account.characters) {
        if (!character.selected) {
            continue;
        }
        const auto& slot =
            character.equipment
                .slots[static_cast<std::size_t>(state::account::inventory::EquipmentSlot::emblem)];
        if (!slot.has_value()) {
            return false;
        }
        state::build_data::items::Definition definition{};
        if (!state::build_data::items::find_hash(slot->definitionHash, definition)) {
            return false;
        }
        index = definition.definitionIndex;
        definitionHash = slot->definitionHash;
        return true;
    }
    return false;
}

} // namespace

/** Builds the family-two snapshot carrying the social roster directory and member record. */
bool prepare_social_roster(Scratch& scratch,
                           const middleware::queuez::Subscription& subscription,
                           std::uint32_t objectId,
                           const Reservation& reservation,
                           Prepared& prepared) noexcept {
    // Both slot ids are resolved here, so the caller's single id is not used.
    (void)objectId;
    const state::AccountState account = state::account_snapshot();
    if (account.primarySoid == 0 || reservation.rawWriteOffset > scratch.plaintext.size()) {
        return report_failure("social_roster_state");
    }
    const auto destination = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    // The body is the directory record followed by the member record.
    constexpr std::size_t kTotal = middleware::datagen::kSocialRosterDirectorySize
                                   + middleware::datagen::kSocialRosterMemberSize;
    if (destination.size() < kTotal) {
        return report_failure("social_roster_storage");
    }

    std::uint16_t emblem = kEmptyDefinitionIndex;
    std::uint32_t emblemHash = 0;
    if (!selected_emblem_definition_index(account, emblem, emblemHash)) {
        emblem = kEmptyDefinitionIndex;
    }

    Prepared staged{};
    std::size_t objectCount = 0;
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t rawUsed = 0;

    /**
     * Writes one object and stages it.
     * Both lookups match on the first qword, so the directory leads with the account soid and
     * repeats it at +8 as the link the member record leads with. Only the member has the emblem.
     */
    const auto emit = [&](std::size_t size, std::uint32_t id, bool directory) noexcept {
        if (objectCount >= staged.objects.size() || size < kEmblemVariantOffset + sizeof emblem) {
            return false;
        }
        const auto body = destination.subspan(rawUsed, size);
        std::fill(body.begin(), body.end(), std::byte{});
        if (directory) {
            std::memcpy(body.data(), &account.primarySoid, sizeof account.primarySoid);
            std::memcpy(body.data() + sizeof account.primarySoid,
                        &account.primarySoid,
                        sizeof account.primarySoid);
        } else {
            std::memcpy(body.data(), &account.primarySoid, sizeof account.primarySoid);
            std::memcpy(body.data() + kEmblemDefinitionOffset, &emblem, sizeof emblem);
            std::memcpy(body.data() + kEmblemVariantOffset,
                        &kEmptyDefinitionIndex,
                        sizeof kEmptyDefinitionIndex);
        }
        std::size_t compressedSize = 0;
        if (!compress_object(scratch,
                             body,
                             id,
                             account.primarySoid,
                             compressedExtent,
                             staged.objects[objectCount],
                             compressedSize)) {
            return false;
        }
        compressedExtent += compressedSize;
        rawUsed += size;
        ++objectCount;
        return true;
    };

    // The directory goes first so a partial land reads as the directory surviving without a member
    // record, rather than as an unexplained miss.
    if (!emit(middleware::datagen::kSocialRosterDirectorySize,
              middleware::datagen::kSocialRosterDirectoryObjectId,
              true)) {
        return report_failure("social_roster_directory");
    }
    if (!emit(middleware::datagen::kSocialRosterMemberSize,
              middleware::datagen::kSocialRosterMemberObjectId,
              false)) {
        return report_failure("social_roster_member");
    }

    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + rawUsed);
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        return report_failure("social_roster_commit");
    }

    std::array<char, kReportCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=queuez stage=social_roster result=ok soid=0x%016llX"
                                      " objects=%zu bytes=%zu emblem=%u hash=0x%08X",
                                      static_cast<unsigned long long>(account.primarySoid),
                                      objectCount,
                                      rawUsed,
                                      static_cast<unsigned>(emblem),
                                      static_cast<unsigned>(emblemHash));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

} // namespace sunrise::server::bap::encrypted::push::snapshot
