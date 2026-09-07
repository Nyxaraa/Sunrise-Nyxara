#pragma once

#include <concepts>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "sendable.h"
#include "srw_lock.h"

namespace sunrise::core::threading {

/** Data reachable only while its mutex is held; nothing else can name it. */
template <typename Data, typename Mutex = SrwLock> class DataMutex {
public:
    explicit DataMutex() noexcept
        requires std::default_initializable<Data>
    = default;

    template <typename... Args>
        requires std::constructible_from<Data, Args...>
    explicit DataMutex(std::in_place_t, Args&&... args) : data_(std::forward<Args>(args)...) {}

    /**
     * Runs func under the exclusive lock.
     * @param func Called with the guarded data.
     * @return Whatever func returns; the type must be Sendable so no reference escapes.
     */
    template <std::invocable<Data&> Func, Sendable Return = std::invoke_result_t<Func, Data&>>
    [[nodiscard]] Return lock(Func&& func) noexcept {
        const std::lock_guard lock(mutex_);
        return std::invoke(std::forward<Func>(func), data_);
    }

    /**
     * Runs func under the exclusive lock only when it is free; skips func otherwise.
     * @param func Called with the guarded data, or not at all.
     * @return False when the lock was held, so the caller can report the skipped work.
     */
    template <std::invocable<Data&> Func> [[nodiscard]] bool try_lock(Func&& func) noexcept {
        std::unique_lock lock(mutex_, std::try_to_lock);

        if (!lock.owns_lock()) {
            return false;
        }
        std::invoke(std::forward<Func>(func), data_);
        return true;
    }

private:
    mutable Mutex mutex_;
    Data data_;
};

/** DataMutex that also admits concurrent readers; a reader only sees a const Data&. */
template <typename Data, typename SharedMutex = SrwLock> class SharedDataMutex {
public:
    explicit SharedDataMutex() noexcept
        requires std::default_initializable<Data>
    = default;

    template <typename... Args>
        requires std::constructible_from<Data, Args...>
    explicit SharedDataMutex(std::in_place_t, Args&&... args)
        : data_(std::forward<Args>(args)...) {}

    /**
     * Runs func under the shared lock; other readers may run at the same time.
     * @param func Called with the guarded data.
     * @return Whatever func returns; the type must be Sendable so no reference escapes.
     */
    template <std::invocable<const Data&> Func,
              Sendable Return = std::invoke_result_t<Func, const Data&>>
    [[nodiscard]] Return lock_read(Func&& func) const noexcept {
        const std::shared_lock lock(mutex_);
        return std::invoke(std::forward<Func>(func), data_);
    }

    /**
     * Runs func under the exclusive lock; no other reader or writer is active.
     * @param func Called with the guarded data.
     * @return Whatever func returns; the type must be Sendable so no reference escapes.
     */
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
