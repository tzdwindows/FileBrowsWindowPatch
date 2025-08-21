#include "pch.h"   

#include "CrashMonitor.h"
#include <iostream>

std::atomic<bool> CrashMonitor::isRunning(false);

CrashMonitor::CrashMonitor() {
    // ��ʼ��Logger
    Logger::GetInstance().Initialize();
}

CrashMonitor::~CrashMonitor() {
    Stop();
}

LONG WINAPI CrashMonitor::UnhandledExceptionHandler(PEXCEPTION_POINTERS exception) {
    // ��¼������Ϣ
    LOG_FATAL_EX(exception, "UnhandledExceptionHandler",
        "Application crashed with exception code: 0x{:X}",
        exception->ExceptionRecord->ExceptionCode);

    // ��ϵͳ�����쳣����ֹ����
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI CrashMonitor::ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        // �����˳�������¼Ϊ����
        isRunning = false;
        return TRUE;
    }

    // ��������̨�¼�����رգ���¼Ϊ����
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

    // ����δ�����쳣������
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);

    // ���ÿ���̨���ƴ�����
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

void CrashMonitor::Stop() {
    isRunning = false;

    // �ָ�Ĭ�ϵ��쳣����
    SetUnhandledExceptionFilter(nullptr);

    // �Ƴ�����̨���ƴ�����
    SetConsoleCtrlHandler(nullptr, FALSE);
}