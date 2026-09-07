#include "entity_object_type_build.h"

#include <algorithm>
#include <cstring>
namespace sunrise::client::content::activity::entity_object_types {
namespace {
namespace reader = middleware::content::packages::reader;
namespace types = state::gameplay::entity_object_types;
/** Package classes and offsets define the reciprocal RSAT/class join. */
constexpr std::uint32_t kRsatClass = 0x80809BB6U, kDefinitionClass = 0x80809C0FU;
constexpr std::size_t kReverseOffset = 8, kForwardOffset = 136, kObjectTypeOffset = 150;
struct Context {
    const reader::Source& source;
    reader::Scratch& scratch;
    types::Rows rows;
};
/** The native class definition stores its RSAT at 136 and its object type at 150. */
bool collect(void* opaque, std::uint32_t rsat) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    try {
        if (context.rows.size() >= types::kMaximumRows) {
            return false;
        }
        std::vector<std::byte> resource, definition;
        std::uint32_t cls{}, backlink{}, forward{};
        if (!reader::read_tag(context.source, context.scratch, rsat, resource, cls)
            || cls != kRsatClass || resource.size() < kReverseOffset + sizeof(std::uint32_t)) {
            return false;
        }
        std::memcpy(&backlink, resource.data() + kReverseOffset, sizeof backlink);
        if (!backlink || backlink == 0xFFFFFFFFU
            || !reader::read_tag(context.source, context.scratch, backlink, definition, cls)
            || cls != kDefinitionClass || definition.size() <= kObjectTypeOffset) {
            return false;
        }
        std::memcpy(&forward, definition.data() + kForwardOffset, sizeof forward);
        const auto objectType = std::to_integer<std::uint8_t>(definition[kObjectTypeOffset]);
        if (forward != rsat || objectType > types::kMaximumObjectType) {
            return false;
        }
        context.rows.push_back({rsat, backlink, objectType});
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace
/** No partial or ambiguous package-class catalogue is published. */
bool build(const reader::Source& source,
           reader::Scratch& scratch,
           const types::Fingerprint& fingerprint) noexcept {
    try {
        Context context{source, scratch, {}};
        reader::ScanResult result{};
        if (!reader::scan_class(source.directory, kRsatClass, &collect, &context, result)) {
            return false;
        }
        std::sort(context.rows.begin(), context.rows.end(), [](const auto& a, const auto& b) {
            return a.rsatTag < b.rsatTag;
        });
        return types::publish(std::move(context.rows), fingerprint);
    } catch (...) {
        return false;
    }
}
} // namespace sunrise::client::content::activity::entity_object_types
