#include "execution.hpp"

#include <atomic>
#include <cstdlib>
#include <string>

#include <lua.h>
#include <luacode.h>

namespace xeno::execution {
namespace {

std::atomic<bool> executing{false};

}

bool is_executing() {
    return executing.load(std::memory_order_relaxed);
}

bool run(lua_State* state, const std::string& source, std::string& error) {
    error.clear();
    if (!state) {
        error = "no Luau state";
        return false;
    }

    const int base = lua_gettop(state);
    if (!lua_checkstack(state, 4)) {
        error = "Luau stack exhausted";
        return false;
    }

    lua_State* thread = lua_newthread(state);
    if (!thread) {
        error = "could not create Luau thread";
        return false;
    }

    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1;

    size_t bytecode_size = 0;
    char* bytecode = luau_compile(source.data(), source.size(), &options, &bytecode_size);
    if (!bytecode) {
        lua_settop(state, base);
        error = "compiler failed";
        return false;
    }

    const int loaded = luau_load(thread, "=xeno", bytecode, bytecode_size, 0);
    std::free(bytecode);

    if (loaded != 0) {
        const char* message = lua_tolstring(thread, -1, nullptr);
        error = message ? message : "failed to load chunk";
        lua_settop(thread, 0);
        lua_settop(state, base);
        return false;
    }

    struct executing_guard {
        executing_guard() {
            executing.store(true, std::memory_order_relaxed);
        }
        ~executing_guard() {
            executing.store(false, std::memory_order_relaxed);
        }
    } guard;

    const int status = lua_pcall(thread, 0, 0, 0);

    if (status == LUA_YIELD) {
        error = "script yielded; yielding is not supported by the executor runner";
        lua_settop(thread, 0);
    } else if (status != 0) {
        const char* message = lua_tolstring(thread, -1, nullptr);
        error = message ? message : "runtime error";
        lua_settop(thread, 0);
    }

    lua_settop(state, base);
    return status == 0;
}

}
