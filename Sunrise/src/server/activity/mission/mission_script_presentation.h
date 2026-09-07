#pragma once
#include "../../../state/activity/presentation/config.h"
struct lua_State;
namespace sunrise::server::activity::mission::lua_vm::detail {
void register_presentation_api(lua_State*);
void capture_presentation(lua_State*,
                          int program,
                          bool publicTarget,
                          state::activity::presentation::Config& output);
}
