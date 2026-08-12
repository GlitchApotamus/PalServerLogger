#include <windows.h>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <mutex>
#include <queue>
#include <thread>
#include <nlohmann/json.hpp>
#include "MinHook.h"

using json = nlohmann::json;

typedef BOOL(WINAPI *tWriteConsoleW)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);

tWriteConsoleW pOriginalWriteConsoleW = nullptr;
tWriteFile pOriginalWriteFile = nullptr;

std::string g_LogFilePath = "";
int g_MaxLogs = 5;
std::string g_TimestampFormat = "%Y-%m-%d %H:%M:%S";
std::string g_FileTimestampFormat = "%Y%m%d_%H%M%S";

// Thread-safe async file queue to prevent freezing the game/console threads
std::queue<std::string> g_LogQueue;
std::mutex g_QueueMutex;
bool g_IsRunning = true;

std::string GetFormattedTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &timeT);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, g_TimestampFormat.c_str());
    return oss.str();
}

std::string SanitizeFilenameComponent(std::string value)
{
    static const std::string invalidChars = "<>:\"/\\|?*";
    for (char &ch : value)
    {
        if (ch < 32 || invalidChars.find(ch) != std::string::npos)
        {
            ch = '_';
        }
    }
    return value;
}

// Instant-write background thread
void LogWriterThread()
{
    while (g_IsRunning)
    {
        std::vector<std::string> localBatch;
        {
            std::lock_guard<std::mutex> lock(g_QueueMutex);
            while (!g_LogQueue.empty())
            {
                localBatch.push_back(g_LogQueue.front());
                g_LogQueue.pop();
            }
        }

        if (!localBatch.empty() && !g_LogFilePath.empty())
        {
            std::ofstream logFile(g_LogFilePath, std::ios_base::app | std::ios_base::binary);
            if (logFile.is_open())
            {
                for (const auto &line : localBatch)
                {
                    logFile.write((line + "\n").c_str(), line.length() + 1);
                }
                logFile.flush(); // Ensure it hits disk immediately
            }
        }
        else
        {
            // Sleep briefly to avoid high CPU usage when idle, but wake up fast (5ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

// 1. Initialize Log Environment with Auto-Config, Rotation, and Timestamp Format
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

    if (!std::filesystem::exists(logDir))
        std::filesystem::create_directory(logDir);
    if (!std::filesystem::exists(configDir))
        std::filesystem::create_directory(configDir);

    json defaultJson = {
        {"max_log_files", 5},
        {"timestamp_format", "%Y-%m-%d %H:%M:%S"}};

    bool configModified = false;
    json config;

    if (!std::filesystem::exists(configPath))
    {
        config = defaultJson;
        configModified = true;
    }
    else
    {
        std::ifstream f(configPath);
        try
        {
            f >> config;
            f.close();

            for (auto it = defaultJson.begin(); it != defaultJson.end(); ++it)
            {
                if (!config.contains(it.key()))
                {
                    config[it.key()] = it.value();
                    configModified = true;
                }
            }
        }
        catch (...)
        {
            config = defaultJson;
            configModified = true;
        }
    }

    if (configModified)
    {
        std::ofstream defaultConfig(configPath);
        if (defaultConfig.is_open())
        {
            defaultConfig << config.dump(4);
        }
    }

    try
    {
        if (config.contains("max_log_files"))
            g_MaxLogs = config["max_log_files"];
        if (config.contains("timestamp_format"))
            g_TimestampFormat = config["timestamp_format"];
        // Keep filename format aligned with user timestamp format unless an explicit override exists.
        if (config.contains("filename_timestamp_format"))
            g_FileTimestampFormat = config["filename_timestamp_format"];
        else
            g_FileTimestampFormat = g_TimestampFormat;
    }
    catch (...)
    {
    }

    // Log Rotation
    std::vector<std::filesystem::directory_entry> logFiles;
    for (const auto &entry : std::filesystem::directory_iterator(logDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".log")
            logFiles.push_back(entry);
    }

    if (logFiles.size() >= (size_t)g_MaxLogs)
    {
        std::sort(logFiles.begin(), logFiles.end(), [](const auto &a, const auto &b)
                  { return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b); });

        size_t toDelete = (logFiles.size() - g_MaxLogs) + 1;
        for (size_t i = 0; i < toDelete; ++i)
        {
            std::filesystem::remove(logFiles[i].path());
        }
    }

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);

    std::ostringstream timestampStream;
    timestampStream << std::put_time(&timeinfo, g_FileTimestampFormat.c_str());
    std::string safeTimestamp = SanitizeFilenameComponent(timestampStream.str());
    if (safeTimestamp.empty())
    {
        std::ostringstream fallback;
        fallback << std::put_time(&timeinfo, "%Y%m%d_%H%M%S");
        safeTimestamp = fallback.str();
    }

    std::stringstream ss;
    ss << "server_log_" << safeTimestamp << ".log";
    g_LogFilePath = (logDir / ss.str()).string();

    // Start background asynchronous disk-writer thread
    std::thread(LogWriterThread).detach();
}

std::string g_PendingLogChunk = "";
std::chrono::steady_clock::time_point g_LastAppendTime;

std::string g_LineBuffer = "";
std::chrono::steady_clock::time_point g_LastMessageTime = std::chrono::steady_clock::now();

void WriteToDashboardLog(const std::string &message)
{
    if (g_LogFilePath.empty() || message.empty())
        return;

    std::lock_guard<std::mutex> lock(g_QueueMutex);
    auto now = std::chrono::steady_clock::now();

    // If it's been more than 50ms since the last chunk, treat any lingering buffer as a complete line
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastMessageTime).count();
    if (elapsed > 50 && !g_LineBuffer.empty())
    {
        std::ostringstream formattedLine;
        formattedLine << "[" << GetFormattedTimestamp() << "] " << g_LineBuffer;
        g_LogQueue.push(formattedLine.str());
        g_LineBuffer.clear();
    }

    g_LineBuffer += message;
    g_LastMessageTime = now;

    size_t pos = 0;
    while ((pos = g_LineBuffer.find('\n')) != std::string::npos)
    {
        std::string singleLine = g_LineBuffer.substr(0, pos);
        g_LineBuffer.erase(0, pos + 1);

        if (!singleLine.empty() && singleLine.back() == '\r')
        {
            singleLine.pop_back();
        }

        if (!singleLine.empty())
        {
            std::ostringstream formattedLine;
            formattedLine << "[" << GetFormattedTimestamp() << "] " << singleLine;
            g_LogQueue.push(formattedLine.str());
        }
    }

    // If the buffer gets too long without a newline, force flush it
    if (g_LineBuffer.length() > 256)
    {
        std::ostringstream formattedLine;
        formattedLine << "[" << GetFormattedTimestamp() << "] " << g_LineBuffer;
        g_LogQueue.push(formattedLine.str());
        g_LineBuffer.clear();
    }
}

BOOL WINAPI Hooked_WriteConsoleW(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    if (lpBuffer && nNumberOfCharsToWrite > 0)
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)lpBuffer, nNumberOfCharsToWrite, NULL, 0, NULL, NULL);
        if (size_needed > 0)
        {
            std::string utf8String(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)lpBuffer, nNumberOfCharsToWrite, &utf8String[0], size_needed, NULL, NULL);
            WriteToDashboardLog(utf8String);
        }
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
        g_IsRunning = false;
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}