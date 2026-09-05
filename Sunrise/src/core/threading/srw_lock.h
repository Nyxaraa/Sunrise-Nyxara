#pragma once

#include <WinSock2.h>

namespace sunrise::core::threading {

/** Wrapper to enable std::lock_guard and std::shared_lock for SRWLOCK */
class SrwLock final {
public:
    constexpr explicit SrwLock() noexcept = default;

    SrwLock(const SrwLock&) = delete;
    SrwLock(SrwLock&&) = delete;
    SrwLock& operator=(const SrwLock&) = delete;
    SrwLock& operator=(SrwLock&&) = delete;

    // stl Lockable
    void lock() noexcept {
        AcquireSRWLockExclusive(&lock_);
    }

    [[nodiscard]] bool try_lock() noexcept {
        return TryAcquireSRWLockExclusive(&lock_);
    }

    void unlock() noexcept {
        ReleaseSRWLockExclusive(&lock_);
    }

    // stl SharedLockable
    void lock_shared() noexcept {
        AcquireSRWLockShared(&lock_);
    }

    [[nodiscard]] bool try_lock_shared() noexcept {
        return TryAcquireSRWLockShared(&lock_);
    }

    void unlock_shared() noexcept {
        ReleaseSRWLockShared(&lock_);
    }

private:
    SRWLOCK lock_{SRWLOCK_INIT};
};

} // namespace sunrise::core::threading
