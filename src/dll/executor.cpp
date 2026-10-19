#include "executor.hpp"

#include "log.hpp"
#include "overlay.hpp"
#include "state.hpp"
#include "version.hpp"

#include <windows.h>
#include <MinHook.h>

#include <string>

namespace xeno::executor {
namespace {

HANDLE runtime_mutex = nullptr;

}

void run() {
    runtime_mutex = CreateMutexW(nullptr, TRUE, L"Local\\XenoRuntime");
    if (runtime_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        log::warn("another Xeno runtime is already attached to this process");
        return;
    }

    log::info(std::string(XENO_NAME) + " " + XENO_VERSION + " booting");

    const MH_STATUS hook_status = MH_Initialize();
    if (hook_status != MH_OK && hook_status != MH_ERROR_ALREADY_INITIALIZED) {
        log::error("MinHook initialization failed");
        return;
    }

    overlay::start();

    if (state::install()) {
        log::info("ready - INSERT opens the workspace");
    } else {
        log::error("attach failed - the execution queue stays offline");
    }
}

}
