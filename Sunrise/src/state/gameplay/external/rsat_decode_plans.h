#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "../../../middleware/gameplay/external/composite_entity_codec.h"

namespace sunrise::state::gameplay::rsat_decode_plans {
using Digest = std::array<std::byte, 32>;
namespace external = middleware::gameplay::external;
struct PlanRow final {
    std::uint32_t component{}, schema{}, first{}, count{}, bits{}, active{};
};
struct SchemaRow final {
    std::uint32_t handle{}, size{}, first{}, count{};
};
/** Fixed cache-file row sizes make producer and consumer ABI drift fail at compile time. */
inline constexpr std::size_t kPlanRowSize = 24;
inline constexpr std::size_t kSchemaRowSize = 16;
inline constexpr std::size_t kAdditionalSchemaSize = 28;
inline constexpr std::size_t kAdditionalFieldSize = 40;
inline constexpr std::size_t kDecodeEntrySize = 20;
namespace runtime = middleware::bap::activity_message::wire_schema::runtime;
struct AdditionalSchema final {
    std::uint32_t handle{}, original{}, serialized{}, flags{}, array{}, first{}, count{};
};
struct AdditionalField final {
    std::uint32_t originalOffset{}, serializedOffset{}, bitmapOffset{};
    float half{};
    std::uint8_t type{}, presence{};
    std::uint16_t parameter2{};
    std::uint32_t nested{};
    std::int32_t bias{}, width{};
    std::uint32_t parameter3{}, parameter4{};
};
/** Stop readers before loading or destroying a cache; plan spans borrow its storage. */
class Cache final {
public:
    [[nodiscard]] bool load_installed(std::wstring_view artifactDirectory,
                                      const Digest& sdkBuild) noexcept;
    [[nodiscard]] bool
    load(const wchar_t* path, const Digest& sdkBuild, const Digest& evidence) noexcept;
    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }
    [[nodiscard]] bool plan(std::uint32_t component,
                            std::uint32_t schema,
                            external::SobjectDecodePlan& output) const noexcept;
    [[nodiscard]] bool schema(std::uint32_t handle, std::uint32_t& size) const noexcept;
    [[nodiscard]] bool
    field(std::uint32_t handle, std::uint32_t ordinal, std::uint32_t& bit) const noexcept;

    [[nodiscard]] bool additional_schema(std::uint32_t, runtime::SchemaView&) const noexcept;
    [[nodiscard]] bool
    additional_field(std::uint32_t, runtime::FieldView&, std::uint32_t&) const noexcept;

private:
    std::vector<PlanRow> plans_;
    std::vector<external::SobjectDecodeEntry> entries_;
    std::vector<SchemaRow> schemas_;
    std::vector<std::uint32_t> fields_;
    std::vector<AdditionalSchema> additionalSchemas_;
    std::vector<AdditionalField> additionalFields_;
    bool ready_{};
};
[[nodiscard]] bool
resolve_plan(const void*, std::uint32_t, std::uint32_t, external::SobjectDecodePlan&) noexcept;
[[nodiscard]] bool resolve_schema_layout(const void*, std::uint32_t, std::uint32_t&) noexcept;
[[nodiscard]] bool
resolve_field_layout(const void*, std::uint32_t, std::uint32_t, std::uint32_t&) noexcept;
[[nodiscard]] bool
resolve_additional_schema(const void*, std::uint32_t, runtime::SchemaView&) noexcept;
[[nodiscard]] bool
resolve_additional_field(const void*, std::uint32_t, runtime::FieldView&, std::uint32_t&) noexcept;
} // namespace sunrise::state::gameplay::rsat_decode_plans
