#pragma once

#include <cstddef>
#include <span>

#include "../../../patterns/game.h"
#include "../config_getter/game_config_getter_targets.h"
#include "../content.h"
#include "../network.h"
#include "../packages/game_package_targets.h"

namespace sunrise::client::targets::game::resolution {

/** Network signatures occupy the leading registry slice through the content-id token load. */
inline constexpr std::size_t kNetworkMatchCount =
    static_cast<std::size_t>(patterns::game::Id::contentIdTokenLoad) + 1;
/** Content signatures begin immediately after the network registry slice. */
inline constexpr std::size_t kContentFirstMatch =
    static_cast<std::size_t>(patterns::game::Id::queuezObjectResolver);
/** The aggregate match array covers the complete game signature registry. */
inline constexpr std::size_t kMatchCount = static_cast<std::size_t>(patterns::game::Id::count);
inline constexpr std::size_t kContentMatchCount = kMatchCount - kContentFirstMatch;
inline constexpr std::size_t kRequiredMatchCount = kMatchCount;
static_assert(kNetworkMatchCount == kContentFirstMatch);

} // namespace sunrise::client::targets::game::resolution

namespace sunrise::client::targets::game::network {

/**
 * Derives a network target table without publishing it.
 * @param image Executable ranges from the main game image.
 * @param matches Validated unique matches from the network registry slice.
 * @param output Receives the complete derived table.
 * @return True when every direct and derived address validates.
 */
[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image,
                          std::span<const patterns::Match> matches,
                          Targets& output) noexcept;

/** @param targets Fully validated network table published without failure. */
void publish(const Targets& targets) noexcept;

} // namespace sunrise::client::targets::game::network

namespace sunrise::client::targets::game::content {

/**
 * Derives a content target table without publishing it.
 * @param image Executable ranges from the main game image.
 * @param matches Validated unique matches from the content registry slice.
 * @param output Receives the complete derived table.
 * @return True when every direct and derived address validates.
 */
[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image,
                          std::span<const patterns::Match> matches,
                          Targets& output) noexcept;

/** @param targets Fully validated content table published without failure. */
void publish(const Targets& targets) noexcept;

} // namespace sunrise::client::targets::game::content
