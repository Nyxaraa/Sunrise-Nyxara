#pragma once
#include <cstdint>
#include <span>

namespace sunrise::client::hooks::mission_retirement {
struct RequestId {
    std::uint64_t session{}, binding{}, revision{};
    bool operator==(const RequestId&) const = default;
};
enum class Status { absent, baselinePending, retiring, complete, failed, failedRetiring };
struct Progress {
    Status value{Status::absent};
    std::int32_t sourceRegion{-1};
    std::uintptr_t owner{};
    void observe(std::int32_t region, std::uintptr_t observedOwner,
        std::uint32_t groupCount, std::size_t matchingKeys) noexcept {
        if (region != sourceRegion || !observedOwner || !groupCount || groupCount > 128) return;
        if (value == Status::baselinePending && matchingKeys > 0) {
            owner = observedOwner; value = Status::retiring;
        } else if (value == Status::retiring && owner == observedOwner && matchingKeys == 0) {
            value = Status::complete;
        }
    }
};
// Keys remain at their wire ordinals. The client observes them before and after removal.
Status prepare(RequestId id, std::int32_t sourceRegion,
    std::span<const std::uint32_t> keys) noexcept;
Status status(RequestId id) noexcept;
// Called only from the existing native frame observation point; never changes game memory.
void poll(std::int32_t currentRegion) noexcept;
}
