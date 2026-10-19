#pragma once

struct lua_State;

namespace xeno::api {

bool install(lua_State* state);

bool installed(lua_State* state);

}
