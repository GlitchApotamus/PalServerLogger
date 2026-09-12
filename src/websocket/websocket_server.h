#pragma once

#include <string>

#include "core/runtime.h"

void EmitWebSocketDebug(const std::string &detail);
void RemoveWebSocketClient(SOCKET client);
void SendWebSocketText(SOCKET client, const std::string &message);
void BroadcastWebSocketLogs();
void WebSocketServerThread();
