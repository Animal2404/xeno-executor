#include "state.hpp"

#include "api.hpp"
#include "execution.hpp"
#include "hooks.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "signatures.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <lua.h>

namespace xeno::state {
namespace {

using resume_fn = int(__cdecl*)(lua_State*, lua_State*, int);

resume_fn original_resume = nullptr;
std::atomic<lua_State*> last_state{nullptr};
std::mutex queue_mutex;
std::deque<std::string> queue;
std::atomic<bool> inside_drain{false};

bool is_game_state(lua_State* state) {
    if (!lua_checkstack(state, 2)) {
        return false;
    }

    const int top = lua_gettop(state);
    lua_getglobal(state, "game");
    const bool valid = lua_type(state, -1) == LUA_TUSERDATA;
    lua_settop(state, top);
    return valid;
}

void queue_autoexec() {
    const auto directory = util::autoexec_dir();
    std::vector<std::filesystem::path> scripts;

    std::error_code error;
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        if (item.is_regular_file(error) && item.path().extension() == L".lua") {
            scripts.push_back(item.path());
        }
    }

    std::sort(scripts.begin(), scripts.end());
    for (const auto& path : scripts) {
        const std::string source = util::read_text(path);
        if (source.empty()) {
            continue;
        }
        schedule(source);
        log::info("autoexec queued: " + util::narrow(path.filename().wstring()));
    }
}

void process(lua_State* state) {
    if (!state || !is_game_state(state)) {
        return;
    }

    if (!api::installed(state)) {
        if (!api::install(state)) {
            log::error("could not install the executor API on this Luau state");
            return;
        }

        char address[32]{};
        std::snprintf(address, sizeof(address), "%p", static_cast<void*>(state));
        log::info(std::string(XENO_NAME) + " attached to Luau state " + address);

        queue_autoexec();
    }

    last_state.store(state, std::memory_order_relaxed);

    bool expected = false;
    if (!inside_drain.compare_exchange_strong(
            expected, true, std::memory_order_relaxed)) {
        return;
    }

    struct drain_guard {
        ~drain_guard() {
            inside_drain.store(false, std::memory_order_relaxed);
        }
    } guard;

    std::deque<std::string> jobs;
    {
        std::lock_guard lock(queue_mutex);
        jobs.swap(queue);
    }

    if (jobs.empty()) {
        return;
    }

    for (const auto& source : jobs) {
        std::string error;
        if (execution::run(state, source, error)) {
            log::info("script executed");
        } else {
            log::error("script failed: " + error);
        }
    }
}

int __cdecl resume_detour(lua_State* state, lua_State* from, int arguments) {
    try {
        process(state);
    } catch (const std::exception& error) {
        log::error(std::string("resume hook failed: ") + error.what());
    } catch (...) {
        log::error("resume hook failed with an unknown exception");
    }

    return original_resume(state, from, arguments);
}

}

bool install() {
    {
        std::lock_guard lock(hooks::mutex());
        const MH_STATUS initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
            log::error("MinHook initialization failed");
            return false;
        }
    }

    const std::string pattern = util::config_value("lua_resume", signatures::lua_resume);

    for (int attempt = 0; attempt < 120; ++attempt) {
        std::uint8_t* target = mem::scan(pattern);
        if (!target) {
            Sleep(500);
            continue;
        }

        std::lock_guard lock(hooks::mutex());

        LPVOID trampoline = nullptr;
        if (MH_CreateHook(
                reinterpret_cast<LPVOID>(target),
                reinterpret_cast<LPVOID>(&resume_detour),
                &trampoline) != MH_OK) {
            log::error("could not create lua_resume hook");
            return false;
        }

        original_resume = reinterpret_cast<resume_fn>(trampoline);

        if (MH_EnableHook(reinterpret_cast<LPVOID>(target)) != MH_OK) {
            log::error("could not enable lua_resume hook");
            return false;
        }

        char address[32]{};
        std::snprintf(address, sizeof(address), "%p", static_cast<void*>(target));
        log::info("lua_resume hooked at " + std::string(address));
        return true;
    }

    log::error(
        "lua_resume signature not found. Update %LOCALAPPDATA%\\Xeno\\signatures.ini "
        "(see docs/UPDATING.md)");
    return false;
}

bool attached() {
    return last_state.load(std::memory_order_relaxed) != nullptr;
}

bool schedule(std::string source) {
    if (source.empty()) {
        return false;
    }
    std::lock_guard lock(queue_mutex);
    queue.push_back(std::move(source));
    return true;
}

}
