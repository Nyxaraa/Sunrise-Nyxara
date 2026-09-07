#include "scriptable_catalog_container_index.h"

#include <algorithm>
#include <span>

#include "../../../middleware/content/packages/tables/component_container_reader.h"
#include "../spawn_sets/spawn_set_catalog_builder.h"

namespace sunrise::client::content::activity::scriptables::internal {
namespace {

namespace package_reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

/** Bounds the retained sweep so a malformed install cannot grow the index without limit. */
constexpr std::size_t kEntryCapacity = 262'144;

struct SweepContext final {
    ContainerIndex* output{};
    ContainerIndexCancelCheck cancel{};
    bool failed{};
};

[[nodiscard]] bool cancelled(ContainerIndexCancelCheck cancel) noexcept {
    return cancel != nullptr && cancel();
}

/** Keeps one container tag and the normalized stem of the package family holding it. */
[[nodiscard]] bool collect_entry(void* opaque, const package_reader::ClassEntry& entry) noexcept {
    auto& context = *static_cast<SweepContext*>(opaque);
    if (context.output == nullptr || cancelled(context.cancel)) {
        context.failed = true;
        return false;
    }
    if (context.output->entries.size() >= kEntryCapacity) {
        context.failed = true;
        return false;
    }
    ContainerIndexEntry row{};
    row.tag = entry.tag;
    row.stemValid = spawn_sets::normalize_stem(entry.packageFamily, row.stem, row.stemLength);
    if (!row.stemValid) {
        context.output->stemsComplete = false;
    }
    try {
        context.output->entries.push_back(row);
    } catch (...) {
        context.failed = true;
        return false;
    }
    return true;
}

} // namespace

/** Sweeps the install once for the container class and keeps each tag with its stem. */
bool build_container_index(const package_reader::Source& source,
                           ContainerIndex& output,
                           ContainerIndexCancelCheck cancel) noexcept {
    output = {};
    output.stemsComplete = true;
    try {
        output.entries.reserve(4'096);
    } catch (...) {
        return false;
    }
    SweepContext context{&output, cancel, false};
    package_reader::ScanResult scan{};
    const bool scanned = package_reader::scan_class_entries(
        source.directory, tables::kContainerClass, &collect_entry, &context, scan);
    if (context.failed || cancelled(cancel)) {
        return false;
    }
    output.complete = scanned;
    return true;
}

} // namespace sunrise::client::content::activity::scriptables::internal
