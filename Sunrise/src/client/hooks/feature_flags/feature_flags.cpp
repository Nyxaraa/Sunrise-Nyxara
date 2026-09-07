#include "feature_flags.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::feature_flags {
namespace {

using RegisterName = void(__fastcall*)(const char*) noexcept;

/**
 * The name registrar: it locks, adds the name to the shared list, and returns.
 * Wildcarded at both RIP-relative operands and at the call, so only its shape is matched.
 */
inline constexpr std::string_view kRegisterNameText =
    "48 89 5C 24 08 57 48 83 EC 20 8B 1D ? ? ? ? 48 8B F9 8B CB 33 D2 E8 ? ? ? ? 83 3D ? ? ? ? 32";
/** Compiled length of the registrar signature, counted from its text at build time. */
inline constexpr std::size_t kRegisterNameSize = patterns::signature_length(kRegisterNameText);
constinit const std::array<patterns::PatternByte, kRegisterNameSize> kRegisterName =
    patterns::signature<kRegisterNameSize>(kRegisterNameText);

/** Name the channel manager tests before it closes a channel that has lost every owner. */
constexpr const char* kPreventOwnerlessClose = "prevent_closing_channels_with_no_owners";

/** Offset of the registered-count operand inside the registrar, and of the instruction after it. */
constexpr std::size_t kCountOperandOffset = 30;
constexpr std::size_t kCountNextInstruction = 35;

std::atomic<bool> g_applied{};

} // namespace

/**
 * Registers the named client feature flags this build asks for, once.
 * TODO: find the client's unregister path; until then shutdown cannot reverse the registration.
 */
void apply_once() noexcept {
    if (!core::settings::get().server.activation.preventOwnerlessChannelClose) {
        return;
    }
    if (g_applied.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::byte* const found = patterns::scan_main_image_unique(kRegisterName, "feature_register");
    if (found == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=feature stage=register result=fail reason=signature");
        return;
    }
    // The registrar copies the name into a fixed table and bumps this count, so the count is the
    // only proof the name landed. Calling the registrar says nothing on its own.
    const auto* counter = reinterpret_cast<const std::int32_t*>(
        patterns::resolve_relative(found + kCountOperandOffset, found + kCountNextInstruction));
    const std::int32_t before = counter != nullptr ? *counter : -1;
    const auto call = reinterpret_cast<RegisterName>(found);
    call(kPreventOwnerlessClose);
    const std::int32_t after = counter != nullptr ? *counter : -1;
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=feature stage=register result=ok before=%d after=%d",
                                      before,
                                      after);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace sunrise::client::hooks::feature_flags
