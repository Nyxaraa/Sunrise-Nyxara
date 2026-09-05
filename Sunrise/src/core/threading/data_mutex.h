#pragma once

#include <concepts>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "sendable.h"
#include "srw_lock.h"

namespace sunrise::core::threading {

/** A combination of Mutex + Data. This allows Data types to be written as if they're single
 * threaded as you'll only have access when the mutex is locked. */
template <typename Data, typename Mutex = SrwLock> class DataMutex {
public:
    explicit DataMutex() noexcept
        requires std::default_initializable<Data>
    = default;

    template <typename... Args>
        requires std::constructible_from<Data, Args...>
    explicit DataMutex(std::in_place_t, Args&&... args) : data_(std::forward<Args>(args)...) {}

    /** Locks the mutex and calls the given Func */
    template <std::invocable<Data&> Func, Sendable Return = std::invoke_result_t<Func, Data&>>
    [[nodiscard]] Return lock(Func&& func) noexcept {
        const std::lock_guard lock(mutex_);
        return std::invoke(std::forward<Func>(func), data_);
    }

    /** Tries to loc the mutex, only calls the given Func if successful */
    template <std::invocable<Data&> Func> void try_lock(Func&& func) noexcept {
        std::unique_lock lock(mutex_, std::try_to_lock);

        if (lock.owns_lock()) {
            std::invoke(std::forward<Func>(func), data_);
        }
    }

private:
    mutable Mutex mutex_;
    Data data_;
};

/** Similar to the above but also allows for multple readers. Readers are passed a const Data&,
 * making accidental writes impossible */
template <typename Data, typename SharedMutex = SrwLock> class SharedDataMutex {
public:
    explicit SharedDataMutex() noexcept
        requires std::default_initializable<Data>
    = default;

    template <typename... Args>
        requires std::constructible_from<Data, Args...>
    explicit SharedDataMutex(std::in_place_t, Args&&... args)
        : data_(std::forward<Args>(args)...) {}

    /** Locks the mutex for reading and calls the given Func. Multiple readers can be active at
     * once */
    template <std::invocable<const Data&> Func,
              Sendable Return = std::invoke_result_t<Func, const Data&>>
    [[nodiscard]] Return lock_read(Func&& func) const noexcept {
        const std::shared_lock lock(mutex_);
        return std::invoke(std::forward<Func>(func), data_);
    }

    /** Locks the mutex for writing and calls the given Func. This is an exclusive lock and
     * guarantees there are no other readers or writers */
    template <std::invocable<Data&> Func, Sendable Return = std::invoke_result_t<Func, Data&>>
    [[nodiscard]] Return lock_write(Func&& func) noexcept {
        const std::lock_guard lock(mutex_);
        return std::invoke(std::forward<Func>(func), data_);
    }

private:
    mutable SharedMutex mutex_{};
    Data data_{};
};

} // namespace sunrise::core::threading
