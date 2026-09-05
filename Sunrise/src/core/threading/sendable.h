#pragma once

#include <concepts>

namespace sunrise::core::threading {

/** An specializable struct that indicates a type can be sent across thread boundaries */
template <typename T> struct IsSendable : std::false_type {};

/** Indicates a specific type can be sent across thread boundaries. Integral, loating point, and
 * void types are always allowed since they're easily copyable. Custom types can be marked as
 * `Sendable` by specializing `IsSendable` above */
template <typename T>
concept Sendable =
    std::integral<T> || std::floating_point<T> || std::is_void_v<T> || IsSendable<T>::value;

} // namespace sunrise::core::threading
