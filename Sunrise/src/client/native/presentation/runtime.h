#pragma once
namespace sunrise::client::native::presentation {
// Called by the game's existing platform callback pump, never a native detour.
void service() noexcept;
}
