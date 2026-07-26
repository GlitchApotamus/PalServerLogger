#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#pragma comment(linker, "/export:Direct3DCreate9=c:\\windows\\system32\\d3d9.Direct3DCreate9")
#pragma comment(linker, "/export:Direct3DCreate9Ex=c:\\windows\\system32\\d3d9.Direct3DCreate9Ex")
#pragma comment(linker, "/export:D3DPERF_BeginEvent=c:\\windows\\system32\\d3d9.D3DPERF_BeginEvent")
#pragma comment(linker, "/export:D3DPERF_EndEvent=c:\\windows\\system32\\d3d9.D3DPERF_EndEvent")
#pragma comment(linker, "/export:D3DPERF_GetStatus=c:\\windows\\system32\\d3d9.D3DPERF_GetStatus")
#pragma comment(linker, "/export:D3DPERF_QueryRepeatFrame=c:\\windows\\system32\\d3d9.D3DPERF_QueryRepeatFrame")
#pragma comment(linker, "/export:D3DPERF_SetMarker=c:\\windows\\system32\\d3d9.D3DPERF_SetMarker")
#pragma comment(linker, "/export:D3DPERF_SetOptions=c:\\windows\\system32\\d3d9.D3DPERF_SetOptions")
#pragma comment(linker, "/export:D3DPERF_SetRegion=c:\\windows\\system32\\d3d9.D3DPERF_SetRegion")

std::filesystem::path GetModuleDirectory()
{
    char path[MAX_PATH];
    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetModuleDirectory, &hModule);
    GetModuleFileNameA(hModule, path, sizeof(path));
    return std::filesystem::path(path).parent_path();
}

// ---------------------------------------------------------
// Colorized Logging System
// ---------------------------------------------------------
enum LogLevel
{
    LOG_INFO,
    LOG_SUCCESS,
    LOG_ERROR
};
void DebugLog(LogLevel level, const std::string &message)
{
    std::string levelStr;
    WORD colorAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    switch (level)
    {
    case LOG_INFO:
        levelStr = "INFO";
        colorAttr = FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE;
        break;
    case LOG_SUCCESS:
        levelStr = "SUCCESS";
        colorAttr = FOREGROUND_INTENSITY | FOREGROUND_GREEN;
        break;
    case LOG_ERROR:
        levelStr = "ERROR";
        colorAttr = FOREGROUND_INTENSITY | FOREGROUND_RED;
        break;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole && hConsole != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
        WORD saved_attributes = consoleInfo.wAttributes;

        // Construct the full string
        std::string fullLine = "[ModLoader]: [" + levelStr + "] " + message + "\n";

        // Print "[ModLoader]: ["
        WriteFile(hConsole, "[ModLoader]: [", 14, &written, nullptr);

        // Print Colorized Level
        SetConsoleTextAttribute(hConsole, colorAttr);
        WriteFile(hConsole, levelStr.c_str(), (DWORD)levelStr.length(), &written, nullptr);

        // Print "] <message>"
        SetConsoleTextAttribute(hConsole, saved_attributes);
        std::string suffix = "] " + message + "\n";
        WriteFile(hConsole, suffix.c_str(), (DWORD)suffix.length(), &written, nullptr);
    }
}
// ---------------------------------------------------------
// Mod Loader Thread
// ---------------------------------------------------------
DWORD WINAPI ChainLoadDLLs(LPVOID lpParam)
{
    Sleep(500);

    DebugLog(LOG_INFO, "Mod thread started.");

    std::filesystem::path binDir = GetModuleDirectory();
    std::filesystem::path configPath = binDir / "d3d9_config.json";

    // Step up three directories (Win64 -> Binaries -> Pal -> PalServer) to find the root
    std::filesystem::path serverRoot = binDir.parent_path().parent_path().parent_path();

    // Auto-Generate config
    if (!std::filesystem::exists(configPath))
    {
        DebugLog(LOG_INFO, "Config missing. Generating default d3d9_config.json...");

        std::ofstream defaultConfig(configPath);
        if (defaultConfig.is_open())
        {
            json defaultJson = {
                {"load_dlls", json::array({"PalServerLogger.dll"})}};
            defaultConfig << defaultJson.dump(4);
            defaultConfig.close();

            DebugLog(LOG_SUCCESS, "Default config created.");
        }
        else
        {
            DebugLog(LOG_ERROR, "Failed to create default config file.");
            return 1;
        }
    }

    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        DebugLog(LOG_ERROR, "Could not open config at: " + configPath.string());
        return 1;
    }

    json config;
    try
    {
        configFile >> config;

        if (config.contains("load_dlls") && config["load_dlls"].is_array())
        {
            for (const auto &mod : config["load_dlls"])
            {
                std::string modFilename = mod.get<std::string>();
                std::filesystem::path modPath = binDir / modFilename;

                // Format the string relative to the server root for clean logging
                std::string displayPath = std::filesystem::relative(modPath, serverRoot).string();

                DebugLog(LOG_INFO, "Attempting to load: " + displayPath);

                // Still use the absolute path for the actual memory injection to prevent working directory bugs
                HMODULE hMod = LoadLibraryA(modPath.string().c_str());
                if (hMod)
                {
                    DebugLog(LOG_SUCCESS, "Injected " + modFilename);
                }
                else
                {
                    DebugLog(LOG_ERROR, "Failed to inject " + modFilename + ". Windows Error Code: " + std::to_string(GetLastError()));
                }
            }
        }
        else
        {
            DebugLog(LOG_ERROR, "JSON does not contain 'load_dlls' array.");
        }
    }
    catch (const json::parse_error &e)
    {
        DebugLog(LOG_ERROR, std::string("JSON Formatting Error - ") + e.what());
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // REMOVED: The manual WriteFile call that was causing the duplicate/garbage text.

        // Create the thread to handle everything else
        CreateThread(nullptr, 0, ChainLoadDLLs, nullptr, 0, nullptr);
    }
    return TRUE;
}