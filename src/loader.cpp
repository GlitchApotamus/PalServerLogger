#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <functional>
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

// Global configuration object to hold translations at runtime
json g_config;

std::filesystem::path GetModuleDirectory()
{
    char path[MAX_PATH];
    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetModuleDirectory, &hModule);
    GetModuleFileNameA(hModule, path, sizeof(path));
    return std::filesystem::path(path).parent_path();
}

// Simple string formatting helper for translations (replaces {0}, {1}, etc.)
std::string FormatString(const std::string &fmt, const std::vector<std::string> &args)
{
    std::string result = fmt;
    for (size_t i = 0; i < args.size(); ++i)
    {
        std::string placeholder = "{" + std::to_string(i) + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos)
        {
            result.replace(pos, placeholder.length(), args[i]);
            pos += args[i].length();
        }
    }
    return result;
}

// Translation Lookup Function with Fallback
std::string Translate(const std::string &key, const std::string &fallback, const std::vector<std::string> &args = {})
{
    if (g_config.contains("translations") && g_config["translations"].contains(key) && g_config["translations"][key].is_string())
    {
        std::string customTemplate = g_config["translations"][key].get<std::string>();
        return FormatString(customTemplate, args);
    }
    return FormatString(fallback, args);
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

    // Fetch translated prefix dynamically (defaults to "ModLoader" if missing)
    std::string prefix = Translate("ModLoaderPrefix", "ModLoader");

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole && hConsole != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
        WORD saved_attributes = consoleInfo.wAttributes;

        std::string bracketPrefix = "[" + prefix + "]: [";
        std::string fullLine = bracketPrefix + levelStr + "] " + message + "\n";

        WriteFile(hConsole, bracketPrefix.c_str(), (DWORD)bracketPrefix.length(), &written, nullptr);

        SetConsoleTextAttribute(hConsole, colorAttr);
        WriteFile(hConsole, levelStr.c_str(), (DWORD)levelStr.length(), &written, nullptr);

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

    std::filesystem::path binDir = GetModuleDirectory();
    std::filesystem::path configPath = binDir / "d3d9_config.json";
    std::filesystem::path serverRoot = binDir.parent_path().parent_path().parent_path();

    const json defaultConfig = {
        {"load_dlls", json::array({"PalServerLogger.dll"})},
        {"UsePalDefender", false},
        {"translations", {{"ModLoaderPrefix", "ModLoader"}, {"ModThreadStarted", "Mod thread started."}, {"ConfigMissing", "Config missing. Generating default d3d9_config.json..."}, {"DefaultConfigCreated", "Default config created."}, {"AttemptingLoad", "Attempting to load: {0}"}, {"InjectedSuccess", "Injected {0}"}, {"InjectedFailed", "Failed to inject {0}. Windows Error Code: {1}"}, {"ConfigErrorMissingArray", "JSON does not contain 'load_dlls' array."}, {"ConfigErrorOpening", "Could not open config at: {0}"}}}};

    auto writeConfig = [&]()
    {
        std::ofstream outFile(configPath);
        if (outFile.is_open())
        {
            outFile << g_config.dump(4);
            outFile.close();
        }
    };

    // Recursively restore missing keys from defaults while preserving user overrides.
    std::function<bool(json &, const json &)> mergeMissingKeys =
        [&](json &target, const json &defaults) -> bool
    {
        bool changed = false;
        if (!defaults.is_object())
        {
            return false;
        }

        if (!target.is_object())
        {
            target = defaults;
            return true;
        }

        for (auto it = defaults.begin(); it != defaults.end(); ++it)
        {
            const std::string &key = it.key();
            const json &defaultValue = it.value();

            if (!target.contains(key))
            {
                target[key] = defaultValue;
                changed = true;
                continue;
            }

            if (defaultValue.is_object())
            {
                changed = mergeMissingKeys(target[key], defaultValue) || changed;
            }
        }

        return changed;
    };

    bool configUpdated = false;

    // 1. Auto-Generate config with default translations if missing
    if (!std::filesystem::exists(configPath))
    {
        g_config = defaultConfig;
        writeConfig();
    }
    else
    {
        // Load existing config
        std::ifstream configFile(configPath);
        if (configFile.is_open())
        {
            try
            {
                configFile >> g_config;
            }
            catch (const json::parse_error &e)
            {
                DebugLog(LOG_ERROR, std::string("JSON Formatting Error - ") + e.what());
                g_config = defaultConfig;
                configUpdated = true;
            }
        }

        configUpdated = mergeMissingKeys(g_config, defaultConfig) || configUpdated;
    }

    DebugLog(LOG_INFO, Translate("ModThreadStarted", "Mod thread started."));

    if (!g_config.contains("load_dlls") || !g_config["load_dlls"].is_array())
    {
        g_config["load_dlls"] = json::array();
        configUpdated = true;
    }

    const bool usePalDefender = g_config.value("UsePalDefender", false);
    bool hasPalDefender = false;
    for (const auto &mod : g_config["load_dlls"])
    {
        if (mod.is_string() && mod.get<std::string>() == "PalDefender.dll")
        {
            hasPalDefender = true;
            break;
        }
    }

    if (usePalDefender && !hasPalDefender)
    {
        g_config["load_dlls"].push_back("PalDefender.dll");
        configUpdated = true;
    }
    else if (!usePalDefender && hasPalDefender)
    {
        json filteredLoadDlls = json::array();
        for (const auto &mod : g_config["load_dlls"])
        {
            if (!(mod.is_string() && mod.get<std::string>() == "PalDefender.dll"))
            {
                filteredLoadDlls.push_back(mod);
            }
        }
        g_config["load_dlls"] = filteredLoadDlls;
        configUpdated = true;
    }

    if (configUpdated)
    {
        writeConfig();
    }

    if (g_config.contains("load_dlls") && g_config["load_dlls"].is_array())
    {
        for (const auto &mod : g_config["load_dlls"])
        {
            std::string modFilename = mod.get<std::string>();
            std::filesystem::path modPath = binDir / modFilename;
            std::string displayPath = std::filesystem::relative(modPath, serverRoot).string();

            DebugLog(LOG_INFO, Translate("AttemptingLoad", "Attempting to load: {0}", {displayPath}));

            HMODULE hMod = LoadLibraryA(modPath.string().c_str());
            if (hMod)
            {
                DebugLog(LOG_SUCCESS, Translate("InjectedSuccess", "Injected {0}", {modFilename}));
            }
            else
            {
                std::string errCode = std::to_string(GetLastError());
                DebugLog(LOG_ERROR, Translate("InjectedFailed", "Failed to inject {0}. Windows Error Code: {1}", {modFilename, errCode}));
            }
        }
    }
    else
    {
        DebugLog(LOG_ERROR, Translate("ConfigErrorMissingArray", "JSON does not contain 'load_dlls' array."));
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, ChainLoadDLLs, nullptr, 0, nullptr);
    }
    return TRUE;
}