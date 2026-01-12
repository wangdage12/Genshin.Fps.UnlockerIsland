#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <Windows.h>

enum class LogLevel
{
    Info,
    Warning,
    Error,
    Debug   // ��������
};

class Logger
{
public:
    static Logger& GetInstance()
    {
        static Logger instance;
        return instance;
    }

    void Initialize(const std::wstring& logFilePath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open())
        {
            m_logFile.close();
        }
        
        m_logFile.open(logFilePath, std::ios::out | std::ios::app);
        if (!m_logFile.is_open())
        {
            return;
        }
        
        m_logFile << "\n========================================\n";
        m_logFile << "Genshin.Fps.UnlockerIsland Log Started\n";
        m_logFile << "Time: " << GetCurrentTimeString() << "\n";
        m_logFile << "========================================\n";
        m_logFile.flush();
    }

    void Log(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logFile.is_open())
        {
            return;
        }

        std::string levelStr;
        switch (level)
        {
        case LogLevel::Info:    levelStr = "[INFO]"; break;
        case LogLevel::Warning: levelStr = "[WARN]"; break;
        case LogLevel::Error:   levelStr = "[ERROR]"; break;
        case LogLevel::Debug:   levelStr = "[DEBUG]"; break;
        }

        m_logFile << GetCurrentTimeString() << " " << levelStr << " " << message << "\n";
        m_logFile.flush();
    }

    void LogException(const std::string& location, const std::string& exceptionMsg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logFile.is_open())
        {
            return;
        }

        m_logFile << GetCurrentTimeString() << " [EXCEPTION] Location: " << location 
                  << " | Message: " << exceptionMsg << "\n";
        m_logFile.flush();
    }

    void LogHook(const std::string& hookName, const std::string& status, const std::string& details = "")
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logFile.is_open())
        {
            return;
        }

        if (!details.empty())
        {
            m_logFile << GetCurrentTimeString() << " [HOOK] " << hookName << " - " << status 
                      << " | " << details << "\n";
        }
        else
        {
            m_logFile << GetCurrentTimeString() << " [HOOK] " << hookName << " - " << status << "\n";
        }
        m_logFile.flush();
    }

    void LogFunction(const std::string& functionName, const std::string& action)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logFile.is_open())
        {
            return;
        }

        m_logFile << GetCurrentTimeString() << " [FUNCTION] " << functionName << " - " << action << "\n";
        m_logFile.flush();
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open())
        {
            m_logFile << "\n========================================\n";
            m_logFile << "Genshin.Fps.UnlockerIsland Log Ended\n";
            m_logFile << "Time: " << GetCurrentTimeString() << "\n";
            m_logFile << "========================================\n\n";
            m_logFile.close();
        }
    }

private:
    Logger() = default;
    ~Logger()
    {
        Shutdown();
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetCurrentTimeString()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::ofstream m_logFile;
    std::mutex m_mutex;
};

#define LOG_INFO(msg)    Logger::GetInstance().Log(LogLevel::Info, msg)
#define LOG_WARNING(msg) Logger::GetInstance().Log(LogLevel::Warning, msg)
#define LOG_ERROR(msg)   Logger::GetInstance().Log(LogLevel::Error, msg)
#define LOG_DEBUG(msg)   Logger::GetInstance().Log(LogLevel::Debug, msg)
#define LOG_EXCEPTION(loc, msg) Logger::GetInstance().LogException(loc, msg)
#define LOG_HOOK(name, status, ...) Logger::GetInstance().LogHook(name, status, ##__VA_ARGS__)
#define LOG_FUNCTION(func, action) Logger::GetInstance().LogFunction(func, action)