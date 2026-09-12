#include "core/logging.h"
#include "core/runtime.h"

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
                logFile.flush();
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void WriteToDashboardLog(const std::string &message)
{
    if (message.empty())
        return;

    std::lock_guard<std::mutex> lock(g_QueueMutex);
    const auto now = std::chrono::steady_clock::now();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastMessageTime).count();
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
