#pragma once

#include <cstddef>

namespace sunrise::client::content::investment::layout {

/** The globals blob stores its investment-root tag after 16 ABI bytes. */
inline constexpr std::size_t kGlobalsRootTagOffset = 16;
/** The dense item-table handle sits at byte 776 of the investment root. */
inline constexpr std::size_t kItemTableTagOffset = 776;
/** The reusable/randomized plug-set table handle sits at byte 824 of the investment root. */
inline constexpr std::size_t kPlugSetTableTagOffset = 824;

} // namespace sunrise::client::content::investment::layout
