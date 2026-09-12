#include "hooks/console_hooks.h"
#include "core/logging.h"

BOOL WINAPI Hooked_WriteConsoleA(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    TraceHookCall("WriteConsoleA");
    if (lpBuffer && nNumberOfCharsToWrite > 0)
    {
        std::string ansiString(static_cast<const char *>(lpBuffer), nNumberOfCharsToWrite);
        WriteToDashboardLog(ansiString);
    }
    return pOriginalWriteConsoleA(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved);
}

BOOL WINAPI Hooked_WriteConsoleW(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    TraceHookCall("WriteConsoleW");
    if (lpBuffer && nNumberOfCharsToWrite > 0)
    {
        const int size_needed = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(lpBuffer), nNumberOfCharsToWrite, NULL, 0, NULL, NULL);
        if (size_needed > 0)
        {
            std::string utf8String(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(lpBuffer), nNumberOfCharsToWrite, &utf8String[0], size_needed, NULL, NULL);
            WriteToDashboardLog(utf8String);
        }
    }
    return pOriginalWriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved);
}

BOOL WINAPI Hooked_WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    TraceHookCall("WriteFile");
    if (hFile == GetStdHandle(STD_OUTPUT_HANDLE) || hFile == GetStdHandle(STD_ERROR_HANDLE) || GetFileType(hFile) == FILE_TYPE_CHAR)
    {
        if (lpBuffer && nNumberOfBytesToWrite > 0)
        {
            std::string str(static_cast<const char *>(lpBuffer), nNumberOfBytesToWrite);
            WriteToDashboardLog(str);
        }
    }
    return pOriginalWriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

void WINAPI Hooked_OutputDebugStringA(LPCSTR lpOutputString)
{
    TraceHookCall("OutputDebugStringA");
    if (lpOutputString)
        WriteToDashboardLog(std::string(lpOutputString));

    if (pOriginalOutputDebugStringA)
        pOriginalOutputDebugStringA(lpOutputString);
}

void WINAPI Hooked_OutputDebugStringW(LPCWSTR lpOutputString)
{
    TraceHookCall("OutputDebugStringW");
    if (lpOutputString)
    {
        const int size_needed = WideCharToMultiByte(CP_UTF8, 0, lpOutputString, -1, NULL, 0, NULL, NULL);
        if (size_needed > 0)
        {
            std::string utf8String(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, lpOutputString, -1, &utf8String[0], size_needed, NULL, NULL);
            WriteToDashboardLog(utf8String);
        }
    }

    if (pOriginalOutputDebugStringW)
        pOriginalOutputDebugStringW(lpOutputString);
}

DWORD WINAPI InitializeConsoleHooks(LPVOID lpParam)
{
    InitializeLogEnvironment();

    MH_STATUS initStatus = MH_Initialize();
    if (g_DebugHooks)
        WriteToDashboardLog("[HOOK_DEBUG] MH_Initialize => " + MinHookStatusToString(initStatus));
    if (initStatus != MH_OK)
        return 1;

    HMODULE hKernelBase = GetModuleHandleA("kernelbase.dll");
    if (!hKernelBase)
        hKernelBase = GetModuleHandleA("kernel32.dll");

    LPVOID pTargetWriteConsoleA = reinterpret_cast<LPVOID>(GetProcAddress(hKernelBase, "WriteConsoleA"));
    LPVOID pTargetWriteConsoleW = reinterpret_cast<LPVOID>(GetProcAddress(hKernelBase, "WriteConsoleW"));
    LPVOID pTargetWriteFile = reinterpret_cast<LPVOID>(GetProcAddress(hKernelBase, "WriteFile"));
    LPVOID pTargetOutputDebugStringA = reinterpret_cast<LPVOID>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "OutputDebugStringA"));
    LPVOID pTargetOutputDebugStringW = reinterpret_cast<LPVOID>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "OutputDebugStringW"));

    if (pTargetWriteConsoleA)
    {
        MH_STATUS status = MH_CreateHook(pTargetWriteConsoleA, &Hooked_WriteConsoleA, reinterpret_cast<LPVOID *>(&pOriginalWriteConsoleA));
        if (g_DebugHooks)
            WriteToDashboardLog("[HOOK_DEBUG] MH_CreateHook WriteConsoleA => " + MinHookStatusToString(status));
    }
    if (pTargetWriteConsoleW)
    {
        MH_STATUS status = MH_CreateHook(pTargetWriteConsoleW, &Hooked_WriteConsoleW, reinterpret_cast<LPVOID *>(&pOriginalWriteConsoleW));
        if (g_DebugHooks)
            WriteToDashboardLog("[HOOK_DEBUG] MH_CreateHook WriteConsoleW => " + MinHookStatusToString(status));
    }
    if (pTargetWriteFile)
    {
        MH_STATUS status = MH_CreateHook(pTargetWriteFile, &Hooked_WriteFile, reinterpret_cast<LPVOID *>(&pOriginalWriteFile));
        if (g_DebugHooks)
            WriteToDashboardLog("[HOOK_DEBUG] MH_CreateHook WriteFile => " + MinHookStatusToString(status));
    }
    if (pTargetOutputDebugStringA)
    {
        MH_STATUS status = MH_CreateHook(pTargetOutputDebugStringA, &Hooked_OutputDebugStringA, reinterpret_cast<LPVOID *>(&pOriginalOutputDebugStringA));
        if (g_DebugHooks)
            WriteToDashboardLog("[HOOK_DEBUG] MH_CreateHook OutputDebugStringA => " + MinHookStatusToString(status));
    }
    if (pTargetOutputDebugStringW)
    {
        MH_STATUS status = MH_CreateHook(pTargetOutputDebugStringW, &Hooked_OutputDebugStringW, reinterpret_cast<LPVOID *>(&pOriginalOutputDebugStringW));
        if (g_DebugHooks)
            WriteToDashboardLog("[HOOK_DEBUG] MH_CreateHook OutputDebugStringW => " + MinHookStatusToString(status));
    }

    const MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
    if (g_DebugHooks)
        WriteToDashboardLog("[HOOK_DEBUG] MH_EnableHook => " + MinHookStatusToString(enableStatus));
    return 0;
}
