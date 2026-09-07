#pragma once

#include <cstdint>

namespace sunrise::client::hooks::world_objects::trace {

/** Associates observations only with one native rebind iterator and its direct calls. */
struct Association final {
    std::uintptr_t iterator{};
    std::uint32_t actor{0xFFFFFFFFU}, owner{0xFFFFFFFFU};
    bool active{};

    /** Accepts only the direct iterator call made by the native rebind pass. */
    bool visit(std::uintptr_t caller, std::uintptr_t expected, std::uintptr_t value) noexcept {
        if (!active || caller != expected || (iterator != 0 && iterator != value)) {
            return false;
        }
        iterator = value;
        actor = owner = 0xFFFFFFFFU;
        return true;
    }

    /** Accepts only calls for the current actor's owner on the direct rebind stack. */
    bool
    matches(std::uintptr_t caller, std::uintptr_t expected, std::uint32_t value) const noexcept {
        return active && actor != 0xFFFFFFFFU && caller == expected && owner == value;
    }
};

/** Restores the enclosing thread-local trace when a nested native call returns. */
template <typename Value> class Scope final {
public:
    Scope(Value*& slot, Value& current) noexcept : slot_(slot), prior_(slot) {
        slot_ = &current;
    }
    ~Scope() {
        slot_ = prior_;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Value*& slot_;
    Value* prior_;
};

} // namespace sunrise::client::hooks::world_objects::trace
