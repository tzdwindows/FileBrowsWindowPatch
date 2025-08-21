#include "pch.h"   

#include "CrashMonitor.h"
#include <iostream>

std::atomic<bool> CrashMonitor::isRunning(false);

CrashMonitor::CrashMonitor() {
    // 初始化Logger
    Logger::GetInstance().Initialize();
}

CrashMonitor::~CrashMonitor() {
    Stop();
}

LONG WINAPI CrashMonitor::UnhandledExceptionHandler(PEXCEPTION_POINTERS exception) {
    // 记录崩溃信息
    LOG_FATAL_EX(exception, "UnhandledExceptionHandler",
        "Application crashed with exception code: 0x{:X}",
        exception->ExceptionRecord->ExceptionCode);

    // 让系统处理异常（终止程序）
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI CrashMonitor::ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        // 正常退出，不记录为崩溃
        isRunning = false;
        return TRUE;
    }

    // 其他控制台事件（如关闭）记录为崩溃
    LOG_FATAL("ConsoleCtrlHandler",
        "Application terminated by console control event: {}",
        ctrlType);

    return FALSE;
}

void CrashMonitor::Run() {
    if (isRunning) {
        return;
    }

    isRunning = true;

    // 设置未处理异常过滤器
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);

    // 设置控制台控制处理器
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

void CrashMonitor::Stop() {
    isRunning = false;

    // 恢复默认的异常处理
    SetUnhandledExceptionFilter(nullptr);

    // 移除控制台控制处理器
    SetConsoleCtrlHandler(nullptr, FALSE);
}