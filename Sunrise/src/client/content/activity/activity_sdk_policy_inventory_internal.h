#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "activity_sdk_policy_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal {

/** One capability's fixed gate sets produce at most this many unique refusal reasons. */
inline constexpr std::size_t kMaximumFailureReasonCount = 16;

/** These stable strings are scalar values in canonical format-v12 policy rows. */
inline constexpr std::string_view kExact = "exact";
inline constexpr std::string_view kPassed = "passed";
inline constexpr std::string_view kFailed = "failed";
inline constexpr std::string_view kUnresolved = "unresolved";
inline constexpr std::string_view kInspect = "inspect";
inline constexpr std::string_view kPanelTest = "panel_test";
inline constexpr std::string_view kScript = "script";
inline constexpr std::string_view kCandidate = "candidate";
inline constexpr std::string_view kRefused = "refused";

/** One host surface paired with the sorted subject row that owns it. */
struct HostModel final {
    const HostSurface* surface{};
    std::uint32_t subjectIndex{};
};

/** Supports transparent lookup in the snapshot's owned deferred string index. */
struct StringHash final {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept;
};

/** Compares owned and borrowed deferred strings without temporary allocations. */
struct StringEqual final {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept;
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const noexcept;
    [[nodiscard]] bool operator()(const std::string& left, std::string_view right) const noexcept;
    [[nodiscard]] bool operator()(std::string_view left, const std::string& right) const noexcept;
};

/** Collects one capability's bounded refusal reasons before canonical sorting. */
struct FailureReasons final {
    std::array<std::string_view, kMaximumFailureReasonCount> values{};
    std::size_t count{};

    [[nodiscard]] bool add(std::string_view value) noexcept;
    void canonicalize() noexcept;
};

/** One object's complete scenario-local occurrence cardinality. */
struct RouteEvidence final {
    bool hasScenario{};
    bool ambiguous{};
    std::uint32_t invalidOccurrences{};
};

/** @return True when left precedes right by unsigned UTF-8 byte order. */
[[nodiscard]] bool byte_less(std::string_view left, std::string_view right) noexcept;

/** @return True when count can be added to current without exceeding maximum. */
[[nodiscard]] bool can_add(std::size_t current, std::size_t count, std::size_t maximum) noexcept;

/** Converts one owned row interval while retaining the current cursor for an empty range. */
[[nodiscard]] bool make_range(std::size_t first, std::size_t count, format::Range& output) noexcept;

/** @return True for an empty optional value or one bounded canonical UTF-8 value. */
[[nodiscard]] bool valid_text(std::string_view value) noexcept;

/** Checks parent order, identity, bounds, and every borrowed UTF-8 value. */
[[nodiscard]] bool valid_inputs(const Inputs& inputs);

/** Builds complete object route evidence without retaining occurrence strings. */
[[nodiscard]] bool build_routes(const Inputs& inputs, std::vector<RouteEvidence>& output);

/** Owns one pending policy projection and its non-final string pool. */
class Builder final {
public:
    Builder(const Inputs& inputs, std::span<const RouteEvidence> routes);

    /** Builds every policy section in canonical ownership order. */
    [[nodiscard]] bool run();

    /** @return The finished snapshot, moved out of the builder. */
    [[nodiscard]] Snapshot take() noexcept;

private:
    /** Reserves input-derived estimates without imposing one captured estate size. */
    void reserve();

    /** Interns one borrowed value into the snapshot-owned deferred string pool. */
    [[nodiscard]] bool intern(std::string_view value, Text& output);

    /** Appends one deferred text row for one interned value. */
    [[nodiscard]] bool add_text(std::string_view value, format::TextKind kind);

    /** @return True after activity and sorted slot aliases are emitted in parent order. */
    [[nodiscard]] bool build_aliases();

    /** Appends one canonical gate and records a failed reason for later refusal rows. */
    [[nodiscard]] bool add_gate(std::string_view name,
                                bool passed,
                                std::string_view reason,
                                std::string_view wouldConfirm,
                                FailureReasons& failures);

    /** Appends one refusal and its sorted unique Text-owned reason rows. */
    [[nodiscard]] bool add_refusal(std::string_view capabilityId,
                                   std::string_view exposure,
                                   std::string_view status,
                                   FailureReasons& failures,
                                   std::uint32_t capabilityIndex);

    /** Initializes one capability and assigns its future canonical row index. */
    [[nodiscard]] bool begin_capability(std::string_view subjectId,
                                        std::string_view operation,
                                        std::string_view valueSchemaId,
                                        format::SubjectKind subjectKind,
                                        std::uint32_t subjectIndex,
                                        std::uint32_t exposureFlags,
                                        std::uint32_t candidateExposureFlags,
                                        Capability& capability,
                                        std::string& capabilityId,
                                        std::uint32_t& capabilityIndex);

    /** Closes and appends one capability after all owned child rows are present. */
    [[nodiscard]] bool
    finish_capability(Capability& capability, std::size_t firstGate, std::size_t firstRefusal);

    /** @return True after every activity mission-binding capability is emitted in row order. */
    [[nodiscard]] bool build_activity_capabilities();

    /** Emits the always-visible inspection capability for one final slot. */
    [[nodiscard]] bool build_inspect_capability(const SlotInput& input, std::uint32_t slotIndex);

    /** Emits one device or trigger operation from final slot and route evidence. */
    [[nodiscard]] bool build_adapter_capability(const SlotInput& input,
                                                std::uint32_t slotIndex,
                                                std::string_view operation);

    /** @return True after every slot capability group is emitted in canonical operation order. */
    [[nodiscard]] bool build_slot_capabilities();

    /** Emits one compiled host surface using its implemented adapter gate set. */
    [[nodiscard]] bool build_host_capability(const HostModel& model);

    /** @return True after host subjects and operations are emitted in byte-sorted order. */
    [[nodiscard]] bool build_host_capabilities();

    const Inputs& inputs_;
    std::span<const RouteEvidence> routes_{};
    Snapshot output_{};
    std::unordered_map<std::string, std::uint32_t, StringHash, StringEqual> stringIndexes_{};
};

} // namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal
