#pragma once
#include "config.h"
namespace sunrise::client::sdk::presentation {
constexpr bool valid_tag(std::uint32_t tag) noexcept {
    return tag && tag != 0xFFFFFFFFU;
}
constexpr bool valid_config(const Config& config) noexcept {
    if (config.movieCount > config.movies.size() || config.effectCount > config.effects.size()
        || config.deliveryCount > config.deliveries.size())
        return false;
    for (unsigned i = 0; i < config.movieCount; ++i) {
        const auto& m = config.movies[i];
        if (!valid_tag(m.asset) || !valid_tag(m.header) || !valid_tag(m.subtitles)
            || !valid_tag(m.catalog) || !valid_tag(m.stream))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (config.movies[j].asset == m.asset) return false;
    }
    if (config.movieCount) {
        std::array<std::uint32_t, 18> tags{};
        for (unsigned i = 0; i < 6; ++i) {
            tags[i] = config.surfaces.definitions[i];
            tags[i + 6] = config.surfaces.buffers[i];
            tags[i + 12] = config.surfaces.containers[i];
        }
        for (unsigned i = 0; i < tags.size(); ++i) {
            if (!valid_tag(tags[i])) return false;
            for (unsigned j = 0; j < i; ++j)
                if (tags[i] == tags[j]) return false;
        }
    }
    for (unsigned i = 0; i < config.effectCount; ++i) {
        const auto& e = config.effects[i];
        if (e.region < 0 || !valid_tag(e.source) || !valid_tag(e.original)
            || !valid_tag(e.replacement))
            return false;
        for (unsigned j = 0; j < i; ++j) {
            const auto& other = config.effects[j];
            if (e.region == other.region && e.source == other.source
                && e.sourceOffset == other.sourceOffset)
                return false;
        }
    }
    for (unsigned i = 0; i < config.deliveryCount; ++i) {
        const auto& d = config.deliveries[i];
        if (d.region < 0 || !valid_tag(d.resource) || !valid_tag(d.channel)) return false;
        for (unsigned j = 0; j < i; ++j) {
            const auto& other = config.deliveries[j];
            if (d.region == other.region && d.resource == other.resource) return false;
        }
    }
    return true;
}
constexpr bool effect_matches(const EffectAttachment& rule,
                              int region,
                              std::uint32_t source,
                              std::uint32_t schema,
                              std::uint64_t offset,
                              std::uint32_t original) noexcept {
    return rule.region == region && rule.source == source && schema == 0x80809540U
           && rule.sourceOffset == offset && rule.original == original;
}
} // namespace sunrise::client::sdk::presentation
