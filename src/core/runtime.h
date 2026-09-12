#pragma once

#define _WINSOCKAPI_
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <MinHook.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

typedef BOOL(WINAPI *tWriteConsoleA)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteConsoleW)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL(WINAPI *tWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef void(WINAPI *tOutputDebugStringA)(LPCSTR);
typedef void(WINAPI *tOutputDebugStringW)(LPCWSTR);

extern tWriteConsoleA pOriginalWriteConsoleA;
extern tWriteConsoleW pOriginalWriteConsoleW;
extern tWriteFile pOriginalWriteFile;
extern tOutputDebugStringA pOriginalOutputDebugStringA;
extern tOutputDebugStringW pOriginalOutputDebugStringW;

extern std::string g_LogFilePath;
extern int g_MaxLogs;
extern std::string g_TimestampFormat;
extern std::string g_FileTimestampFormat;
extern int g_WebSocketPort;
extern std::string g_WebSocketHost;
extern bool g_WebSocketEnabled;
extern bool g_DebugHooks;
extern std::string g_WebSocketSecret;

extern std::queue<std::string> g_LogQueue;
extern std::mutex g_QueueMutex;
extern std::queue<std::string> g_WebSocketQueue;
extern std::mutex g_WebSocketQueueMutex;
extern std::vector<SOCKET> g_WebSocketClients;
extern std::mutex g_WebSocketClientsMutex;
extern std::unordered_map<std::string, std::uintmax_t> g_FallbackLogOffsets;
extern std::unordered_map<std::string, bool> g_FallbackLogReported;
extern std::mutex g_FallbackLogOffsetsMutex;
extern std::once_flag g_FallbackLogThreadFlag;
extern bool g_IsRunning;

extern std::string g_PendingLogChunk;
extern std::chrono::steady_clock::time_point g_LastAppendTime;
extern std::string g_LineBuffer;
extern std::chrono::steady_clock::time_point g_LastMessageTime;
extern std::string g_LastLoggedLine;
extern std::chrono::steady_clock::time_point g_LastLoggedLineTime;
extern std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_RecentLogLines;

std::string TrimString(const std::string &value);
std::string GenerateWebSocketSecret();
std::string ExtractWebSocketToken(const std::string &request);
bool IsWebSocketAuthorized(const std::string &request);
std::string MinHookStatusToString(MH_STATUS status);
void TraceHookCall(const std::string &name);
std::string BuildWebSocketLogPayload(const std::string &message);
std::string GetFormattedTimestamp();
std::string SanitizeFilenameComponent(std::string value);
void MigrateLegacyPalServerLoggerFolder(const std::filesystem::path &legacyLogDir, const std::filesystem::path &newLogDir);
bool IsKnownBackendNoise(const std::string &line);
bool LooksLikeGameServerLogPath(const std::filesystem::path &path);
std::vector<std::filesystem::path> FindFallbackLogFiles();
void TailFallbackLogFile(const std::filesystem::path &logPath);
void FallbackLogTailThread();
void StartFallbackLogTailThread();
std::string Base64Encode(const std::string &input);
std::string ComputeWebSocketAccept(const std::string &clientKey);
std::string BuildWebSocketFrame(const std::string &payload);
void InitializeLogEnvironment();
bool ShouldSkipDuplicateLogLine(const std::string &line);
void QueueWebSocketLog(const std::string &message);
void EmitWebSocketDebug(const std::string &detail);
void RemoveWebSocketClient(SOCKET client);
void SendWebSocketText(SOCKET client, const std::string &message);
void BroadcastWebSocketLogs();
void WebSocketServerThread();
