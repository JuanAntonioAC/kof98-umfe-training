#include "bind.h"
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void* reserved) {
    (void)module;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        game_set_module(module);
        game_install();
    }
    return TRUE;
}
