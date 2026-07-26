#include <windows.h>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "MinHook.h"

using json = nlohmann::json;

typedef BOOL(WINAPI *tWriteConsoleW)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);

tWriteConsoleW pOriginalWriteConsoleW = nullptr;
tWriteFile pOriginalWriteFile = nullptr;

std::string g_LogFilePath = "";

// 1. Initialize Log Environment with Auto-Config and Rotation
void InitializeLogEnvironment()
{
    char path[MAX_PATH];
    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InitializeLogEnvironment, &hModule);
    GetModuleFileNameA(hModule, path, sizeof(path));

    std::filesystem::path dllPath(path);
    std::filesystem::path logDir = dllPath.parent_path() / "PalServerLogs";
    std::filesystem::path configDir = logDir / "config";
    std::filesystem::path configPath = configDir / "logger_config.json";

    // Create directories
    if (!std::filesystem::exists(logDir))
        std::filesystem::create_directory(logDir);
    if (!std::filesystem::exists(configDir))
        std::filesystem::create_directory(configDir);

    // Load or Create Config
    int maxLogs = 5;
    if (!std::filesystem::exists(configPath))
    {
        std::ofstream defaultConfig(configPath);
        json defaultJson = {{"max_log_files", 5}};
        defaultConfig << defaultJson.dump(4);
    }
    else
    {
        std::ifstream f(configPath);
        json config;
        try
        {
            f >> config;
            if (config.contains("max_log_files"))
                maxLogs = config["max_log_files"];
        }
        catch (...)
        {
        }
    }

    // Log Rotation
    std::vector<std::filesystem::directory_entry> logFiles;
    for (const auto &entry : std::filesystem::directory_iterator(logDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
            logFiles.push_back(entry);
    }

    if (logFiles.size() >= (size_t)maxLogs)
    {
        std::sort(logFiles.begin(), logFiles.end(), [](const auto &a, const auto &b)
                  { return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b); });

        size_t toDelete = (logFiles.size() - maxLogs) + 1;
        for (size_t i = 0; i < toDelete; ++i)
        {
            std::filesystem::remove(logFiles[i].path());
        }
    }

    // Current Log File
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);

    std::stringstream ss;
    ss << "server_log_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << ".txt";
    g_LogFilePath = (logDir / ss.str()).string();
}

void WriteToDashboardLog(const std::string &message)
{
    if (g_LogFilePath.empty())
        return;
    std::ofstream logFile(g_LogFilePath, std::ios_base::app | std::ios_base::binary);
    if (logFile.is_open())
    {
        logFile.write(message.c_str(), message.length());
    }
}

BOOL WINAPI Hooked_WriteConsoleW(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    if (lpBuffer && nNumberOfCharsToWrite > 0)
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)lpBuffer, nNumberOfCharsToWrite, NULL, 0, NULL, NULL);
        std::string utf8String(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)lpBuffer, nNumberOfCharsToWrite, &utf8String[0], size_needed, NULL, NULL);
        WriteToDashboardLog(utf8String);
    }
    return pOriginalWriteConsoleW(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved);
}

BOOL WINAPI Hooked_WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    if (hFile == GetStdHandle(STD_OUTPUT_HANDLE) || hFile == GetStdHandle(STD_ERROR_HANDLE) || GetFileType(hFile) == FILE_TYPE_CHAR)
    {
        if (lpBuffer && nNumberOfBytesToWrite > 0)
        {
            std::string str((const char *)lpBuffer, nNumberOfBytesToWrite);
            WriteToDashboardLog(str);
        }
    }
    return pOriginalWriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

DWORD WINAPI InitializeConsoleHooks(LPVOID lpParam)
{
    InitializeLogEnvironment();
    if (MH_Initialize() != MH_OK)
        return 1;

    HMODULE hKernelBase = GetModuleHandleA("kernelbase.dll");
    if (!hKernelBase)
        hKernelBase = GetModuleHandleA("kernel32.dll");

    LPVOID pTargetWriteConsoleW = (LPVOID)GetProcAddress(hKernelBase, "WriteConsoleW");
    LPVOID pTargetWriteFile = (LPVOID)GetProcAddress(hKernelBase, "WriteFile");

    if (pTargetWriteConsoleW)
        MH_CreateHook(pTargetWriteConsoleW, &Hooked_WriteConsoleW, reinterpret_cast<LPVOID *>(&pOriginalWriteConsoleW));
    if (pTargetWriteFile)
        MH_CreateHook(pTargetWriteFile, &Hooked_WriteFile, reinterpret_cast<LPVOID *>(&pOriginalWriteFile));

    MH_EnableHook(MH_ALL_HOOKS);
    return 0;
}

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
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}