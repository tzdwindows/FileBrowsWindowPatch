#pragma once
#include "Logger.h"

class CrashMonitor {
public:
    CrashMonitor();
    ~CrashMonitor();

    void Run();
    void Stop();

    static LONG WINAPI UnhandledExceptionHandler(PEXCEPTION_POINTERS exception);
    static void SignalHandler(int sig);
    static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);
    static LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* ep);

private:
    void ShowCrashDialogFromUIThread();
    std::atomic<bool> isRunning_;
};