#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>

#include "../../../../core/logging/log.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/content/content_catalog.h"
#include "../../../content/handles/handle_resolver.h"
#include "../../../content/investment/internal.h"
#include "../../../content/investment/layout.h"
#include "../../../content/items/layout.h"
#include "../../../memory/current_process_memory.h"
#include "../../../targets/game/content.h"
#include "internal.h"
#include "socket_row_relocation.h"

namespace sunrise::client::hooks::network::investment {
namespace {

namespace content_handles = client::content::handles;
namespace content_investment = client::content::investment;
namespace item_layout = client::content::items::layout;

/** FNV-1 name hash shared by the installed investment-globals candidates. */
constexpr std::uint32_t kInvestmentGlobalsNameHash = 0x6F7125CBU;
constexpr std::size_t kBootstrapCandidateCapacity = 64;

/** Exact installed item identities used to validate a candidate investment root. */
constexpr std::uint32_t kLegArmorReferenceHash = 3'213'968'579U;
constexpr std::array<std::uint32_t, 4> kArrivalsLegModHashes{
    3'465'659'109U, // Flourishing Blade
    3'465'659'111U, // Automatic Prize
    3'465'659'104U, // Dimensional Tithes
    3'465'659'105U, // Ascendant Bounty
};

/** Installed reusable-set layout and the two rows proved by package extraction. */
constexpr std::size_t kTableArrayDescriptorOffset = 8;
constexpr std::size_t kPlugSetRowStride = 24;
constexpr std::size_t kPlugSetMemberDescriptorOffset = 8;
constexpr std::size_t kPlugMemberStride = 32;
constexpr std::size_t kArrayMarkerSize = 4;
constexpr std::size_t kArrayHeaderSize = 16;
constexpr std::size_t kGeneralSetIndex = 8;
constexpr std::size_t kLegSetIndex = 14;
constexpr std::uint64_t kGeneralMemberCount = 19;
constexpr std::uint64_t kLegMemberCount = 52;
constexpr std::size_t kMaximumSetCount = 4096;
constexpr std::size_t kMaximumMemberCount = 4096;
constexpr std::uintptr_t kMaximumLowAddress = UINT32_MAX;
constexpr std::size_t kLowArenaSize = 64U * 1024U;
constexpr std::size_t kArenaAlignment = 16U;
constexpr std::size_t kPlugBlockOffset = 0x184;
constexpr std::size_t kPlugCategoryOffset = 4;
constexpr std::size_t kPlugBlockSize = 64;
constexpr unsigned kCategoryAttemptLimit = 120;
constexpr ULONGLONG kCategoryRetryIntervalMs = 250;
constexpr ULONGLONG kCategoryRetryWindowMs = 30000;

struct ArrayDescriptor {
    std::uint64_t count{};
    std::int64_t relative{};

    friend bool operator==(const ArrayDescriptor&, const ArrayDescriptor&) = default;
};

struct ArrayView {
    std::uintptr_t descriptor{};
    std::uintptr_t header{};
    std::uintptr_t data{};
    std::uint64_t count{};
    std::uint32_t elementClass{};
};

struct LocatedSets {
    content_investment::Source source{};
    ArrayView general{};
    ArrayView legs{};
};

struct Allocation {
    std::byte* base{};
    std::size_t size{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return base != nullptr && size != 0;
    }
};

struct AppliedSet {
    std::uintptr_t descriptor{};
    ArrayDescriptor original{};
    ArrayDescriptor replacement{};
};

struct CategoryPatch {
    std::uintptr_t address{};
    std::uint32_t original{};
};
constexpr std::uint32_t kGeneralCategory = 0x94493B9BU;
constexpr std::uint32_t kLegCategory = 0x7DDE0206U;
std::array<CategoryPatch, 4> g_categories{};
std::size_t g_categoryCount{};

enum class Failure : std::uint8_t {
    none,
    buildData,
    targets,
    source,
    ambiguous,
    allocation,
    write,
    verification,
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::atomic_bool g_armed{false};
std::array<Allocation, 2> g_allocations{};
std::array<AppliedSet, 2> g_applied{};
std::size_t g_appliedCount{};
Failure g_lastFailure{Failure::none};
std::byte* g_lowArena{};
std::size_t g_lowArenaUsed{};
std::array<std::array<relocation::Row, relocation::kMaximumMembers>, 2> g_expected{};
std::array<std::size_t, 2> g_expectedCounts{};
content_investment::Source g_categorySource{};
std::array<std::uint16_t, 4> g_categoryRoutes{};
std::uint16_t g_categoryReference{};
unsigned g_categoryAttempts{};
ULONGLONG g_categoryNext{};
ULONGLONG g_categoryDeadline{};
const char* g_categoryFailure = "none";

[[nodiscard]] const char* failure_name(Failure failure) noexcept {
    switch (failure) {
    case Failure::buildData:
        return "build_data";
    case Failure::targets:
        return "targets";
    case Failure::source:
        return "source";
    case Failure::ambiguous:
        return "ambiguous";
    case Failure::allocation:
        return "allocation";
    case Failure::write:
        return "write";
    case Failure::verification:
        return "verification";
    case Failure::none:
        return "none";
    }
    return "unknown";
}

void report_failure(Failure failure, std::uint64_t detail = 0) noexcept {
    if (failure == g_lastFailure) {
        return;
    }
    g_lastFailure = failure;
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=investment stage=arrivals_leg_sets result=deferred reason=%s detail=%llu",
                      failure_name(failure),
                      static_cast<unsigned long long>(detail));
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::warn,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

[[nodiscard]] bool read_bytes(std::uintptr_t address, std::span<std::byte> output) noexcept {
    return !output.empty() && memory::read_current_process(nullptr, address, output);
}

template <typename Value> [[nodiscard]] bool read(std::uintptr_t address, Value& value) noexcept {
    return read_bytes(address, std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

[[nodiscard]] bool write_bytes(std::uintptr_t address, std::span<const std::byte> bytes) noexcept {
    if (address == 0 || bytes.empty()) {
        return false;
    }
    void* destination = reinterpret_cast<void*>(address);
    DWORD previous = 0;
    if (VirtualProtect(destination, bytes.size(), PAGE_READWRITE, &previous) == FALSE) {
        return false;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    DWORD ignored = 0;
    const bool restored = VirtualProtect(destination, bytes.size(), previous, &ignored) != FALSE;
    return restored;
}

template <typename Value>
[[nodiscard]] bool write(std::uintptr_t address, const Value& value) noexcept {
    return write_bytes(address,
                       std::span(reinterpret_cast<const std::byte*>(&value), sizeof value));
}

[[nodiscard]] bool
add_relative(std::uintptr_t base, std::int64_t relative, std::uintptr_t& output) noexcept {
    if (relative >= 0) {
        const auto distance = static_cast<std::uint64_t>(relative);
        if (distance > (std::numeric_limits<std::uintptr_t>::max)() - base) {
            return false;
        }
        output = base + static_cast<std::uintptr_t>(distance);
        return true;
    }
    const auto distance = static_cast<std::uint64_t>(-(relative + 1)) + 1U;
    if (distance > base) {
        return false;
    }
    output = base - static_cast<std::uintptr_t>(distance);
    return true;
}

/** Resolves one native count/self-relative array and validates its repeated header. */
[[nodiscard]] bool resolve_array(std::uintptr_t descriptor,
                                 std::size_t maximumCount,
                                 std::size_t stride,
                                 ArrayView& output) noexcept {
    output = {};
    ArrayDescriptor encoded{};
    if (!read(descriptor, encoded) || encoded.count == 0 || encoded.count > maximumCount) {
        return false;
    }
    std::uintptr_t header = 0;
    if (!add_relative(descriptor + sizeof(std::uint64_t), encoded.relative, header)
        || header < kArrayMarkerSize) {
        return false;
    }
    std::uint32_t marker = 0;
    std::uint64_t repeatedCount = 0;
    std::uint32_t elementClass = 0;
    if (!read(header - kArrayMarkerSize, marker) || !read(header, repeatedCount)
        || !read(header + sizeof(std::uint64_t), elementClass) || repeatedCount != encoded.count
        || (marker >> 16U) != 0x8080U || (elementClass >> 16U) != 0x8080U
        || encoded.count
               > ((std::numeric_limits<std::uintptr_t>::max)() - header - kArrayHeaderSize)
                     / stride) {
        return false;
    }
    const std::uintptr_t data = header + kArrayHeaderSize;
    std::byte tail{};
    if (!read(data + static_cast<std::uintptr_t>(encoded.count * stride) - 1U, tail)) {
        return false;
    }
    output = {descriptor, header, data, encoded.count, elementClass};
    return true;
}

[[nodiscard]] bool
member_index(const ArrayView& array, std::size_t position, std::uint32_t& index) noexcept {
    return position < array.count
           && read(array.data + static_cast<std::uintptr_t>(position) * kPlugMemberStride, index);
}

[[nodiscard]] std::size_t count_member(const ArrayView& array, std::uint32_t index) noexcept {
    std::size_t count = 0;
    for (std::size_t position = 0; position < array.count; ++position) {
        std::uint32_t current = 0;
        if (!member_index(array, position, current)) {
            return kMaximumMemberCount + 1U;
        }
        count += current == index ? 1U : 0U;
    }
    return count;
}

/** Validates the exact native pre-patch membership, not just its two row numbers. */
[[nodiscard]] bool native_membership(const ArrayView& general,
                                     const ArrayView& legs,
                                     std::span<const std::uint16_t> routes,
                                     std::uint16_t reference) noexcept {
    if (general.count != kGeneralMemberCount || legs.count != kLegMemberCount
        || general.elementClass != legs.elementClass || count_member(general, reference) != 0
        || count_member(legs, reference) != 1) {
        return false;
    }
    for (const std::uint16_t route : routes) {
        if (count_member(general, route) != 1 || count_member(legs, route) != 0) {
            return false;
        }
    }
    return true;
}

/** Resolves one investment candidate and accepts only the exact installed item/set relation. */
[[nodiscard]] bool resolve_candidate(const state::content::Definition& candidate,
                                     std::span<const std::uint16_t> routes,
                                     std::uint16_t reference,
                                     LocatedSets& output) noexcept {
    const auto& targets = targets::game::content::get();
    content_investment::Source source{};
    source.investmentGlobalsTag = candidate.tag;
    source.handles.tablesSlot = reinterpret_cast<std::uintptr_t>(targets.contentHandleTablesSlot);
    source.handles.read = &memory::read_current_process;

    std::uintptr_t globals = 0;
    std::uintptr_t root = 0;
    std::uintptr_t itemTable = 0;
    std::uintptr_t plugSetTable = 0;
    std::uint32_t rootTag = 0;
    std::uint32_t itemTableTag = 0;
    std::uint32_t plugSetTableTag = 0;
    std::uint64_t itemCount = 0;
    if (!content_handles::resolve(source.handles, source.investmentGlobalsTag, globals)
        || !read(globals + content_investment::layout::kGlobalsRootTagOffset, rootTag)
        || !content_handles::resolve(source.handles, rootTag, root)
        || !read(root + content_investment::layout::kItemTableTagOffset, itemTableTag)
        || !content_handles::resolve(source.handles, itemTableTag, itemTable)
        || !read(itemTable + item_layout::kTableRowCountOffset, itemCount) || itemCount == 0
        || itemCount > state::build_data::items::kDefinitionCapacity
        || !read(root + content_investment::layout::kPlugSetTableTagOffset, plugSetTableTag)
        || !content_handles::resolve(source.handles, plugSetTableTag, plugSetTable)) {
        return false;
    }

    const std::uintptr_t itemRows = itemTable + item_layout::kTableFirstRowOffset;
    const auto matches_item = [&](std::uint16_t index, std::uint32_t hash) noexcept {
        if (index >= itemCount) {
            return false;
        }
        item_layout::ItemIndexRow row{};
        return read(itemRows + static_cast<std::uintptr_t>(index) * sizeof row, row)
               && row.definitionHash == hash;
    };
    if (!matches_item(reference, kLegArmorReferenceHash)) {
        return false;
    }
    for (std::size_t index = 0; index < routes.size(); ++index) {
        if (!matches_item(routes[index], kArrivalsLegModHashes[index])) {
            return false;
        }
    }

    ArrayView sets{};
    if (!resolve_array(
            plugSetTable + kTableArrayDescriptorOffset, kMaximumSetCount, kPlugSetRowStride, sets)
        || sets.count <= kLegSetIndex) {
        return false;
    }
    const std::uintptr_t generalDescriptor =
        sets.data + kGeneralSetIndex * kPlugSetRowStride + kPlugSetMemberDescriptorOffset;
    const std::uintptr_t legDescriptor =
        sets.data + kLegSetIndex * kPlugSetRowStride + kPlugSetMemberDescriptorOffset;
    ArrayView general{};
    ArrayView legs{};
    if (!resolve_array(generalDescriptor, kMaximumMemberCount, kPlugMemberStride, general)
        || !resolve_array(legDescriptor, kMaximumMemberCount, kPlugMemberStride, legs)
        || !native_membership(general, legs, routes, reference)) {
        return false;
    }
    output = {source, general, legs};
    return true;
}

/** Finds exactly one native pair among every registered investment-globals candidate. */
[[nodiscard]] bool locate_sets(std::span<const std::uint16_t> routes,
                               std::uint16_t reference,
                               LocatedSets& output,
                               Failure& failure,
                               std::uint64_t& detail) noexcept {
    output = {};
    failure = Failure::source;
    detail = 0;
    if (!targets::game::content::is_resolved()
        || targets::game::content::get().contentHandleTablesSlot == nullptr) {
        failure = Failure::targets;
        return false;
    }
    std::array<state::content::Definition, kBootstrapCandidateCapacity> candidates{};
    std::size_t candidateCount = 0;
    if (!state::content::lookup_hash(kInvestmentGlobalsNameHash, candidates, candidateCount)
        && candidateCount == 0) {
        return false;
    }
    std::size_t matches = 0;
    for (std::size_t index = 0; index < candidateCount; ++index) {
        LocatedSets candidateSets{};
        if (!resolve_candidate(candidates[index], routes, reference, candidateSets)) {
            continue;
        }
        if (matches != 0
            && (candidateSets.general.descriptor != output.general.descriptor
                || candidateSets.legs.descriptor != output.legs.descriptor)) {
            failure = Failure::ambiguous;
            detail = matches + 1U;
            return false;
        }
        output = candidateSets;
        ++matches;
    }
    detail = matches;
    return matches != 0;
}

[[nodiscard]] std::uintptr_t align_up(std::uintptr_t value, std::uintptr_t alignment) noexcept {
    const std::uintptr_t mask = alignment - 1U;
    if (value > (std::numeric_limits<std::uintptr_t>::max)() - mask) {
        return 0;
    }
    return (value + mask) & ~mask;
}

/** Reserves one process-lifetime arena while the low content address domain is still available. */
[[nodiscard]] std::byte* reserve_low_arena() noexcept {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::uintptr_t granularity = system.dwAllocationGranularity;
    std::uintptr_t cursor =
        align_up(reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress), granularity);
    while (cursor != 0 && cursor <= kMaximumLowAddress
           && kLowArenaSize <= kMaximumLowAddress - cursor + 1U) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information, sizeof information)
            == 0) {
            break;
        }
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const std::uintptr_t next =
            information.RegionSize <= (std::numeric_limits<std::uintptr_t>::max)() - base
                ? base + information.RegionSize
                : 0;
        if (information.State == MEM_FREE) {
            const std::uintptr_t candidate = align_up((std::max)(cursor, base), granularity);
            const std::uintptr_t offset = candidate >= base ? candidate - base : 0;
            if (candidate != 0 && candidate <= kMaximumLowAddress
                && kLowArenaSize <= kMaximumLowAddress - candidate + 1U && candidate >= base
                && offset <= information.RegionSize
                && kLowArenaSize <= information.RegionSize - offset) {
                void* const allocated = VirtualAlloc(reinterpret_cast<void*>(candidate),
                                                     kLowArenaSize,
                                                     MEM_RESERVE | MEM_COMMIT,
                                                     PAGE_READWRITE);
                if (allocated != nullptr) {
                    return static_cast<std::byte*>(allocated);
                }
            }
        }
        if (next == 0 || next <= cursor) {
            break;
        }
        cursor = align_up(next, granularity);
    }
    return nullptr;
}

/** Carves one immutable array payload from the arena reserved at DLL process attach. */
[[nodiscard]] Allocation allocate_array(std::size_t size) noexcept {
    const std::size_t aligned = static_cast<std::size_t>(align_up(g_lowArenaUsed, kArenaAlignment));
    if (g_lowArena == nullptr || aligned > kLowArenaSize || size > kLowArenaSize - aligned) {
        return {};
    }
    g_lowArenaUsed = aligned + size;
    return {g_lowArena + aligned, size};
}

void release(Allocation& allocation) noexcept {
    // Storage belongs to the process-lifetime low arena, not to an individual array.
    allocation = {};
}

[[nodiscard]] bool
snapshot_row(const ArrayView& source, std::size_t position, relocation::Row& output) noexcept {
    output = {};
    const auto address = source.data + position * kPlugMemberStride;
    if (position >= source.count || address % 8 != 0
        || source.elementClass != relocation::kMemberClass || !read_bytes(address, output.bytes))
        return false;
    const auto count = relocation::get<std::uint64_t>(output.bytes.data() + 8);
    if (count != 0) {
        std::uintptr_t header = 0;
        if (count != 1
            || !add_relative(
                address + 16, relocation::get<std::int64_t>(output.bytes.data() + 16), header)
            || header < 4 || header % 8 != 0 || !read_bytes(header - 4, output.condition))
            return false;
    }
    return relocation::valid(output);
}

[[nodiscard]] bool verify_owned(std::size_t set, const Allocation& allocation) noexcept {
    return allocation
           && relocation::verify(std::span(g_expected[set].data(), g_expectedCounts[set]),
                                 std::span<const std::byte>(allocation.base, allocation.size));
}

/** Validates the exact plug blocks observed in this build before changing four category fields. */
bool stage_categories(const content_investment::Source& source,
                      std::span<const std::uint16_t> routes,
                      std::uint16_t reference,
                      std::array<CategoryPatch, 4>& output) noexcept {
    std::uintptr_t globals = 0, root = 0, table = 0;
    g_categoryFailure = "table";
    std::uint32_t tag = 0;
    if (!content_handles::resolve(source.handles, source.investmentGlobalsTag, globals)
        || !read(globals + content_investment::layout::kGlobalsRootTagOffset, tag)
        || !content_handles::resolve(source.handles, tag, root)
        || !read(root + content_investment::layout::kItemTableTagOffset, tag)
        || !content_handles::resolve(source.handles, tag, table))
        return false;
    const auto block = [&](std::uint16_t index,
                           std::uint32_t hash,
                           std::uintptr_t& address,
                           std::array<std::byte, kPlugBlockSize>& bytes) noexcept {
        item_layout::ItemIndexRow row{};
        g_categoryFailure = "definition";
        std::uintptr_t definition = 0;
        if (!read(table + item_layout::kTableFirstRowOffset + index * sizeof row, row)
            || row.definitionHash != hash
            || !content_handles::resolve(source.handles, row.targetHandle, definition))
            return false;
        address = definition + kPlugBlockOffset + kPlugCategoryOffset;
        g_categoryFailure = "block";
        return read_bytes(definition + kPlugBlockOffset, bytes)
               && relocation::get<std::uint32_t>(bytes.data()) == 0x808077E3U;
    };
    std::array<std::byte, kPlugBlockSize> referenceBlock{};
    std::uintptr_t referenceAddress = 0;
    if (!block(reference, kLegArmorReferenceHash, referenceAddress, referenceBlock)) return false;
    g_categoryFailure = "reference_category";
    if (relocation::get<std::uint32_t>(referenceBlock.data() + 4) != kLegCategory) return false;
    for (std::size_t i = 0; i < routes.size(); ++i) {
        std::array<std::byte, kPlugBlockSize> bytes{};
        if (!block(routes[i], kArrivalsLegModHashes[i], output[i].address, bytes)) return false;
        output[i].original = relocation::get<std::uint32_t>(bytes.data() + 4);
        g_categoryFailure = "category_or_metadata";
        if ((output[i].original != kGeneralCategory && output[i].original != kLegCategory)
            || std::memcmp(bytes.data() + 8, referenceBlock.data() + 8, 56) != 0)
            return false;
    }
    return true;
}

bool categories_current() noexcept {
    if (g_categoryCount != g_categories.size()) return false;
    for (const auto& category : g_categories) {
        std::uint32_t value = 0;
        if (!read(category.address, value) || value != kLegCategory) return false;
    }
    return true;
}

/** Restore only fields still owned by this patch; retain ownership if any restore fails. */
bool restore_categories() noexcept {
    bool restored = true;
    for (std::size_t i = 0; i < g_categoryCount; ++i) {
        const auto& category = g_categories[i];
        std::uint32_t current = 0;
        if (!read(category.address, current)
            || (current != category.original
                && (current != kLegCategory || !write(category.address, category.original)))) {
            restored = false;
        }
    }
    if (restored) g_categoryCount = 0;
    return restored;
}

/** Bounded retry for item definitions that become available after set-table initialization. */
void apply_pending_categories() noexcept {
    if (g_categoryAttempts == 0) return;
    const auto now = GetTickCount64();
    if (now < g_categoryNext) return;
    g_categoryNext = now + kCategoryRetryIntervalMs;
    --g_categoryAttempts;
    std::array<CategoryPatch, 4> categories{};
    if (!stage_categories(g_categorySource, g_categoryRoutes, g_categoryReference, categories)) {
        if (now >= g_categoryDeadline) g_categoryAttempts = 0;
        if (g_categoryAttempts == kCategoryAttemptLimit - 1 || g_categoryAttempts == 0) {
            std::array<char, 200> line{};
            std::snprintf(line.data(),
                          line.size(),
                          "ev=investment stage=arrivals_leg_categories result=%s reason=%s",
                          g_categoryAttempts == 0 ? "failed" : "pending",
                          g_categoryFailure);
            core::log::write(core::log::Channel::client, core::log::Level::warn, line.data());
        }
        return;
    }
    g_categoryAttempts = 0;
    g_categories = categories;
    g_categoryCount = categories.size();
    bool written = true;
    for (const auto& category : categories) {
        if (!write(category.address, kLegCategory)) {
            written = false;
            break;
        }
    }
    if (written && categories_current()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=investment stage=arrivals_leg_categories result=applied verified=4");
        return;
    }
    (void)restore_categories();
    core::log::write(
        core::log::Channel::client,
        core::log::Level::warn,
        "ev=investment stage=arrivals_leg_categories result=failed reason=write_or_verify");
}

[[nodiscard]] bool encode_descriptor(std::uintptr_t descriptor,
                                     const Allocation& allocation,
                                     std::uint64_t count,
                                     ArrayDescriptor& output) noexcept {
    const std::uintptr_t header = reinterpret_cast<std::uintptr_t>(allocation.base) + 8;
    const std::uintptr_t relativeBase = descriptor + sizeof(std::uint64_t);
    if (header >= relativeBase) {
        const std::uintptr_t distance = header - relativeBase;
        if (distance > static_cast<std::uintptr_t>((std::numeric_limits<std::int64_t>::max)())) {
            return false;
        }
        output = {count, static_cast<std::int64_t>(distance)};
        return true;
    }
    const std::uintptr_t distance = relativeBase - header;
    if (distance > static_cast<std::uintptr_t>((std::numeric_limits<std::int64_t>::max)())) {
        return false;
    }
    output = {count, -static_cast<std::int64_t>(distance)};
    return true;
}

/** Builds the general array with the four Arrivals leg mods removed. */
[[nodiscard]] bool build_general(const ArrayView& source,
                                 std::span<const std::uint16_t> routes,
                                 Allocation& allocation,
                                 ArrayDescriptor& descriptor) noexcept {
    const std::uint64_t newCount = source.count - routes.size();
    if (newCount != kGeneralMemberCount - kArrivalsLegModHashes.size()) return false;
    std::size_t written = 0;
    for (std::size_t position = 0; position < source.count; ++position) {
        std::uint32_t index = 0;
        if (!member_index(source, position, index)) {
            release(allocation);
            return false;
        }
        if (std::find(routes.begin(), routes.end(), index) != routes.end()) {
            continue;
        }
        if (written >= newCount || !snapshot_row(source, position, g_expected[0][written])) {
            return false;
        }
        ++written;
    }
    if (written != newCount) {
        release(allocation);
        return false;
    }
    g_expectedCounts[0] = written;
    allocation = allocate_array(relocation::capacity(written));
    return allocation
           && relocation::build(std::span(g_expected[0].data(), written),
                                std::span(allocation.base, allocation.size))
           && verify_owned(0, allocation)
           && encode_descriptor(source.descriptor, allocation, newCount, descriptor);
}

/** Moves each target's OWN original row and condition data into the leg array. */
[[nodiscard]] bool build_legs(const ArrayView& source,
                              const ArrayView& general,
                              std::span<const std::uint16_t> routes,
                              std::uint16_t reference,
                              Allocation& allocation,
                              ArrayDescriptor& descriptor) noexcept {
    const std::uint64_t newCount = source.count + routes.size();
    if (newCount != kLegMemberCount + kArrivalsLegModHashes.size()) return false;
    std::size_t referencePosition = source.count;
    for (std::size_t position = 0; position < source.count; ++position) {
        if (!snapshot_row(source, position, g_expected[1][position])) {
            release(allocation);
            return false;
        }
        std::uint32_t index = 0;
        if (!member_index(source, position, index)) {
            release(allocation);
            return false;
        }
        if (index == reference) {
            referencePosition = position;
        }
    }
    if (referencePosition == source.count) {
        release(allocation);
        return false;
    }
    for (std::size_t route = 0; route < routes.size(); ++route) {
        bool found = false;
        for (std::size_t position = 0; position < general.count; ++position) {
            std::uint32_t index = 0;
            if (!member_index(general, position, index)) return false;
            if (index != routes[route]) continue;
            if (found || !snapshot_row(general, position, g_expected[1][source.count + route]))
                return false;
            found = true;
        }
        if (!found) return false;
    }
    g_expectedCounts[1] = static_cast<std::size_t>(newCount);
    // Keep the native Empty Mod Socket first, followed by the four artifact mods.
    std::rotate(g_expected[1].begin() + 1,
                g_expected[1].begin() + source.count,
                g_expected[1].begin() + newCount);
    allocation = allocate_array(relocation::capacity(g_expectedCounts[1]));
    return allocation
           && relocation::build(std::span(g_expected[1].data(), g_expectedCounts[1]),
                                std::span(allocation.base, allocation.size))
           && verify_owned(1, allocation)
           && encode_descriptor(source.descriptor, allocation, newCount, descriptor);
}

[[nodiscard]] bool patched_membership(const ArrayView& general,
                                      const ArrayView& legs,
                                      std::span<const std::uint16_t> routes,
                                      std::uint16_t reference) noexcept {
    if (general.count != kGeneralMemberCount - routes.size()
        || legs.count != kLegMemberCount + routes.size() || count_member(legs, reference) != 1) {
        return false;
    }
    for (const std::uint16_t route : routes) {
        if (count_member(general, route) != 0 || count_member(legs, route) != 1) {
            return false;
        }
    }
    return true;
}

/** Fast steady-state check used by the callback fallback after synchronous application. */
[[nodiscard]] bool descriptors_current() noexcept {
    if (g_appliedCount != g_applied.size()) {
        return false;
    }
    ArrayDescriptor generalDescriptor{};
    ArrayDescriptor legDescriptor{};
    if (!read(g_applied[0].descriptor, generalDescriptor)
        || !read(g_applied[1].descriptor, legDescriptor)
        || generalDescriptor != g_applied[0].replacement
        || legDescriptor != g_applied[1].replacement) {
        return false;
    }
    return true;
}

/** Full post-write proof, deliberately paid only once rather than on every callback. */
[[nodiscard]] bool patched_sets_current(std::span<const std::uint16_t> routes,
                                        std::uint16_t reference) noexcept {
    if (!descriptors_current()) {
        return false;
    }
    ArrayView general{};
    ArrayView legs{};
    return resolve_array(g_applied[0].descriptor, kMaximumMemberCount, kPlugMemberStride, general)
           && resolve_array(g_applied[1].descriptor, kMaximumMemberCount, kPlugMemberStride, legs)
           && patched_membership(general, legs, routes, reference)
           && verify_owned(0, g_allocations[0]) && verify_owned(1, g_allocations[1]);
}

/** Restores descriptors only when they still name this module's allocations. */
bool restore_applied() noexcept {
    g_categoryAttempts = 0;
    bool restoredAll = restore_categories();
    for (std::size_t index = g_appliedCount; index > 0; --index) {
        const AppliedSet& applied = g_applied[index - 1];
        ArrayDescriptor current{};
        if (!read(applied.descriptor, current)) {
            restoredAll = false;
        } else if (current == applied.original) {
            continue;
        } else if (current != applied.replacement || !write(applied.descriptor, applied.original)) {
            restoredAll = false;
        }
    }
    if (restoredAll) {
        for (Allocation& allocation : g_allocations) {
            release(allocation);
        }
        g_applied = {};
        g_appliedCount = 0;
        g_categoryCount = 0;
    }
    return restoredAll;
}

} // namespace

void reserve_socket_menu_routing_storage() noexcept {
    if (g_lowArena == nullptr) {
        g_lowArena = reserve_low_arena();
    }
}

/** Arms the one synchronous correction after native content-table patching completes. */
void arm_socket_menu_routing() noexcept {
    g_armed.store(true, std::memory_order_release);
}

/** Moves exactly four members between two validated native reusable plug sets. */
void apply_socket_menu_routing() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_armed.load(std::memory_order_acquire)) {
        apply_pending_categories();
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    // One attempt per native patch-completion event. No callback-pump retry loop.
    g_armed.store(false, std::memory_order_release);

    std::array<std::uint16_t, kArrivalsLegModHashes.size()> routeIndices{};
    std::uint16_t referenceIndex = UINT16_MAX;
    state::build_data::items::Definition definition{};
    bool mapped = state::build_data::find_item_definition_hash(kLegArmorReferenceHash, definition);
    if (mapped) {
        referenceIndex = definition.definitionIndex;
    }
    for (std::size_t route = 0; mapped && route < kArrivalsLegModHashes.size(); ++route) {
        mapped =
            state::build_data::find_item_definition_hash(kArrivalsLegModHashes[route], definition);
        if (mapped) {
            routeIndices[route] = definition.definitionIndex;
        }
    }
    if (!mapped) {
        report_failure(Failure::buildData);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (descriptors_current()) {
        apply_pending_categories();
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (g_appliedCount != 0 || g_categoryCount != 0) {
        if (!restore_applied()) {
            report_failure(Failure::write);
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
    }
    // Never reuse published storage: native readers may still retain a pointer after restoration.

    LocatedSets located{};
    Failure failure = Failure::none;
    std::uint64_t detail = 0;
    if (!locate_sets(routeIndices, referenceIndex, located, failure, detail)) {
        report_failure(failure, detail);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    std::array<Allocation, 2> allocations{};
    std::array<ArrayDescriptor, 2> replacements{};
    if (!build_general(located.general, routeIndices, allocations[0], replacements[0])
        || !build_legs(located.legs,
                       located.general,
                       routeIndices,
                       referenceIndex,
                       allocations[1],
                       replacements[1])) {
        release(allocations[0]);
        release(allocations[1]);
        report_failure(Failure::allocation);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    ArrayDescriptor originalGeneral{};
    ArrayDescriptor originalLegs{};
    const bool originalsRead = read(located.general.descriptor, originalGeneral)
                               && read(located.legs.descriptor, originalLegs);
    if (!originalsRead) {
        report_failure(Failure::write);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    // Record ownership BEFORE either write; a protection-restoration failure may follow a copy.
    g_allocations = allocations;
    g_applied = {{{located.general.descriptor, originalGeneral, replacements[0]},
                  {located.legs.descriptor, originalLegs, replacements[1]}}};
    g_appliedCount = g_applied.size();
    const bool generalWritten = write(located.general.descriptor, replacements[0]);
    const bool legsWritten = generalWritten && write(located.legs.descriptor, replacements[1]);
    if (!legsWritten) {
        restore_applied();
        report_failure(Failure::write);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    if (!patched_sets_current(routeIndices, referenceIndex)) {
        restore_applied();
        report_failure(Failure::verification);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    g_lastFailure = Failure::none;
    g_categorySource = located.source;
    g_categoryRoutes = routeIndices;
    g_categoryReference = referenceIndex;
    g_categoryAttempts = kCategoryAttemptLimit;
    g_categoryNext = 0;
    g_categoryDeadline = GetTickCount64() + kCategoryRetryWindowMs;
    apply_pending_categories();
    g_armed.store(false, std::memory_order_release);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=investment stage=arrivals_leg_sets result=applied source_count=%llu "
        "leg_count=%llu source=0x%llX legs=0x%llX general_data=0x%llX leg_data=0x%llX "
        "validation=deep_copy_v3 rows_verified=71 order=empty_then_artifact",
        static_cast<unsigned long long>(kGeneralMemberCount - routeIndices.size()),
        static_cast<unsigned long long>(kLegMemberCount + routeIndices.size()),
        static_cast<unsigned long long>(located.general.descriptor),
        static_cast<unsigned long long>(located.legs.descriptor),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_allocations[0].base)),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_allocations[1].base)));
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void restore_socket_menu_routing() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    restore_applied();
    g_armed.store(false, std::memory_order_release);
    g_lastFailure = Failure::none;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace sunrise::client::hooks::network::investment
