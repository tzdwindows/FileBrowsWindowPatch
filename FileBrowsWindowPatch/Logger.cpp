#include "pch.h"

#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <filesystem>

#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")

Logger::Logger() {
    Initialize();
}

Logger::~Logger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

std::string Logger::WideToUTF8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";

    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), (int)wstr.size(),
        nullptr, 0,
        nullptr, nullptr);

    std::string str(size_needed, 0);
    WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), (int)wstr.size(),
        &str[0], size_needed,
        nullptr, nullptr);

    return str;
}

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize() {
    if (initialized_) return;

    // ��ȡDLL����Ŀ¼
    char modulePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    std::filesystem::path path(modulePath);
    logDirPath_ = (path.parent_path() / "FileBrowsLogg").string();

    // ������־Ŀ¼
    if (!std::filesystem::exists(logDirPath_)) {
        std::filesystem::create_directory(logDirPath_);
    }

    // ������־�ļ����������ڣ�
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &in_time_t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d") << ".log";
    std::string logFilePath = (std::filesystem::path(logDirPath_) / oss.str()).string();

    // ����־�ļ�
    logFile_.open(logFilePath, std::ios::out | std::ios::app);
    initialized_ = true;
}

std::string Logger::GetCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &in_time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string Logger::GetLevelString(Level level) {
    switch (level) {
    case Level::LEVEL_FATAL: return "FATAL";
    case Level::LEVEL_ERROR: return "ERROR";
    case Level::LEVEL_WARN:  return "WARN";
    case Level::LEVEL_INFO:  return "INFO";
    case Level::LEVEL_DEBUG:return "DEBUG";
    default:          return "UNKNOWN";
    }
}

void Logger::LogImpl(Level level, const std::string& location, const std::string& message) {
    //std::lock_guard<std::mutex> lock(mutex_);

    // ��ʽ����־��Ŀ
    std::string entry = "[" + GetCurrentTime() + "]["
        + GetLevelString(level) + "]["
        + location + "] "
        + message + "\n";

    // ���������̨����������
    OutputDebugStringA(entry.c_str());

    if (!logFile_.is_open()) return;
    // д���ļ�
    logFile_ << entry;
    logFile_.flush();
}

void Logger::LogFatalImpl(PEXCEPTION_POINTERS exception,const std::string& location, const std::string& message) {
    // ��ִ�г�����־��¼
    LogImpl(Level::LEVEL_FATAL, location, message);

    // ��ȡ���ö�ջ
    std::string stackTrace = GetStackTrace();
    LogImpl(Level::LEVEL_FATAL, location, "Call stack:\n" + stackTrace);

    // ����ת���ļ�
    std::string dumpPath = CreateMiniDump(exception);

    // ������ʾ
    std::string popupMsg = "Fatal Error!\n"
        "Location: " + location + "\n"
        "Message: " + message + "\n\n"
        "Dump file created at:\n" + dumpPath + "\n\n"
        "Call stack:\n" + stackTrace;

    MessageBoxA(
        nullptr,
        popupMsg.c_str(),
        "Critical System Error",
        MB_ICONERROR | MB_OK
    );
}

std::string Logger::GetStackTrace() {
    void* stack[100];
    unsigned short frames;
    SYMBOL_INFO* symbol;
    HANDLE process = GetCurrentProcess();

    SymInitialize(process, NULL, TRUE);
    frames = CaptureStackBackTrace(0, 100, stack, NULL);
    symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    std::stringstream ss;
    for (unsigned int i = 0; i < frames; i++) {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
        ss << i << ": " << symbol->Name << " (0x" << std::hex << symbol->Address << std::dec << ")\n";
    }

    free(symbol);
    SymCleanup(process);
    return ss.str();
}

std::string Logger::CreateMiniDump(PEXCEPTION_POINTERS exception) {
    // ��ȡ��ǰʱ����Ϊ�ļ���
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &in_time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".dmp";
    std::string dumpFileName = oss.str();

    // ��ȡDLL����Ŀ¼
    char modulePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    std::filesystem::path path(modulePath);
    std::string dumpDirPath = (path.parent_path() / "dumps").string();

    // ����dumpsĿ¼
    if (!std::filesystem::exists(dumpDirPath)) {
        std::filesystem::create_directory(dumpDirPath);
    }

    // ����ת���ļ�·��
    std::string dumpFilePath = (std::filesystem::path(dumpDirPath) / dumpFileName).string();

    // ����ת���ļ�
    HANDLE hFile = CreateFileA(dumpFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = exception;  // û���쳣��Ϣ
        mdei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
            MiniDumpNormal, &mdei, NULL, NULL);

        CloseHandle(hFile);
    }

    return dumpFilePath;
}