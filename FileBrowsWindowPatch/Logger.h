#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <Windows.h>
#include <shlobj.h>
#include <type_traits>
#include <atomic>
#include <DbgHelp.h>
#include <filesystem>

#pragma comment(lib, "DbgHelp.lib")

class Logger {
public:
    enum class Level {
        LEVEL_FATAL = 0,  // 严重错误（弹窗+转储）
        LEVEL_ERROR,      // 错误
        LEVEL_WARN,       // 警告
        LEVEL_INFO,       // 信息
        LEVEL_DEBUG       // 调试
    };

    static Logger& GetInstance();
    void Initialize();

    void SetLevel(Level level) {
        currentLevel_ = level;
    }

    template<typename... Args>
    void Log(Level level, const std::string& location, Args&&... args) {
        const bool isDebugBuild = true;

        if (static_cast<int>(level) > static_cast<int>(currentLevel_.load())) {
            return;
        }

        if (level == Level::LEVEL_DEBUG && !isDebugBuild) {
            return;
        }

        if (level == Level::LEVEL_FATAL) {
            LogFatalImpl(nullptr, location, FormatArgs(std::forward<Args>(args)...));
        }
        else {
            LogImpl(level, location, FormatArgs(std::forward<Args>(args)...));
        }
    }

    void LogFatalImpl(PEXCEPTION_POINTERS exception, const std::string& location, const std::string& message);

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static std::string WideToUTF8(const std::wstring& wstr);

    template <typename T>
    static auto FormatArg(T&& arg) -> decltype(auto) {
        if constexpr (std::is_same_v<std::decay_t<T>, std::wstring> ||
            std::is_same_v<std::decay_t<T>, wchar_t*> ||
            std::is_same_v<std::decay_t<T>, const wchar_t*>) {
            return WideToUTF8(arg);
        }
        else {
            return std::forward<T>(arg);
        }
    }

    template<typename... Args>
    std::string FormatArgs(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << FormatArg(std::forward<Args>(args)));
        return oss.str();
    }

    void LogImpl(Level level, const std::string& location, const std::string& message);
    void CheckAndRotateLogFile();
    std::string GetStackTrace();
    std::string CreateMiniDump(PEXCEPTION_POINTERS exception);
    std::string GetCurrentTime();
    std::string GetLevelString(Level level);

    std::ofstream logFile_;
    //std::mutex mutex_;
    std::string logDirPath_;
    std::string dumpDirPath_;
    std::string currentLogDate_;
    bool initialized_ = false;
    bool symbolsInitialized_ = false;

    // 默认日志级别
    std::atomic<Level> currentLevel_{ Level::LEVEL_DEBUG };
};

#define LOG_FATAL(location, ...) \
    [&]() -> void { \
        try { \
            Logger::GetInstance().Log(Logger::Level::LEVEL_FATAL, location, __VA_ARGS__); \
        } catch (...) { \
            /* 捕获并重新抛出以获取异常信息 */ \
            try { \
                throw; \
            } catch (const std::exception& e) { \
                Logger::GetInstance().LogFatalImpl( \
                    nullptr, \
                    std::string("[") + __FUNCTION__ + "][" + __FILE__ + "]", \
                    e.what()); \
            } catch (...) { \
                Logger::GetInstance().LogFatalImpl( \
                    nullptr, \
                    std::string("[") + __FUNCTION__ + "][" + __FILE__ + "]", \
                    "Unknown exception occurred while logging FATAL message"); \
            } \
        } \
    }()

#define LOG_FATAL_EX(exception, location, ...) \
    [&]() -> void { \
        try { \
            Logger::GetInstance().Log(Logger::Level::LEVEL_FATAL, location, __VA_ARGS__); \
        } catch (...) { \
            Logger::GetInstance().LogFatalImpl( \
                exception, \
                std::string("[") + __FUNCTION__ + "][" + __FILE__ + "]", \
                "Exception occurred while logging FATAL message"); \
        } \
    }()

#define LOG_ERROR(location, ...) Logger::GetInstance().Log(Logger::Level::LEVEL_ERROR, location, __VA_ARGS__)
#define LOG_WARN(location, ...)  Logger::GetInstance().Log(Logger::Level::LEVEL_WARN,  location, __VA_ARGS__)
#define LOG_INFO(location, ...)  Logger::GetInstance().Log(Logger::Level::LEVEL_INFO,  location, __VA_ARGS__)
#define LOG_DEBUG(location, ...) Logger::GetInstance().Log(Logger::Level::LEVEL_DEBUG, location, __VA_ARGS__)