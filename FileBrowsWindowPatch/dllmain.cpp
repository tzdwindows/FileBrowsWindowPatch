// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <Windows.h>
#include <detours.h>
#include <CommCtrl.h>
#include <map>
#include <string>
#include <uxtheme.h>
#include <dwmapi.h>
#include <sstream>

#include "ConfigManager.h"
#include "HookManager.h"
#include "TAPSite.h"

#include <fstream>
#include <shlwapi.h>

#include "Logger.h"
#include "LogRedirector.h"
#include "CrashMonitor.h"

#pragma comment(lib, "shlwapi.lib")

HMODULE g_hModule = nullptr;

template<typename T>
struct SimpleFactory : winrt::implements<SimpleFactory<T>, IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override {
        if (pUnkOuter) {
            return CLASS_E_NOAGGREGATION;
        }
        return winrt::make<T>().as(riid, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) noexcept override {
        return S_OK;
    }
};

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv) try {
    LOG_DEBUG("[dllmain.cpp][External:DllGetClassObject]", "The debugger was loaded successfully");
    if (rclsid == CLSID_TAPSite) {
        auto factory = winrt::make<SimpleFactory<TAPSite>>();
        if (!factory) {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = factory->QueryInterface(riid, ppv);
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
catch (...) {
    return winrt::to_hresult();
}

STDAPI DllCanUnloadNow() {
    return S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeName = PathFindFileNameW(exePath);
        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::towlower);

        if (exeName == L"explorer.exe") {
            /*wil::unique_handle handle2(CreateThread(nullptr, 0, TAPSite::InstallUdk, nullptr, 0, nullptr));
            if (!handle2) {
                LOG_ERROR("[dllmain.cpp][DllMain]", L"创建TAPSite线程失败\n");
            }*/
            CrashMonitor monitor;
            monitor.Run();
            // int* p = nullptr; *p = 0;
            //ConfigManager::LoadConfig();
            HookManager::InstallHooks();
        }
        else {
            LOG_INFO("[dllmain.cpp][DllMain]", L"当前进程名: ", exeName);
            LOG_INFO("[dllmain.cpp][DllMain]", L"非explorer.exe进程，自动释放DLL");
            CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                HMODULE hModule = static_cast<HMODULE>(lpParam);
                Sleep(100);
                FreeLibraryAndExitThread(hModule, 0);
                return 0;
                }, hModule, 0, nullptr);
            return TRUE; // 或者根据情况返回
        }
    }
    return TRUE;
}

__declspec(dllexport) void WINAPI SetRemote_Config(Remote_Config* pRemoteConfig)
{
    if (pRemoteConfig == nullptr) {
        return;
    }
    LOG_DEBUG("[dllmain.cpp][External:SetRemote_Config]", "A program is setting parameters");
    auto ShowConfigValues = [](const Remote_Config* config) {
        std::wstringstream ss;
        ss << L"Configuration Values:\n";
        ss << L"effType: " << config->effType << L"\n";
        ss << L"blendColor: 0x" << std::hex << config->blendColor << std::dec << L"\n";
        ss << L"automatic_acquisition_color: " << (config->automatic_acquisition_color ? L"true" : L"false") << L"\n";
        ss << L"automatic_acquisition_color_transparency: " << config->automatic_acquisition_color_transparency << L"\n";
        ss << L"imagePath: " << config->imagePath << L"\n";
        ss << L"imageOpacity: " << config->imageOpacity << L"\n";
        ss << L"imageBlurRadius: " << config->imageBlurRadius << L"\n";
        //ss << L"smallborder: " << (config->smallborder ? L"true" : L"false") << L"\n";
        ss << L"enabled: " << (config->enabled ? L"true" : L"false");
        LOG_DEBUG("[dllmain.cpp][External:SetRemote_Config:ShowConfigValues]", ss.str().c_str());
        };

    ShowConfigValues(pRemoteConfig);

    if (!pRemoteConfig->enabled) {
        HookManager::RemoveHooks();
        if (pRemoteConfig) {
            UnmapViewOfFile(pRemoteConfig);
            pRemoteConfig = NULL;
        }

        CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            Sleep(1000);
            FreeLibraryAndExitThread(g_hModule, 0);
            return 0;
            }, NULL, 0, NULL);

        return;
    } else {
        HookManager::InstallHooks();
    }

    if (pRemoteConfig->NotificationLevel >= static_cast<int>(Logger::Level::LEVEL_FATAL) &&
        pRemoteConfig->NotificationLevel <= static_cast<int>(Logger::Level::LEVEL_DEBUG)) {
        Logger::GetInstance().SetLevel(static_cast<Logger::Level>(pRemoteConfig->NotificationLevel));
    }
    else {
        LOG_ERROR("[dllmain.cpp][External:SetRemote_Config]", "Invalid NotificationLevel: %d", pRemoteConfig->NotificationLevel);
    }
    HookManager::Config newConfig;
    newConfig.effType = pRemoteConfig->effType;
    newConfig.blendColor = pRemoteConfig->blendColor;
    newConfig.automatic_acquisition_color = pRemoteConfig->automatic_acquisition_color;
    newConfig.automatic_acquisition_color_transparency = pRemoteConfig->automatic_acquisition_color_transparency;
    newConfig.imagePath = pRemoteConfig->imagePath;
    newConfig.imageOpacity = pRemoteConfig->imageOpacity;
    newConfig.imageBlurRadius = pRemoteConfig->imageBlurRadius;
    newConfig.smallborder = pRemoteConfig->smallborder;

    {
        HookManager::m_config = newConfig;
    }
}
