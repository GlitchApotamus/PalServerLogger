#define _WINSOCKAPI_
#include <windows.h>
#include <winsock2.h>

#include "core/runtime.h"
#include "core/logging.h"
#include "hooks/console_hooks.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitializeConsoleHooks, nullptr, 0, nullptr);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        g_IsRunning = false;
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
