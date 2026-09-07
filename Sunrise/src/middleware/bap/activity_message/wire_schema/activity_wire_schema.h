#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sunrise::middleware::bap::activity_message::wire_schema {

/** Rows the generated catalog holds. Every table is sized to these exactly. */
inline constexpr std::size_t kMessageCount = 59;
inline constexpr std::size_t kFieldCount = 786;
/** All-ones is the absent value for an unsigned catalog field. */
inline constexpr std::uint32_t kAbsentUnsigned = 0xFFFFFFFFU;

/** How much of a packet's layout the catalog fixes. */
enum class LayoutKind : std::uint8_t {
    reflection,
    authored,
    packageSelected,
    runtimeSelected,
    reflectionWithRawTail,
    absent,
};

/** How the root reflection walk starts. */
enum class CallForm : std::uint8_t {
    direct = 1,
    deltaRootBit = 2,
};

/** Direction encoded by the format-v10 activity-message row. */
enum class Direction : std::uint8_t {
    remoteToClient = 1,
    clientToRemote = 2,
    bidirectional = 3,
    clientToRemoteSpecial = 4,
    nameOnly = 5,
};

/** Coverage encoded by the format-v10 activity-message row. */
enum class Coverage : std::uint8_t {
    fixedWireExact = 1,
    customWireExact = 2,
    partialDynamicBody = 3,
    variableWire = 4,
    serviceConversion = 5,
    nameOnly = 6,
};

/** Definition provenance encoded by the format-v10 activity-message row. */
enum class DefinitionState : std::uint8_t {
    none = 1,
    authored = 2,
    graph = 3,
};

/** Field provenance encoded by the format-v10 activity-message-field row. */
enum class FieldSource : std::uint8_t {
    graph = 1,
    authored = 2,
};

/** Field-name evidence encoded by the format-v10 activity-message-field row. */
enum class FieldConfidence : std::uint8_t {
    verified = 1,
    assumed = 2,
    unnamed = 3,
};

/** Whether one decoded scalar may enter the retained operator-visible sidecar. */
enum class FieldExposure : std::uint8_t {
    operatorValue,
    provisionalValue,
    redacted,
};

/** One activity message type from the executable's generated table. */
struct MessageDescriptor final {
    std::uint32_t id{};
    std::string_view name{};
    Direction direction{Direction::nameOnly};
    std::string_view carriers{};
    std::uint32_t definitionHandle{kAbsentUnsigned};
    std::string_view wireShape{};
    std::string_view statePrerequisites{};
    std::string_view authorRules{};
    std::string_view sender{};
    std::string_view handler{};
    std::int64_t wireMinBits{-1};
    std::int64_t wireMaxBits{-1};
    std::int64_t effectiveMinBits{-1};
    std::int64_t effectiveMaxBits{-1};
    std::uint16_t firstField{};
    std::uint16_t fieldCount{};
    std::uint16_t namedFieldCount{};
    std::uint16_t graphFieldCount{};
    std::uint16_t authoredFieldCount{};
    std::uint32_t definitionStructSize{kAbsentUnsigned};
    Coverage coverage{Coverage::nameOnly};
    DefinitionState definitionState{DefinitionState::none};
    LayoutKind layout{LayoutKind::absent};
    CallForm callForm{CallForm::direct};
    bool clientSends{};
    bool clientHandles{};
};

/** One flattened reflection or authored-grammar field. */
struct FieldDescriptor final {
    std::uint16_t messageId{};
    std::uint16_t catalogIndex{};
    std::string_view path{};
    std::string_view name{};
    std::string_view meaning{};
    std::string_view type{};
    std::string_view source{};
    std::int32_t structOffset{};
    std::int32_t absoluteStructOffset{};
    std::int32_t bits{};
    std::int32_t bitsMin{};
    std::int32_t bitsMax{};
    std::int64_t bias{};
    std::int32_t widthOrCountOffset{};
    std::uint16_t repeat{1};
    std::uint8_t typeCode{};
    std::uint8_t depth{};
    bool presenceBit{};
    FieldExposure exposure{FieldExposure::redacted};
    FieldSource sourceKind{FieldSource::graph};
    FieldConfidence confidence{FieldConfidence::unnamed};
    std::uint32_t nestedHandle{kAbsentUnsigned};
    std::uint32_t ownerHandle{kAbsentUnsigned};
    bool typeCodePresent{};
    bool biasPresent{};
    bool coined{};
    bool documented{};
    bool repeatedBlock{};
};

/** @return All server-authored activity message descriptors in message-id order. */
[[nodiscard]] std::span<const MessageDescriptor> messages() noexcept;

/** @return Every flattened field row of every message, in message-id order. */
[[nodiscard]] std::span<const FieldDescriptor> all_fields() noexcept;

/** @return Descriptor for one message id, or null outside 0..58. */
[[nodiscard]] const MessageDescriptor* find_message(std::uint32_t id) noexcept;

/** @return The flattened fields belonging to one descriptor. */
[[nodiscard]] std::span<const FieldDescriptor> fields(const MessageDescriptor& message) noexcept;

/** The authored meaning and source columns for one field. Both are empty when no grant names it. */
struct FieldDocumentation final {
    std::string_view meaning{};
    std::string_view source{};
};

/** @return The authored documentation grant for one exact message id and field path. */
[[nodiscard]] FieldDocumentation field_documentation(std::uint32_t messageId,
                                                     std::string_view path) noexcept;

/** @return The layout one message row carries, from its provenance, coverage and identity. */
[[nodiscard]] LayoutKind
layout_of(std::uint32_t messageId, DefinitionState state, Coverage coverage) noexcept;

[[nodiscard]] const char* layout_name(LayoutKind layout) noexcept;
[[nodiscard]] const char* call_form_name(CallForm form) noexcept;

} // namespace sunrise::middleware::bap::activity_message::wire_schema
