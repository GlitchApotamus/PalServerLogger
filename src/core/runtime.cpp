#include "core/runtime.h"
#include "core/logging.h"

using json = nlohmann::json;

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

std::string g_PendingLogChunk = "";
std::chrono::steady_clock::time_point g_LastAppendTime;
std::string g_LineBuffer = "";
std::chrono::steady_clock::time_point g_LastMessageTime = std::chrono::steady_clock::now();
std::string g_LastLoggedLine = "";
std::chrono::steady_clock::time_point g_LastLoggedLineTime = std::chrono::steady_clock::now();
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_RecentLogLines;

std::string TrimString(const std::string &value)
{
    const std::string whitespace = " \t\r\n";
    const size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";

    const size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string GenerateWebSocketSecret()
{
    std::string secret;

    FILE *wherePipe = _popen("where openssl 2>nul", "r");
    if (wherePipe)
    {
        char buffer[4096];
        if (fgets(buffer, sizeof(buffer), wherePipe) != nullptr)
        {
            std::string opensslPath = TrimString(buffer);
            if (!opensslPath.empty())
            {
                std::string command = "\"" + opensslPath + "\" rand -base64 48";
                FILE *randPipe = _popen(command.c_str(), "r");
                if (randPipe)
                {
                    char value[512];
                    if (fgets(value, sizeof(value), randPipe) != nullptr)
                    {
                        secret = TrimString(value);
                    }
                    _pclose(randPipe);
                }
            }
        }
        _pclose(wherePipe);
    }

    if (!secret.empty())
        return secret;

    BYTE randomBytes[48];
    HCRYPTPROV provider = 0;
    if (CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) || GetLastError() == NTE_EXISTS)
    {
        if (CryptGenRandom(provider, sizeof(randomBytes), randomBytes))
        {
            std::ostringstream oss;
            for (BYTE byte : randomBytes)
            {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            secret = oss.str();
        }
        CryptReleaseContext(provider, 0);
    }

    return secret.empty() ? "change-me-to-a-long-random-secret" : secret;
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

        const size_t pos = lowerRequest.find(lowerHeader);
        if (pos == std::string::npos)
            return "";

        const size_t valueStart = pos + header.length();
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

        const size_t pos = lowerRequest.find(lowerQueryToken);
        if (pos == std::string::npos)
            continue;

        const size_t valueStart = pos + queryToken.length();
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

    const std::string suppliedToken = ExtractWebSocketToken(request);
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
        "setting breakpad minidump appid",
        "system info/",
        "core info/",
        "system activity/",
        "api:",
        "modloader",
        "paldefender anti cheat",
        "paldefender wiki:",
        "rest api started on port",
        "[s_api]",
        "[system",
        "[core",
        "[generic",
        "steam interface",
        "steamcmdplugin",
        "rconplugin",
        "failed to access steam interface",
        "loading steam interface"};

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

    if (lowered.find("palserverlogs") != std::string::npos || lowered.find("palserverlogger") != std::string::npos)
        return false;

    if (lowered.find("\\config\\") != std::string::npos || lowered.find("/config/") != std::string::npos)
        return false;

    static const std::vector<std::string> preferredSegments = {
        "saved\\logs",
        "saved/logs",
        "saved\\logs\\",
        "saved/logs/",
        "palworldserver",
        "palworldserver.exe",
        "palserver",
        "server_log_",
        "palworld/logs",
        "palworld/saved/logs",
        "palworld\\saved\\logs",
        "pal\\saved\\logs",
        "pal/saved/logs",
        "pal\\saved\\logs\\",
        "pal/saved/logs/",
        "saved\\logs\\server",
        "saved/logs/server",
        "palserverlogs",
        "palserver/logs"};

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
        const std::filesystem::path parent1 = dllDir / "..";
        const std::filesystem::path parent2 = parent1 / "..";
        const std::filesystem::path parent3 = parent2 / "..";

        roots.push_back(dllDir);
        roots.push_back(dllDir / "logs");
        roots.push_back(parent1);
        roots.push_back(parent1 / "logs");
        roots.push_back(parent2);
        roots.push_back(parent2 / "logs");
        roots.push_back(parent2 / "Saved");
        roots.push_back(parent2 / "Saved" / "Logs");
        roots.push_back(parent3);
        roots.push_back(parent3 / "logs");
        roots.push_back(parent3 / "Saved");
        roots.push_back(parent3 / "Saved" / "Logs");
    }

    char currentDir[MAX_PATH] = {};
    if (GetCurrentDirectoryA(MAX_PATH, currentDir) > 0)
    {
        std::filesystem::path cwd(currentDir);
        roots.push_back(cwd);
        roots.push_back(cwd / "logs");
        roots.push_back(cwd / "Saved");
        roots.push_back(cwd / "Saved" / "Logs");
    }

    std::vector<std::filesystem::path> seen;
    for (const auto &root : roots)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec))
            continue;

        const std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
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

            const bool isLogLike = extension == ".log" || extension == ".txt" || filename.find("log") != std::string::npos;
            if (!isLogLike)
                continue;

            std::string pathLower = entry.path().string();
            std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            if (pathLower.find("palserverlogger") != std::string::npos || pathLower.find("\\config\\") != std::string::npos || pathLower.find("/config/") != std::string::npos)
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
        const auto it = g_FallbackLogOffsets.find(logPath.string());
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

    std::string line;
    std::stringstream lineStream(buffer);
    while (std::getline(lineStream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!line.empty() && !IsKnownBackendNoise(line))
            WriteToDashboardLog(line + "\n");
    }

    const std::uintmax_t newSize = std::filesystem::file_size(logPath, ec);
    std::lock_guard<std::mutex> lock(g_FallbackLogOffsetsMutex);
    g_FallbackLogOffsets[logPath.string()] = newSize;
}

void FallbackLogTailThread()
{
    while (g_IsRunning)
    {
        const std::vector<std::filesystem::path> logFiles = FindFallbackLogFiles();
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
        const unsigned char b1 = static_cast<unsigned char>(input[i]);
        const unsigned char b2 = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
        const unsigned char b3 = i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;

        const unsigned char x1 = (b1 >> 2) & 0x3F;
        const unsigned char x2 = ((b1 & 0x03) << 4) | ((b2 >> 4) & 0x0F);
        const unsigned char x3 = ((b2 & 0x0F) << 2) | ((b3 >> 6) & 0x03);
        const unsigned char x4 = b3 & 0x3F;

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
    const std::string digestInput = clientKey + wsGuid;

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

    const std::string sha1(reinterpret_cast<const char *>(hash), hashLen);
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

std::string GetFormattedTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &timeT);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, g_TimestampFormat.c_str());
    return oss.str();
}

void MigrateLegacyPalServerLoggerFolder(const std::filesystem::path &legacyLogDir, const std::filesystem::path &newLogDir)
{
    if (!std::filesystem::exists(legacyLogDir) || legacyLogDir == newLogDir)
        return;

    std::filesystem::create_directories(newLogDir);

    const std::filesystem::path legacyConfigFile = legacyLogDir / "config" / "logger_config.json";
    const std::filesystem::path migratedConfigFile = newLogDir / "Config.json";
    if (std::filesystem::exists(legacyConfigFile) && !std::filesystem::exists(migratedConfigFile))
    {
        std::error_code copyError;
        std::filesystem::copy_file(legacyConfigFile, migratedConfigFile, std::filesystem::copy_options::overwrite_existing, copyError);
    }

    std::error_code iteratorError;
    std::filesystem::recursive_directory_iterator it(legacyLogDir, std::filesystem::directory_options::skip_permission_denied, iteratorError);
    std::filesystem::recursive_directory_iterator end;

    for (; it != end; it.increment(iteratorError))
    {
        if (iteratorError)
        {
            iteratorError.clear();
            continue;
        }

        const std::filesystem::path currentPath = it->path();
        const std::filesystem::path relativePath = std::filesystem::relative(currentPath, legacyLogDir);
        if (relativePath.empty())
            continue;

        const std::string relativeString = relativePath.generic_string();
        if (relativeString == "config" || relativeString.rfind("config/", 0) == 0 || relativeString.rfind("config\\", 0) == 0)
            continue;

        const std::filesystem::path targetPath = newLogDir / relativePath;
        if (it->is_directory())
        {
            std::filesystem::create_directories(targetPath);
            continue;
        }

        std::error_code copyError;
        std::filesystem::copy_file(currentPath, targetPath, std::filesystem::copy_options::overwrite_existing, copyError);
    }

    std::error_code removeError;
    std::filesystem::remove_all(legacyLogDir, removeError);
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

    const std::string payload = "[" + GetFormattedTimestamp() + "] [WEBSOCKET] " + detail;
    const std::string frame = BuildWebSocketFrame(BuildWebSocketLogPayload(payload));

    std::lock_guard<std::mutex> lock(g_WebSocketClientsMutex);
    for (size_t i = 0; i < g_WebSocketClients.size();)
    {
        const SOCKET client = g_WebSocketClients[i];
        const int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
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
    const auto it = std::find(g_WebSocketClients.begin(), g_WebSocketClients.end(), client);
    if (it != g_WebSocketClients.end())
        g_WebSocketClients.erase(it);
}

void SendWebSocketText(SOCKET client, const std::string &message)
{
    if (client == INVALID_SOCKET || message.empty())
        return;

    const std::string frame = BuildWebSocketFrame(message);
    const int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
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
        const std::string frame = BuildWebSocketFrame(message);
        for (size_t i = 0; i < g_WebSocketClients.size();)
        {
            const SOCKET client = g_WebSocketClients[i];
            const int result = send(client, frame.c_str(), static_cast<int>(frame.size()), 0);
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

    const std::string startupWebSocketUrl = "ws://" + g_WebSocketHost + ":" + std::to_string(g_WebSocketPort);
    WriteToDashboardLog("[WEBSOCKET] listening on " + startupWebSocketUrl);
    EmitWebSocketDebug("server started on " + startupWebSocketUrl);

    while (g_IsRunning)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
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
        const int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0)
        {
            closesocket(clientSocket);
            continue;
        }

        const std::string request(buffer, static_cast<size_t>(received));
        std::string response;

        const auto keyPos = request.find("Sec-WebSocket-Key:");
        if (keyPos != std::string::npos)
        {
            if (!IsWebSocketAuthorized(request))
            {
                const std::string unauthorized = "HTTP/1.1 401 Unauthorized\r\n"
                                                 "Content-Type: text/plain\r\n"
                                                 "Content-Length: 0\r\n"
                                                 "Connection: close\r\n\r\n";
                send(clientSocket, unauthorized.c_str(), static_cast<int>(unauthorized.size()), 0);
                closesocket(clientSocket);
                continue;
            }

            const auto lineEnd = request.find("\r\n", keyPos);
            const std::string keyLine = request.substr(keyPos, lineEnd - keyPos);
            const auto colonPos = keyLine.find(':');
            std::string keyValue = keyLine.substr(colonPos + 1);
            keyValue.erase(0, keyValue.find_first_not_of(" \t\r\n"));
            keyValue.erase(keyValue.find_last_not_of(" \t\r\n") + 1);

            const std::string accept = ComputeWebSocketAccept(keyValue);
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

bool ShouldSkipDuplicateLogLine(const std::string &line)
{
    std::string normalized = line;
    while (!normalized.empty() && normalized.back() == '\r')
        normalized.pop_back();
    while (!normalized.empty() && normalized.back() == '\n')
        normalized.pop_back();

    if (normalized.empty())
        return true;

    std::string compact = normalized;
    std::replace(compact.begin(), compact.end(), '\t', ' ');
    while (compact.find("  ") != std::string::npos)
        compact.erase(compact.find("  "), 1);

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastLoggedLineTime).count();

    if (compact == g_LastLoggedLine && elapsedMs < 1000)
        return true;

    if (compact.rfind("[WEBSOCKET]", 0) == 0 || compact.rfind("[FALLBACK_LOG]", 0) == 0)
        return true;

    const auto recentIt = g_RecentLogLines.find(compact);
    if (recentIt != g_RecentLogLines.end())
    {
        const auto recentElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - recentIt->second).count();
        if (recentElapsedMs < 1000)
            return true;
    }

    g_LastLoggedLine = compact;
    g_LastLoggedLineTime = now;
    g_RecentLogLines[compact] = now;

    for (auto it = g_RecentLogLines.begin(); it != g_RecentLogLines.end();)
    {
        const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        if (ageMs > 15000)
            it = g_RecentLogLines.erase(it);
        else
            ++it;
    }

    return false;
}

void InitializeLogEnvironment()
{
    char path[MAX_PATH];
    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InitializeLogEnvironment, &hModule);
    GetModuleFileNameA(hModule, path, sizeof(path));

    const std::filesystem::path dllPath(path);
    const std::filesystem::path legacyLogDir = dllPath.parent_path() / "PalServerLogs";
    const std::filesystem::path logDir = dllPath.parent_path() / "PalServerLogger";
    const std::filesystem::path configPath = logDir / "Config.json";

    MigrateLegacyPalServerLoggerFolder(legacyLogDir, logDir);

    if (!std::filesystem::exists(logDir))
        std::filesystem::create_directory(logDir);

    const json defaultJson = {
        {"max_log_files", 5},
        {"timestamp_format", "%Y-%m-%d %H:%M:%S"},
        {"filename_timestamp_format", "%Y%m%d_%H%M%S"},
        {"websocket_enabled", true},
        {"websocket_port", 8765},
        {"websocket_host", "0.0.0.0"},
        {"websocket_secret", GenerateWebSocketSecret()},
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
                else if (it.key() == "websocket_secret")
                {
                    const std::string currentSecret = config["websocket_secret"].is_string() ? config["websocket_secret"].get<std::string>() : "";
                    if (currentSecret.empty() || currentSecret == "change-me-to-a-long-random-secret")
                    {
                        config["websocket_secret"] = GenerateWebSocketSecret();
                        configModified = true;
                    }
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

        if (config.contains("websocket_secret"))
        {
            WriteToDashboardLog("[WEBSOCKET] generated a new websocket secret and saved it to " + configPath.string());
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

    std::vector<std::filesystem::directory_entry> logFiles;
    for (const auto &entry : std::filesystem::directory_iterator(logDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".log")
            logFiles.push_back(entry);
    }

    if (logFiles.size() >= static_cast<size_t>(g_MaxLogs))
    {
        std::sort(logFiles.begin(), logFiles.end(), [](const auto &a, const auto &b)
                  { return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b); });

        const size_t toDelete = (logFiles.size() - g_MaxLogs) + 1;
        for (size_t i = 0; i < toDelete; ++i)
        {
            std::filesystem::remove(logFiles[i].path());
        }
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
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

    std::thread(LogWriterThread).detach();
    std::call_once(g_FallbackLogThreadFlag, StartFallbackLogTailThread);
    if (g_WebSocketEnabled)
    {
        std::thread(WebSocketServerThread).detach();
    }
}
