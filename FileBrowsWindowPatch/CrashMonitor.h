#pragma once
#include "Logger.h"

class CrashMonitor {
public:
    CrashMonitor();
    ~CrashMonitor();

    void Run();
    void Stop();

private:
    static LONG WINAPI UnhandledExceptionHandler(PEXCEPTION_POINTERS exception);
    static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);

    static std::atomic<bool> isRunning;
};