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
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "MinHook.h"

void WriteToDashboardLog(const std::string &message);

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
std::string g_WebSocketHost = "127.0.0.1";
bool g_WebSocketEnabled = true;
bool g_DebugHooks = false;
std::string g_WebSocketSecret = "change-me-to-a-long-random-secret";

std::string TrimString(const std::string &value)
{
    const std::string whitespace = " \t\r\n";
    size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";

    size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string ExtractWebSocketToken(const std::string &request)
{
    std::string lowerRequest = request;
    std::transform(lowerRequest.begin(), lowerRequest.end(), lowerRequest.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });

    auto findTokenInHeader = [&](const std::string &headerName) -> std::string
    {
        std::string header = headerName + ":";
        std::string lowerHeader = header;
        std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });

        size_t pos = lowerRequest.find(lowerHeader);
        if (pos == std::string::npos)
            return "";

        size_t valueStart = pos + header.length();
        size_t valueEnd = request.find("\r\n", valueStart);
        if (valueEnd == std::string::npos)
            valueEnd = request.size();

        std::string value = request.substr(valueStart, valueEnd - valueStart);
        value = TrimString(value);
        if (value.rfind("Bearer ", 0) == 0)
            value = value.substr(7);
        return TrimString(value);
    };

    std::string token = findTokenInHeader("Authorization");
    if (!token.empty())
        return token;

    const std::string queryTokens[] = {"token=", "auth=", "secret="};
    for (const auto &queryToken : queryTokens)
    {
        std::string lowerQueryToken = queryToken;
        std::transform(lowerQueryToken.begin(), lowerQueryToken.end(), lowerQueryToken.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });

        size_t pos = lowerRequest.find(lowerQueryToken);
        if (pos == std::string::npos)
            continue;

        size_t valueStart = pos + queryToken.length();
        size_t valueEnd = request.find_first_of("& \r\n", valueStart);
        if (valueEnd == std::string::npos)
            valueEnd = request.size();

        return TrimString(request.substr(valueStart, valueEnd - valueStart));
    }

    return "";
}

bool IsWebSocketAuthorized(const std::string &request)
{
    if (g_WebSocketSecret.empty())
        return false;

    std::string suppliedToken = ExtractWebSocketToken(request);
    return !suppliedToken.empty() && suppliedToken == g_WebSocketSecret;
}

std::string MinHookStatusToString(MH_STATUS status)
{
    switch (status)
    {
    case MH_OK:
        return "MH_OK";
    case MH_ERROR_ALREADY_INITIALIZED:
        return "MH_ERROR_ALREADY_INITIALIZED";
    case MH_ERROR_NOT_INITIALIZED:
        return "MH_ERROR_NOT_INITIALIZED";
    case MH_ERROR_ALREADY_CREATED:
        return "MH_ERROR_ALREADY_CREATED";
    case MH_ERROR_NOT_CREATED:
        return "MH_ERROR_NOT_CREATED";
    case MH_ERROR_ENABLED:
        return "MH_ERROR_ENABLED";
    case MH_ERROR_DISABLED:
        return "MH_ERROR_DISABLED";
    case MH_ERROR_NOT_EXECUTABLE:
        return "MH_ERROR_NOT_EXECUTABLE";
    case MH_ERROR_UNSUPPORTED_FUNCTION:
        return "MH_ERROR_UNSUPPORTED_FUNCTION";
    case MH_ERROR_MEMORY_ALLOC:
        return "MH_ERROR_MEMORY_ALLOC";
    case MH_ERROR_MEMORY_PROTECT:
        return "MH_ERROR_MEMORY_PROTECT";
    case MH_ERROR_MODULE_NOT_FOUND:
        return "MH_ERROR_MODULE_NOT_FOUND";
    case MH_ERROR_FUNCTION_NOT_FOUND:
        return "MH_ERROR_FUNCTION_NOT_FOUND";
    default:
        return "MH_UNKNOWN";
    }
}

void TraceHookCall(const std::string &name)
{
    if (!g_DebugHooks)
        return;

    static bool writeConsoleACalled = false;
    static bool writeConsoleWCalled = false;
    static bool writeFileCalled = false;
    static bool outputDebugA = false;
    static bool outputDebugW = false;

    if (name == "WriteConsoleA" && !writeConsoleACalled)
    {
        WriteToDashboardLog("[HOOK_DEBUG] WriteConsoleA callback fired");
        writeConsoleACalled = true;
    }
    else if (name == "WriteConsoleW" && !writeConsoleWCalled)
    {
        WriteToDashboardLog("[HOOK_DEBUG] WriteConsoleW callback fired");
        writeConsoleWCalled = true;
    }
    else if (name == "WriteFile" && !writeFileCalled)
    {
        WriteToDashboardLog("[HOOK_DEBUG] WriteFile callback fired");
        writeFileCalled = true;
    }
    else if (name == "OutputDebugStringA" && !outputDebugA)
    {
        WriteToDashboardLog("[HOOK_DEBUG] OutputDebugStringA callback fired");
        outputDebugA = true;
    }
    else if (name == "OutputDebugStringW" && !outputDebugW)
    {
        WriteToDashboardLog("[HOOK_DEBUG] OutputDebugStringW callback fired");
        outputDebugW = true;
    }
}

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
std::unordered_map<std::string, std::uintmax_t> g_FallbackLogOffsets;
std::unordered_map<std::string, bool> g_FallbackLogReported;
std::mutex g_FallbackLogOffsetsMutex;
std::once_flag g_FallbackLogThreadFlag;
bool g_IsRunning = true;

std::string GetFormattedTimestamp();

bool IsKnownBackendNoise(const std::string &line)
{
    std::string normalized = line;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });

    static const std::vector<std::string> noisyPatterns = {
        "connectivity test",
        "ipv6 http connectivity test",
        "ipv6 udp connectivity test",
        "steamapi_init",
        "steamapi fail",
        "tried to access steam interface",
        "s_api fail",
        "authentication attempt",
        "uploaded file",
        "webserver started on http",
        "generic module",
        "amp is up to date",
        "loaded steamcmdplugin",
        "loaded rconplugin",
        "system info/",
        "core info/",
        "system activity/",
        "api:",
        "modloader",
        "paldefender",
        "[s_api]",
        "[system",
        "[core",
        "[generic",
        "steam interface",
        "game version is",
        "version is v1.0.4.102642",
        "running palworld dedicated server on"};

    for (const auto &pattern : noisyPatterns)
    {
        if (normalized.find(pattern) != std::string::npos)
            return true;
    }

    return false;
}

bool LooksLikeGameServerLogPath(const std::filesystem::path &path)
{
    std::string lowered = path.string();
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });

    static const std::vector<std::string> preferredSegments = {
        "saved\\logs",
        "saved/logs",
        "palworldserver",
        "palworldserver.exe",
        "palserver",
        "server_log_",
        "palserverlogs",
        "palserverlogs/config",
        "palworld/logs",
        "palworld/saved/logs",
        "palworld\\saved\\logs",
        "pal\\saved\\logs",
        "pal/saved/logs"};

    for (const auto &segment : preferredSegments)
    {
        if (lowered.find(segment) != std::string::npos)
            return true;
    }

    return false;
}

std::vector<std::filesystem::path> FindFallbackLogFiles()
{
    std::vector<std::filesystem::path> candidates;

    char modulePath[MAX_PATH] = {};
    HMODULE thisModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&FindFallbackLogFiles, &thisModule) &&
        thisModule)
    {
        GetModuleFileNameA(thisModule, modulePath, MAX_PATH);
    }

    std::filesystem::path dllDir = std::filesystem::path(modulePath).parent_path();
    std::vector<std::filesystem::path> roots;

    if (!dllDir.empty())
    {
        roots.push_back(dllDir);
        roots.push_back(dllDir / "logs");
        roots.push_back(dllDir / "..");
        roots.push_back(dllDir / ".." / "logs");
        roots.push_back(dllDir / ".." / "..");
        roots.push_back(dllDir / ".." / ".." / "logs");
    }

    char currentDir[MAX_PATH] = {};
    if (GetCurrentDirectoryA(MAX_PATH, currentDir) > 0)
    {
        roots.push_back(std::filesystem::path(currentDir));
        roots.push_back(std::filesystem::path(currentDir) / "logs");
    }

    std::vector<std::filesystem::path> seen;
    for (const auto &root : roots)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec))
            continue;

        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(root, options, ec);
        std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            const auto &entry = *it;
            if (!entry.is_regular_file(ec))
                continue;

            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            std::string filename = entry.path().filename().string();
            std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            bool isLogLike = extension == ".log" || extension == ".txt" || filename.find("log") != std::string::npos;
            if (!isLogLike)
                continue;

            if (!LooksLikeGameServerLogPath(entry.path()))
                continue;

            bool alreadySeen = false;
            for (const auto &existing : seen)
            {
                if (existing == entry.path())
                {
                    alreadySeen = true;
                    break;
                }
            }
            if (!alreadySeen)
            {
                seen.push_back(entry.path());
            }
        }
    }

    return seen;
}

void TailFallbackLogFile(const std::filesystem::path &logPath)
{
    std::error_code ec;
    if (!std::filesystem::exists(logPath, ec) || !std::filesystem::is_regular_file(logPath, ec))
        return;

    {
        std::lock_guard<std::mutex> lock(g_FallbackLogOffsetsMutex);
        if (g_DebugHooks && g_FallbackLogReported.find(logPath.string()) == g_FallbackLogReported.end())
        {
            g_FallbackLogReported[logPath.string()] = true;
            WriteToDashboardLog("[FALLBACK_LOG] watching candidate log file: " + logPath.string());
        }
    }

    std::uintmax_t currentOffset = 0;
    {
        std::lock_guard<std::mutex> lock(g_FallbackLogOffsetsMutex);
        auto it = g_FallbackLogOffsets.find(logPath.string());
        if (it == g_FallbackLogOffsets.end())
        {
            currentOffset = std::filesystem::file_size(logPath, ec);
            g_FallbackLogOffsets[logPath.string()] = currentOffset;
            return;
        }
        currentOffset = it->second;
    }

    std::ifstream stream(logPath, std::ios::binary | std::ios::in);
    if (!stream.is_open())
        return;

    stream.seekg(static_cast<std::streamoff>(currentOffset), std::ios::beg);
    std::string buffer;
    char chunk[4096];

    while (stream.read(chunk, sizeof(chunk)) || stream.gcount() > 0)
    {
        buffer.append(chunk, static_cast<size_t>(stream.gcount()));
    }

    if (buffer.empty())
        return;

    std::string working = buffer;
    std::string line;
    std::stringstream lineStream(working);
    while (std::getline(lineStream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!line.empty() && !IsKnownBackendNoise(line) &&
            line.find("[info] Game version is") == std::string::npos &&
            line.find("Running Palworld dedicated server on") == std::string::npos)
            WriteToDashboardLog(line + "\n");
    }

    std::uintmax_t newSize = std::filesystem::file_size(logPath, ec);
    std::lock_guard<std::mutex> lock(g_FallbackLogOffsetsMutex);
    g_FallbackLogOffsets[logPath.string()] = newSize;
}

void FallbackLogTailThread()
{
    while (g_IsRunning)
    {
        std::vector<std::filesystem::path> logFiles = FindFallbackLogFiles();
        for (const auto &logPath : logFiles)
        {
            TailFallbackLogFile(logPath);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void StartFallbackLogTailThread()
{
    std::thread(FallbackLogTailThread).detach();
}

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
            if (!IsWebSocketAuthorized(request))
            {
                std::string unauthorized = "HTTP/1.1 401 Unauthorized\r\n"
                                           "Content-Type: text/plain\r\n"
                                           "Content-Length: 0\r\n"
                                           "Connection: close\r\n\r\n";
                send(clientSocket, unauthorized.c_str(), static_cast<int>(unauthorized.size()), 0);
                closesocket(clientSocket);
                continue;
            }

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
        {"websocket_host", "127.0.0.1"},
        {"websocket_secret", "change-me-to-a-long-random-secret"},
        {"debug_hooks", false}};

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
        if (config.contains("websocket_secret"))
            g_WebSocketSecret = config["websocket_secret"];
        if (config.contains("debug_hooks"))
            g_DebugHooks = config["debug_hooks"];
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

    // Start background asynchronous disk-writer, websocket, and fallback log tailers.
    std::thread(LogWriterThread).detach();
    std::call_once(g_FallbackLogThreadFlag, StartFallbackLogTailThread);
    if (g_WebSocketEnabled)
    {
        std::thread(WebSocketServerThread).detach();
    }
}

std::string g_PendingLogChunk = "";
std::chrono::steady_clock::time_point g_LastAppendTime;

std::string g_LineBuffer = "";
std::chrono::steady_clock::time_point g_LastMessageTime = std::chrono::steady_clock::now();
std::string g_LastLoggedLine = "";
std::chrono::steady_clock::time_point g_LastLoggedLineTime = std::chrono::steady_clock::now();

bool ShouldSkipDuplicateLogLine(const std::string &line)
{
    std::string normalized = line;
    while (!normalized.empty() && normalized.back() == '\r')
        normalized.pop_back();
    while (!normalized.empty() && normalized.back() == '\n')
        normalized.pop_back();

    if (normalized.empty())
        return true;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastLoggedLineTime).count();

    if (normalized == g_LastLoggedLine && elapsedMs < 5000)
        return true;

    g_LastLoggedLine = normalized;
    g_LastLoggedLineTime = now;
    return false;
}

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

        if (!singleLine.empty() && !ShouldSkipDuplicateLogLine(singleLine))
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
        if (!ShouldSkipDuplicateLogLine(g_LineBuffer))
        {
            std::ostringstream formattedLine;
            formattedLine << "[" << GetFormattedTimestamp() << "] " << g_LineBuffer;
            g_LogQueue.push(formattedLine.str());
            QueueWebSocketLog(BuildWebSocketLogPayload(formattedLine.str()));
        }
        g_LineBuffer.clear();
    }
}

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
    TraceHookCall("WriteFile");
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

    MH_STATUS initStatus = MH_Initialize();
    if (g_DebugHooks)
        WriteToDashboardLog("[HOOK_DEBUG] MH_Initialize => " + MinHookStatusToString(initStatus));
    if (initStatus != MH_OK)
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

    MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
    if (g_DebugHooks)
        WriteToDashboardLog("[HOOK_DEBUG] MH_EnableHook => " + MinHookStatusToString(enableStatus));
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