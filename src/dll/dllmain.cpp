#include "executor.hpp"

#include <windows.h>

namespace {

DWORD WINAPI bootstrap(void*) {
    xeno::executor::run();
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, bootstrap, nullptr, 0, nullptr);
        if (!thread) {
            return FALSE;
        }
        CloseHandle(thread);
    }

    return TRUE;
}
