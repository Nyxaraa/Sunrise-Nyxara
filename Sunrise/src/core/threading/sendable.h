#pragma once

#include <concepts>

namespace sunrise::core::threading {

/** Specialize to true_type to let a type leave a locked scope by value. */
template <typename T> struct IsSendable : std::false_type {};

/**
 * Types that may be returned out of a locked scope.
 * Integral, floating point and void copy freely; anything else must opt in through IsSendable.
 */
template <typename T>
concept Sendable =
    std::integral<T> || std::floating_point<T> || std::is_void_v<T> || IsSendable<T>::value;

} // namespace sunrise::core::threading
