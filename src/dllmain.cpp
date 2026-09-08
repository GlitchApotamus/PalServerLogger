#define _WINSOCKAPI_
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
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
#include <cstdint>
#include <nlohmann/json.hpp>
#include "MinHook.h"

using json = nlohmann::json;

typedef BOOL(WINAPI *tWriteConsoleA)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteConsoleW)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef void(WINAPI *tOutputDebugStringA)(LPCSTR);
typedef void(WINAPI *tOutputDebugStringW)(LPCWSTR);

tWriteConsoleA pOriginalWriteConsoleA = nullptr;
tWriteConsoleW pOriginalWriteConsoleW = nullptr;
tWriteFile pOriginalWriteFile = nullptr;
tOutputDebugStringA pOriginalOutputDebugStringA = nullptr;
tOutputDebugStringW pOriginalOutputDebugStringW = nullptr;

std::string g_LogFilePath = "";
int g_MaxLogs = 5;
std::string g_TimestampFormat = "%Y-%m-%d %H:%M:%S";
std::string g_FileTimestampFormat = "%Y%m%d_%H%M%S";
int g_WebSocketPort = 8765;
std::string g_WebSocketHost = "0.0.0.0";
bool g_WebSocketEnabled = true;

std::string BuildWebSocketLogPayload(const std::string &message)
{
    if (message.empty())
        return "";

    json payload = {
        {"type", "log"},
        {"message", message}};
    return payload.dump();
}

// Thread-safe async file queue to prevent freezing the game/console threads
std::queue<std::string> g_LogQueue;
std::mutex g_QueueMutex;
std::queue<std::string> g_WebSocketQueue;
std::mutex g_WebSocketQueueMutex;
std::vector<SOCKET> g_WebSocketClients;
std::mutex g_WebSocketClientsMutex;
bool g_IsRunning = true;

std::string GetFormattedTimestamp();

std::string Base64Encode(const std::string &input)
{
    static const std::string base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3)
    {
        unsigned char b1 = static_cast<unsigned char>(input[i]);
        unsigned char b2 = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
        unsigned char b3 = i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;

        unsigned char x1 = (b1 >> 2) & 0x3F;
        unsigned char x2 = ((b1 & 0x03) << 4) | ((b2 >> 4) & 0x0F);
        unsigned char x3 = ((b2 & 0x0F) << 2) | ((b3 >> 6) & 0x03);
        unsigned char x4 = b3 & 0x3F;

        output.push_back(base64Chars[x1]);
        output.push_back(base64Chars[x2]);
        output.push_back(i + 1 < input.size() ? base64Chars[x3] : '=');
        output.push_back(i + 2 < input.size() ? base64Chars[x4] : '=');
    }

    return output;
}

std::string ComputeWebSocketAccept(const std::string &clientKey)
{
    static const std::string wsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string digestInput = clientKey + wsGuid;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    DWORD hashLen = 20;
    BYTE hash[20] = {0};

    if (!CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash) ||
        !CryptHashData(hHash, reinterpret_cast<const BYTE *>(digestInput.c_str()), static_cast<DWORD>(digestInput.length()), 0) ||
        !CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
    {
        if (hHash)
            CryptDestroyHash(hHash);
        if (hProv)
            CryptReleaseContext(hProv, 0);
        return "";
    }

    std::string sha1(reinterpret_cast<const char *>(hash), hashLen);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return Base64Encode(sha1);
}

std::string BuildWebSocketFrame(const std::string &payload)
{
    std::string frame;
    frame.reserve(payload.size() + 2 + 16);
    frame.push_back(static_cast<char>(0x81));

    if (payload.size() <= 125)
    {
        frame.push_back(static_cast<char>(payload.size()));
    }
    else if (payload.size() <= 65535)
    {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<char>(payload.size() & 0xFF));
    }
    else
    {
        frame.push_back(static_cast<char>(127));
        frame.push_back(static_cast<char>(0));
        frame.push_back(static_cast<char>(0));
        frame.push_back(static_cast<char>(0));
        frame.push_back(static_cast<char>(0));
        frame.push_back(static_cast<char>((payload.size() >> 24) & 0xFF));
        frame.push_back(static_cast<char>((payload.size() >> 16) & 0xFF));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<char>(payload.size() & 0xFF));
    }

    frame.append(payload);
    return frame;
}

void QueueWebSocketLog(const std::string &message)
{
    if (message.empty())
        return;

    std::lock_guard<std::mutex> lock(g_WebSocketQueueMutex);
    g_WebSocketQueue.push(message);
}

void EmitWebSocketDebug(const std::string &detail)
{
    if (!g_WebSocketEnabled)
        return;

    std::string payload = "[" + GetFormattedTimestamp() + "] [WEBSOCKET] " + detail;
    std::string frame = BuildWebSocketFrame(BuildWebSocketLogPayload(payload));

    std::lock_guard<std::mutex> lock(g_WebSocketClientsMutex);
    for (size_t i = 0; i < g_WebSocketClients.size();)
    {
        SOCKET client = g_WebSocketClients[i];
        int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
        if (result == SOCKET_ERROR || result == 0)
        {
            closesocket(client);
            g_WebSocketClients.erase(g_WebSocketClients.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void RemoveWebSocketClient(SOCKET client)
{
    std::lock_guard<std::mutex> lock(g_WebSocketClientsMutex);
    auto it = std::find(g_WebSocketClients.begin(), g_WebSocketClients.end(), client);
    if (it != g_WebSocketClients.end())
        g_WebSocketClients.erase(it);
}

void SendWebSocketText(SOCKET client, const std::string &message)
{
    if (client == INVALID_SOCKET || message.empty())
        return;

    std::string frame = BuildWebSocketFrame(message);
    int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
    if (result == SOCKET_ERROR || result == 0)
    {
        closesocket(client);
        RemoveWebSocketClient(client);
    }
}

void BroadcastWebSocketLogs()
{
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(g_WebSocketQueueMutex);
        while (!g_WebSocketQueue.empty())
        {
            messages.push_back(g_WebSocketQueue.front());
            g_WebSocketQueue.pop();
        }
    }

    if (messages.empty())
        return;

    std::lock_guard<std::mutex> clientsLock(g_WebSocketClientsMutex);
    for (const auto &message : messages)
    {
        std::string frame = BuildWebSocketFrame(message);
        for (size_t i = 0; i < g_WebSocketClients.size();)
        {
            SOCKET client = g_WebSocketClients[i];
            int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
            if (result == SOCKET_ERROR || result == 0)
            {
                closesocket(client);
                g_WebSocketClients.erase(g_WebSocketClients.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
    }
}

void WebSocketServerThread()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return;

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(g_WebSocketPort));
    serverAddr.sin_addr.s_addr = inet_addr(g_WebSocketHost.c_str());

    if (bind(serverSocket, reinterpret_cast<SOCKADDR *>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    EmitWebSocketDebug("server started on ws://" + g_WebSocketHost + ":" + std::to_string(g_WebSocketPort));

    while (g_IsRunning)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0)
        {
            BroadcastWebSocketLogs();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!FD_ISSET(serverSocket, &readSet))
            continue;

        SOCKET clientSocket = ::accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
            continue;

        char buffer[4096] = {0};
        int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0)
        {
            closesocket(clientSocket);
            continue;
        }

        std::string request(buffer, static_cast<size_t>(received));
        std::string response;

        auto keyPos = request.find("Sec-WebSocket-Key:");
        if (keyPos != std::string::npos)
        {
            auto lineEnd = request.find("\r\n", keyPos);
            std::string keyLine = request.substr(keyPos, lineEnd - keyPos);
            auto colonPos = keyLine.find(':');
            std::string keyValue = keyLine.substr(colonPos + 1);
            keyValue.erase(0, keyValue.find_first_not_of(" \t\r\n"));
            keyValue.erase(keyValue.find_last_not_of(" \t\r\n") + 1);

            std::string accept = ComputeWebSocketAccept(keyValue);
            if (!accept.empty())
            {
                std::ostringstream httpResponse;
                httpResponse << "HTTP/1.1 101 Switching Protocols\r\n"
                             << "Upgrade: websocket\r\n"
                             << "Connection: Upgrade\r\n"
                             << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
                response = httpResponse.str();

                send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
                SendWebSocketText(clientSocket, R"({"type":"status","message":"PalServerLogger websocket connected"})");
                EmitWebSocketDebug("new websocket client connected");
                std::lock_guard<std::mutex> lock(g_WebSocketClientsMutex);
                g_WebSocketClients.push_back(clientSocket);
            }
            else
            {
                closesocket(clientSocket);
            }
        }
        else
        {
            closesocket(clientSocket);
        }

        BroadcastWebSocketLogs();
    }

    closesocket(serverSocket);
    WSACleanup();
}

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
        {"timestamp_format", "%Y-%m-%d %H:%M:%S"},
        {"filename_timestamp_format", "%Y%m%d_%H%M%S"},
        {"websocket_enabled", true},
        {"websocket_port", 8765},
        {"websocket_host", "0.0.0.0"}};

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
        if (config.contains("filename_timestamp_format"))
            g_FileTimestampFormat = config["filename_timestamp_format"];
        else
            g_FileTimestampFormat = g_TimestampFormat;
        if (config.contains("websocket_enabled"))
            g_WebSocketEnabled = config["websocket_enabled"];
        if (config.contains("websocket_port"))
            g_WebSocketPort = config["websocket_port"];
        if (config.contains("websocket_host"))
            g_WebSocketHost = config["websocket_host"];
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

    // Start background asynchronous disk-writer and websocket threads
    std::thread(LogWriterThread).detach();
    if (g_WebSocketEnabled)
    {
        std::thread(WebSocketServerThread).detach();
    }
}

std::string g_PendingLogChunk = "";
std::chrono::steady_clock::time_point g_LastAppendTime;

std::string g_LineBuffer = "";
std::chrono::steady_clock::time_point g_LastMessageTime = std::chrono::steady_clock::now();

void WriteToDashboardLog(const std::string &message)
{
    if (message.empty())
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
        QueueWebSocketLog(BuildWebSocketLogPayload(formattedLine.str()));
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
            QueueWebSocketLog(BuildWebSocketLogPayload(formattedLine.str()));
        }
    }

    // If the buffer gets too long without a newline, force flush it
    if (g_LineBuffer.length() > 256)
    {
        std::ostringstream formattedLine;
        formattedLine << "[" << GetFormattedTimestamp() << "] " << g_LineBuffer;
        g_LogQueue.push(formattedLine.str());
        QueueWebSocketLog(BuildWebSocketLogPayload(formattedLine.str()));
        g_LineBuffer.clear();
    }
}

BOOL WINAPI Hooked_WriteConsoleA(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    if (lpBuffer && nNumberOfCharsToWrite > 0)
    {
        std::string ansiString(static_cast<const char *>(lpBuffer), nNumberOfCharsToWrite);
        WriteToDashboardLog(ansiString);
    }
    return pOriginalWriteConsoleA(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved);
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

void WINAPI Hooked_OutputDebugStringA(LPCSTR lpOutputString)
{
    if (lpOutputString)
        WriteToDashboardLog(std::string(lpOutputString));

    if (pOriginalOutputDebugStringA)
        pOriginalOutputDebugStringA(lpOutputString);
}

void WINAPI Hooked_OutputDebugStringW(LPCWSTR lpOutputString)
{
    if (lpOutputString)
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, lpOutputString, -1, NULL, 0, NULL, NULL);
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
    if (MH_Initialize() != MH_OK)
        return 1;

    HMODULE hKernelBase = GetModuleHandleA("kernelbase.dll");
    if (!hKernelBase)
        hKernelBase = GetModuleHandleA("kernel32.dll");

    LPVOID pTargetWriteConsoleA = (LPVOID)GetProcAddress(hKernelBase, "WriteConsoleA");
    LPVOID pTargetWriteConsoleW = (LPVOID)GetProcAddress(hKernelBase, "WriteConsoleW");
    LPVOID pTargetWriteFile = (LPVOID)GetProcAddress(hKernelBase, "WriteFile");
    LPVOID pTargetOutputDebugStringA = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "OutputDebugStringA");
    LPVOID pTargetOutputDebugStringW = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "OutputDebugStringW");

    if (pTargetWriteConsoleA)
        MH_CreateHook(pTargetWriteConsoleA, &Hooked_WriteConsoleA, reinterpret_cast<LPVOID *>(&pOriginalWriteConsoleA));
    if (pTargetWriteConsoleW)
        MH_CreateHook(pTargetWriteConsoleW, &Hooked_WriteConsoleW, reinterpret_cast<LPVOID *>(&pOriginalWriteConsoleW));
    if (pTargetWriteFile)
        MH_CreateHook(pTargetWriteFile, &Hooked_WriteFile, reinterpret_cast<LPVOID *>(&pOriginalWriteFile));
    if (pTargetOutputDebugStringA)
        MH_CreateHook(pTargetOutputDebugStringA, &Hooked_OutputDebugStringA, reinterpret_cast<LPVOID *>(&pOriginalOutputDebugStringA));
    if (pTargetOutputDebugStringW)
        MH_CreateHook(pTargetOutputDebugStringW, &Hooked_OutputDebugStringW, reinterpret_cast<LPVOID *>(&pOriginalOutputDebugStringW));

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