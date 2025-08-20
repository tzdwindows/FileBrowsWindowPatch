#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <mutex>

class SafeLogger {
public:
    static void Initialize() {
        GetInstance(); // ȷ��ʵ����
    }

    static void Log(const std::wstring& message) {
        GetInstance().AddLog(message);
    }

    static void Log(const std::string& message) {
        GetInstance().AddLog(message);
    }

private:
    std::wofstream m_logFile;
    std::mutex m_mutex;
    static SafeLogger* s_instance;

    SafeLogger() {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(GetModuleHandle(NULL), path, MAX_PATH);
        PathRemoveFileSpecW(path);
        PathAppendW(path, L"ExplorerBlurMica.log");

        // ʹ��Windows APIֱ�Ӵ����ļ�������CRT����
        m_logFile.open(path, std::ios::out | std::ios::app);
    }

    ~SafeLogger() {
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
    }

    static SafeLogger& GetInstance() {
        static SafeLogger instance;
        return instance;
    }

    void AddLog(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile << message;
            if (message.back() != L'\n') {
                m_logFile << L"\n";
            }
            m_logFile.flush();
        }

        // ��ȫ��OutputDebugStringW����
        if (IsDebuggerPresent()) {
            ::OutputDebugStringW(message.c_str());
            if (message.back() != L'\n') {
                ::OutputDebugStringW(L"\n");
            }
        }
    }

    void AddLog(const std::string& message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile << message.c_str();
            if (message.back() != '\n') {
                m_logFile << L"\n";
            }
            m_logFile.flush();
        }

        if (IsDebuggerPresent()) {
            ::OutputDebugStringA(message.c_str());
            if (message.back() != '\n') {
                ::OutputDebugStringA("\n");
            }
        }
    }
};