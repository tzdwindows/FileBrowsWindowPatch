#include "pch.h"  
#include <strsafe.h>  
#include <thread>

#include <xamlom.h>

#include "TaskbarAppearanceService.h"
#include "TAPSite.h"  

#include "Logger.h"
#include "VisualTreeWatcher.h"

static const GUID CLSID_TAPSite =
{ 0x1ca585f8, 0xf1c7, 0x4bbe, { 0xb7, 0x9e, 0xc4, 0xc3, 0x45, 0xb3, 0x29, 0xe7 } };


winrt::com_ptr<IVisualTreeServiceCallback2> TAPSite::s_VisualTreeWatcher;

wil::unique_event_nothrow TAPSite::GetReadyEvent() {
    wil::unique_event_nothrow readyEvent;
    // 修正事件名称拼写
    winrt::check_hresult(readyEvent.create(
        wil::EventOptions::ManualReset,
        L"FileBrowserWindowPatch_Ready"
    ));
    return readyEvent;
}

DWORD TAPSite::Install(void*)
{
    static int connectionCounter = 1;
    const auto event = GetReadyEvent();

    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    std::wstring location = dllPath;

    constexpr const wchar_t* kTapDllName = L"FileBrowsWindowPatch.dll";

    using InitializeXamlDiagnosticsEx_t = HRESULT(WINAPI*)(
        LPCWSTR endPointName,
        DWORD   pid,
        LPCWSTR wszDllXamlDiagnostics,
        LPCWSTR wszTAPDllName,
        REFCLSID tapClsid,
        LPCWSTR wszInitializationData
        );

    wil::unique_hmodule hMod(GetModuleHandleW(L"Windows.UI.Xaml.dll"));
    if (!hMod)
    {
        hMod.reset(LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
        if (!hMod)
        {
            MessageBoxA(nullptr, "无法加载 Windows.UI.Xaml.dll", "错误", MB_OK | MB_ICONERROR);
            event.SetEvent();
            return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        }
    }

    auto ixde = reinterpret_cast<InitializeXamlDiagnosticsEx_t>(
        GetProcAddress(hMod.get(), "InitializeXamlDiagnosticsEx")
        );
    if (!ixde)
    {
        MessageBoxA(nullptr, "未能找到 InitializeXamlDiagnosticsEx，请确认运行环境。", "错误", MB_OK | MB_ICONERROR);
        event.SetEvent();
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    // 调试提示
    {
        wchar_t msg[256];
        swprintf_s(msg, L"使用的调试 API DLL: Windows.UI.Xaml.dll");
        MessageBoxW(nullptr, msg, L"调试信息", MB_OK | MB_ICONINFORMATION);
        OutputDebugStringW(msg);
    }

    const DWORD pid = GetCurrentProcessId();
    HRESULT hr = E_FAIL;

    auto call_ex = [&](LPCWSTR connName) -> HRESULT {
        return ixde(connName, pid, location.c_str(), kTapDllName, CLSID_TAPSite, nullptr);
        };

    // 尝试 VisualDiagConnection*
    {
        wchar_t connectionName[64];
        swprintf_s(connectionName, L"VisualDiagConnection%d", connectionCounter++);
        hr = call_ex(connectionName);
    }

    if (FAILED(hr))
    {
        char msg[256];
        sprintf_s(msg, "Windows.UI.Xaml.dll InitializeXamlDiagnosticsEx 失败: 0x%08X", hr);
        MessageBoxA(nullptr, msg, "错误", MB_OK | MB_ICONERROR);
    }

    event.SetEvent();
    return hr;
}

// 仅替换/修改后的函数：DWORD TAPSite::InstallUdk(void*)
DWORD TAPSite::InstallUdk(void* lpParam)
{
    static int connectionCounter = 1;

    // 获取当前模块路径（作为 wszDllXamlDiagnostics）
    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    std::wstring location = dllPath;

    constexpr const wchar_t* kTapDllName = L"FileBrowsWindowPatch.dll";

    using InitializeXamlDiagnosticsEx_t = HRESULT(WINAPI*)(
        LPCWSTR endPointName,
        DWORD   pid,
        LPCWSTR wszDllXamlDiagnostics,
        LPCWSTR wszTAPDllName,
        REFCLSID tapClsid,
        LPCWSTR wszInitializationData
        );

    // COM STA 初始化 —— XAML diagnostics 交互通常需要 STA
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInited = SUCCEEDED(hrCo);
    if (!coInited) {
        wchar_t buf[256];
        swprintf_s(buf, L"[TAPSite::InstallUdk] CoInitializeEx failed: 0x%08X\n", hrCo);
        OutputDebugStringW(buf);
        // 仍继续尝试加载 dll / 调用 API，但可能会失败
    }

    wil::unique_hmodule hMod(GetModuleHandleW(L"Microsoft.Internal.FrameworkUdk.dll"));
    if (!hMod)
    {
        hMod.reset(LoadLibraryExW(L"Microsoft.Internal.FrameworkUdk.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
        if (!hMod)
        {
            if (coInited) CoUninitialize();
            return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        }
    }

    auto ixde = reinterpret_cast<InitializeXamlDiagnosticsEx_t>(
        GetProcAddress(hMod.get(), "InitializeXamlDiagnosticsEx")
        );
    if (!ixde)
    {
        if (coInited) CoUninitialize();
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const DWORD pid = GetCurrentProcessId();
    HRESULT hr = E_FAIL;

    // 尝试次数与等待策略（当传入 HWND 时使用轮询重试）
    HWND targetHwnd = reinterpret_cast<HWND>(lpParam);
    const int maxAttempts = 60;      // 最大尝试次数
    const int sleepMillis = 200;     // 每次尝试间隔

    auto call_ex = [&](LPCWSTR connName) -> HRESULT {
        return ixde(connName, pid, location.c_str(), kTapDllName, CLSID_TAPSite, nullptr);
        };

    if (targetHwnd == nullptr) {
        // 传统启动路径：直接构造唯一连接名并调用一次（保持向后兼容）
        wchar_t connectionName[64];
        swprintf_s(connectionName, L"WinUIVisualDiagConnection%d", connectionCounter++);
        hr = call_ex(connectionName);
        if (FAILED(hr)) {
            char msg[256];
            sprintf_s(msg, "Microsoft.Internal.FrameworkUdk.dll InitializeXamlDiagnosticsEx 失败: 0x%08X", hr);
            MessageBoxA(nullptr, msg, "错误", MB_OK | MB_ICONERROR);
        }
        else {
            wchar_t dbg[256];
            swprintf_s(dbg, L"[TAPSite::InstallUdk] InitializeXamlDiagnosticsEx succeeded name=%s hr=0x%08X\n", connectionName, hr);
            OutputDebugStringW(dbg);
        }
    }
    else {
        // 针对单个新窗口：等待窗口创建并重试直到 InitializeXamlDiagnosticsEx 成功或超时
        wchar_t clsName[128] = { 0 };
        GetClassNameW(targetHwnd, clsName, _countof(clsName));

        wchar_t dbgStart[256];
        swprintf_s(dbgStart, L"[TAPSite::InstallUdk] Install requested for hwnd=%p class=%s\n", targetHwnd, clsName);
        OutputDebugStringW(dbgStart);

        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            if (!IsWindow(targetHwnd)) {
                // 窗口还不存在或已销毁，继续等待
                Sleep(sleepMillis);
                continue;
            }

            // 可选: 检查窗口是否可见 / 已经 ShowWindow
            if (!(IsWindowVisible(targetHwnd))) {
                // 窗口尚未可见也等待
                Sleep(sleepMillis);
                continue;
            }

            // 为窗口的异步 XAML 初始化再给一点时间
            Sleep(100);

            // 生成包含 hwnd 的连接名，便于排查日志
            wchar_t connectionName[128];
            swprintf_s(connectionName, L"WinUIVisualDiag_h%p_%d", targetHwnd, connectionCounter++);

            hr = call_ex(connectionName);
            if (SUCCEEDED(hr)) {
                wchar_t okbuf[256];
                swprintf_s(okbuf, L"[TAPSite::InstallUdk] InitializeXamlDiagnosticsEx succeeded name=%s hr=0x%08X hwnd=%p\n", connectionName, hr, targetHwnd);
                OutputDebugStringW(okbuf);
                // 成功后可以在这里做额外工作（例如针对该 hwnd 的 InstallForWindow）
                // TODO: 如果你有 InstallForWindow(hwnd) 函数，可以在这里调用它去 attach VisualTreeWatcher。
                break;
            }
            else {
                wchar_t failbuf[256];
                swprintf_s(failbuf, L"[TAPSite::InstallUdk] InitializeXamlDiagnosticsEx attempt %d failed hr=0x%08X name=%s hwnd=%p\n", attempt + 1, hr, connectionName, targetHwnd);
                OutputDebugStringW(failbuf);
            }

            Sleep(sleepMillis);
        }

        // 如果最终失败，弹窗提示（可选）
        if (FAILED(hr)) {
            /*char msg[256];
            sprintf_s(msg, "Microsoft.Internal.FrameworkUdk.dll InitializeXamlDiagnosticsEx (for hwnd) 最终失败: 0x%08X", hr);
            MessageBoxA(nullptr, msg, "错误", MB_OK | MB_ICONERROR);*/
        }
    }

    if (coInited) CoUninitialize();
    return hr;
}

HRESULT TAPSite::SetSite(IUnknown* pUnkSite) try
{
    if (s_VisualTreeWatcher) // 直接检查指针是否有效
    {
        return E_UNEXPECTED;
    }

    site.copy_from(pUnkSite);

    if (site)
    {
        // 创建 VisualTreeWatcher 并获取其接口
        auto watcher = winrt::make_self<VisualTreeWatcher>(site, GetReadyEvent());
        s_VisualTreeWatcher = watcher.as<IVisualTreeServiceCallback2>();
    }

    return S_OK;
}
catch (...)
{
    return winrt::to_hresult();
}

HRESULT TAPSite::GetSite(REFIID riid, void** ppvSite) noexcept
{
    return site.as(riid, ppvSite);
}