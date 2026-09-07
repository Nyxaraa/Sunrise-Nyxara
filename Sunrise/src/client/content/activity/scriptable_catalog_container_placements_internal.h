#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "scriptable_catalog_container_placements.h"

namespace sunrise::client::content::activity::scriptables::internal {

/** Fixed-capacity per-tag store of decoded container placements, shared across scenarios. */
struct ContainerPlacementCache::Impl final {
    // Fixed capacities bound one cached tag's decoded rows and inline bytes.
    static constexpr std::size_t kEntryCapacity = 16'384;
    static constexpr std::size_t kPlacementCapacity = 200'000;
    static constexpr std::size_t kConfigCapacity = 500'000;
    static constexpr std::size_t kComponentCapacity = 600'000;
    static constexpr std::size_t kInlineNameCapacity = 1'048'576;
    static constexpr std::size_t kInlineByteCapacity = 128ULL * 1024ULL * 1024ULL;

    struct Evidence final {
        std::vector<state::build_data::scriptables::InlineNameCandidate> rows{};
        std::vector<std::byte> bytes{};
    };

    struct Value final {
        std::vector<state::build_data::scriptables::ContainerPlacement> placements{};
        std::vector<state::build_data::scriptables::ContainerPlacementConfig> configs{};
        std::vector<state::build_data::scriptables::ContainerPlacementComponent> components{};
        Evidence evidence{};
    };

    Impl() {
        entries.reserve(kEntryCapacity);
    }

    std::unordered_map<std::uint32_t, std::shared_ptr<const Value>> entries{};
    std::size_t placementRows{};
    std::size_t configRows{};
    std::size_t componentRows{};
    std::size_t inlineNameRows{};
    std::size_t inlineBytes{};
};

/** Gives this translation unit bounded access to the cache's private storage. */
struct ContainerPlacementCacheAccess final {
    using Evidence = ContainerPlacementCache::Impl::Evidence;
    using Value = ContainerPlacementCache::Impl::Value;

    [[nodiscard]] static std::shared_ptr<const Value> find(const ContainerPlacementCache& cache,
                                                           std::uint32_t tag) noexcept {
        if (cache.impl_ == nullptr) {
            return {};
        }
        const auto found = cache.impl_->entries.find(tag);
        return found == cache.impl_->entries.end() ? std::shared_ptr<const Value>{} : found->second;
    }

    /** Stores one tag's decoded placements, dropping the insert when a capacity is reached. */
    static void remember(ContainerPlacementCache& cache, std::uint32_t tag, Value value) noexcept {
        if (cache.impl_ == nullptr || cache.impl_->entries.contains(tag)) {
            return;
        }
        ContainerPlacementCache::Impl& impl = *cache.impl_;
        if (impl.entries.size() >= ContainerPlacementCache::Impl::kEntryCapacity
            || impl.placementRows > ContainerPlacementCache::Impl::kPlacementCapacity
            || value.placements.size()
                   > ContainerPlacementCache::Impl::kPlacementCapacity - impl.placementRows
            || impl.configRows > ContainerPlacementCache::Impl::kConfigCapacity
            || value.configs.size()
                   > ContainerPlacementCache::Impl::kConfigCapacity - impl.configRows
            || impl.componentRows > ContainerPlacementCache::Impl::kComponentCapacity
            || value.components.size()
                   > ContainerPlacementCache::Impl::kComponentCapacity - impl.componentRows
            || impl.inlineNameRows > ContainerPlacementCache::Impl::kInlineNameCapacity
            || value.evidence.rows.size()
                   > ContainerPlacementCache::Impl::kInlineNameCapacity - impl.inlineNameRows
            || impl.inlineBytes > ContainerPlacementCache::Impl::kInlineByteCapacity
            || value.evidence.bytes.size()
                   > ContainerPlacementCache::Impl::kInlineByteCapacity - impl.inlineBytes) {
            return;
        }
        try {
            const std::size_t placements = value.placements.size();
            const std::size_t configs = value.configs.size();
            const std::size_t components = value.components.size();
            const std::size_t names = value.evidence.rows.size();
            const std::size_t bytes = value.evidence.bytes.size();
            auto shared = std::make_shared<Value>(std::move(value));
            const auto result = impl.entries.emplace(tag, std::move(shared));
            if (result.second) {
                impl.placementRows += placements;
                impl.configRows += configs;
                impl.componentRows += components;
                impl.inlineNameRows += names;
                impl.inlineBytes += bytes;
            }
        } catch (...) {
            // A complete result stays valid when the optional pass cache cannot grow.
        }
    }
};

namespace container_placements {

namespace catalog = state::build_data::scriptables;
namespace package_reader = middleware::content::packages::reader;

/** Hard limits bound one scenario's process-only package graph. */
constexpr std::size_t kListCapacity = 8'192;
constexpr std::size_t kOwnerCapacity = 32'768;
constexpr std::size_t kPlacementCapacity = 262'144;
constexpr std::size_t kConfigCapacity = 1'048'576;
constexpr std::size_t kComponentCapacity = 1'048'576;

/** Inputs and outputs of one container placement pass. */
struct BuildContext final {
    const package_reader::Source* source{};
    package_reader::Scratch* scratch{};
    catalog::Snapshot* output{};
    ContainerPlacementCache* cache{};
    ContainerPlacementCancelCheck cancel{};
    std::string_view stem{};
    std::unordered_map<std::uint32_t, std::uint32_t> listRows{};
    std::vector<std::byte> containerBytes{};
    std::vector<std::byte> listBytes{};
    std::vector<std::byte> classBytes{};
    std::vector<std::byte> configBytes{};
    std::vector<std::byte> resourceBytes{};
    bool failed{};
};

/** @return True when the caller asked this pass to stop. */
[[nodiscard]] inline bool cancelled(const BuildContext& context) noexcept {
    return context.cancel != nullptr && context.cancel();
}

/** Clears padding as well as members before a raw row can reach the serialized snapshot. */
template <typename Value> void zero_row_storage(Value& output) noexcept {
    static_assert(std::is_trivially_copyable_v<Value>);
    std::memset(&output, 0, sizeof output);
}

enum class CacheReplay : std::uint8_t {
    unavailable,
    complete,
    cancelled,
};

/** Captures one complete list graph with every row index local to that graph. */
[[nodiscard]] bool capture_cached_graph(const catalog::Snapshot& output,
                                        std::uint32_t listRow,
                                        std::size_t firstPlacement,
                                        std::size_t firstConfig,
                                        std::size_t firstComponent,
                                        std::size_t firstInlineName,
                                        std::size_t firstInlineByte,
                                        ContainerPlacementCacheAccess::Value& cached) noexcept;

/** Replays one complete graph or leaves every destination bank unchanged. */
[[nodiscard]] CacheReplay
replay_cached_graph(BuildContext& context,
                    std::uint32_t listRow,
                    const ContainerPlacementCacheAccess::Value& cached) noexcept;

} // namespace container_placements

} // namespace sunrise::client::content::activity::scriptables::internal
