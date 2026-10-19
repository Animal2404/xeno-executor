#pragma once

#include <string>

struct lua_State;

namespace xeno::state {

bool install();

bool attached();

bool schedule(std::string source);

}
