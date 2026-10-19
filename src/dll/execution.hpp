#pragma once

#include <string>

struct lua_State;

namespace xeno::execution {

bool run(lua_State* state, const std::string& source, std::string& error);

bool is_executing();

}
