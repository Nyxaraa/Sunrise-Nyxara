#pragma once
#include "../../../client/hooks/scripted_presentation/config.h"
struct lua_State;
namespace sunrise::server::activity::mission::lua_vm::detail {
void capture_presentation(lua_State*,
                          int program,
                          bool publicTarget,
                          client::hooks::scripted_presentation::Config& output);
}
