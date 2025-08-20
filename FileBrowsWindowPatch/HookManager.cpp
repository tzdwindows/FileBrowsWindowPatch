#include "pch.h"
#include "HookManager.h"

#include <sstream>

#include "ImageRenderer.h"
#include "ConfigManager.h" 

#include <VersionHelpers.h>
#include <wil/resource.h>
#include <winrt/impl/Windows.UI.2.h>
#include <wil/resource.h>
#include <atlbase.h>
#include <inspectable.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

#include "Logger.h"
#include "XamlTreeScanner.h"

#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Composition.h>

#include "TAPSite.h"
#include <dbghelp.h>
#include <psapi.h>
#include <shlwapi.h>
#include <windows.ui.xaml.media.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Oleacc.lib")

#ifndef UIA_OpacityPropertyId
#define UIA_OpacityPropertyId 30050
#endif

namespace winrt::Windows::UI::Xaml::Shapes
{
	struct Rectangle;
}

namespace winrt::Windows::UI::Xaml::Media
{
	struct ISolidColorBrush;
}

std::map<HWND, WindowEffect> HookManager::windowEffects;
std::mutex HookManager::effectMutex;
HookManager::Config HookManager::m_config;
bool HookManager::is_installHooks = false;
UpdateBackground_t HookManager::OriginalUpdateBackground = nullptr;

HBRUSH HookManager::m_clearBrush = 0;
std::unordered_map<DWORD, HookManager::DUIData> HookManager::m_DUIList;
std::unordered_map<DWORD, bool> HookManager::m_drawtextState;
std::unordered_map<DWORD, std::pair<HWND, HDC>> HookManager::m_ribbonPaint;
ULONG_PTR HookManager::m_gdiplusToken = 0;
pfnSetWindowCompositionAttribute SetWindowCompositionAttribute = nullptr;
Element_Paint_t HookManager::OriginalElementPaint = nullptr;
HBRUSH HookManager::transparentBrush = CreateSolidBrush(0x00000000);
std::unordered_map<std::wstring, ImageCache> HookManager::s_imageCache;
static HANDLE hConfigMap = NULL;
static Remote_Config* pRemoteConfig = NULL;
static HANDLE hThread = NULL;
static bool shouldExit = false;

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuc = winrt::Windows::UI::Composition;
namespace wfn = winrt::Windows::Foundation::Numerics;

PaintBackground_t HookManager::OriginalPaintBackground = nullptr;
static bool is = false;

std::set<std::wstring> HookManager::transparentClasses = {
    L"DirectUIHWND",
    L"SHELLDLL_DefView",
    L"ShellTabWindowClass",
    L"SysTreeView32"
};

namespace PdbResolver {
    bool g_symbolsInitialized = false;
    std::unordered_map<std::string, DWORD64> g_loadedModules;

    std::string WideToNarrow(const wchar_t* wideStr) {
        if (!wideStr) return "";

        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (bufferSize == 0) return "";

        std::string narrowStr(bufferSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &narrowStr[0], bufferSize, nullptr, nullptr);

        // ȥ��ĩβ�� null �ַ�
        if (!narrowStr.empty() && narrowStr.back() == '\0') {
            narrowStr.pop_back();
        }

        return narrowStr;
    }

    bool InitializeSymbols() {
        if (g_symbolsInitialized) return true;

        // ���÷���ѡ��
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

        // ��ʼ�����Ŵ���
        if (!SymInitialize(GetCurrentProcess(), nullptr, FALSE)) {
            LOG_ERROR("[PdbResolver]", "SymInitialize failed: ", GetLastError());
            return false;
        }

        // �������ط��Ż���·�� (��ǰ DLL ����Ŀ¼�� symbols ��Ŀ¼)
        wchar_t dllPath[MAX_PATH] = { 0 };
        if (!g_hModule || !GetModuleFileNameW(g_hModule, dllPath, MAX_PATH)) {
            LOG_WARN("[PdbResolver]", "Failed to get DLL path: ", GetLastError());
        }
        else {
            // ������·������ȡĿ¼����
            wchar_t* lastBackslash = wcsrchr(dllPath, L'\\');
            if (lastBackslash) {
                *lastBackslash = L'\0'; // ȥ���ļ�������
            }

            // ���� symbols ��Ŀ¼·��
            wchar_t symbolsPath[MAX_PATH];
            PathCombineW(symbolsPath, dllPath, L"symbols");

            // ȷ��Ŀ¼����
            if (!PathIsDirectoryW(symbolsPath) && !CreateDirectoryW(symbolsPath, nullptr)) {
                LOG_WARN("[PdbResolver]", "Failed to create symbols directory: ", GetLastError());
            }
            else {
                LOG_DEBUG("[PdbResolver]", "Using local symbols cache: ", symbolsPath);
            }

            // ������������·��
            std::string searchPath = "srv*";
            searchPath += WideToNarrow(symbolsPath);
            searchPath += "*https://msdl.microsoft.com/download/symbols";

            // ���÷�������·��
            if (!SymSetSearchPath(GetCurrentProcess(), searchPath.c_str())) {
                LOG_WARN("[PdbResolver]", "SymSetSearchPath failed: ", GetLastError());
            }
        }

        g_symbolsInitialized = true;
        return true;
    }

    uintptr_t FindFunctionAddress(const char* moduleName, const char* decoratedName) {
        if (!InitializeSymbols()) return 0;

        HMODULE hModule = GetModuleHandleA(moduleName);
        if (!hModule) {
            LOG_ERROR("[PdbResolver]", "Module not loaded: ", moduleName);
            return 0;
        }

        // ��ȡģ������·��
        char modulePath[MAX_PATH] = { 0 };
        GetModuleFileNameA(hModule, modulePath, MAX_PATH);

        // ����Ƿ��Ѽ��ط���
        auto it = g_loadedModules.find(modulePath);
        if (it != g_loadedModules.end()) {
            LOG_DEBUG("[PdbResolver]", "Using cached symbols for: ", modulePath);
        }
        else {
            MODULEINFO moduleInfo;
            if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo))) {
                LOG_ERROR("[PdbResolver]", "GetModuleInformation failed: ", GetLastError());
                return 0;
            }

            // ����ģ����� - ʹ������·��
            DWORD64 baseAddress = SymLoadModuleEx(
                GetCurrentProcess(),
                nullptr,
                modulePath,
                nullptr,
                (DWORD64)moduleInfo.lpBaseOfDll,
                moduleInfo.SizeOfImage,
                nullptr,
                0
            );

            if (!baseAddress) {
                DWORD error = GetLastError();
                LOG_ERROR("[PdbResolver]", "SymLoadModuleEx failed for ", modulePath, ": ", error);
                return 0;
            }

            g_loadedModules[modulePath] = baseAddress;
            LOG_INFO("[PdbResolver]", "Loaded symbols for: ", modulePath);

            // ��ӡģ����Ϣ��֤
            IMAGEHLP_MODULE64 moduleInfo64 = { sizeof(IMAGEHLP_MODULE64) };
            if (SymGetModuleInfo64(GetCurrentProcess(), baseAddress, &moduleInfo64)) {
                LOG_DEBUG("[PdbResolver]", "PDB info: ", moduleInfo64.LoadedPdbName);
                LOG_DEBUG("[PdbResolver]", "PDB signature: ",
                    std::hex , moduleInfo64.PdbSig70.Data1 , "-" , moduleInfo64.PdbAge);
            }
        }

        DWORD64 baseAddress = g_loadedModules[modulePath];
        const char* targetFunction = "UpdateBackground";

        // ���������ص�������
        struct SymbolSearchContext {
            const char* targetName;
            uintptr_t address;
            bool found;
        } context = { targetFunction, 0, false };

        // ����ö�ٻص�
        auto EnumSymbolsCallback = [](PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) -> BOOL {
            SymbolSearchContext* ctx = static_cast<SymbolSearchContext*>(UserContext);

            // ��麯������ƥ��
            if (strstr(pSymInfo->Name, ctx->targetName) != nullptr) {
                // ������֤�Ƿ�ΪHub���Ա
                if (strstr(pSymInfo->Name, "Hub") != nullptr ||
                    strstr(pSymInfo->Name, "DirectUI") != nullptr) {
                    LOG_DEBUG("[PdbResolver]", "Found candidate: ", pSymInfo->Name);
                    ctx->address = static_cast<uintptr_t>(pSymInfo->Address);
                    ctx->found = true;
                    return FALSE; // �ҵ���ֹͣ����
                }
            }
            return TRUE; // ��������
            };

        // ö��ģ���е����з���
        if (!SymEnumSymbols(
            GetCurrentProcess(),        // ���̾��
            baseAddress,                // ģ���ַ
            nullptr,                    // ���� (���з���)
            EnumSymbolsCallback,        // �ص�����
            &context))                 // �û�������
        {
            LOG_ERROR("[PdbResolver]", "SymEnumSymbols failed: ", GetLastError());
        }

        if (context.found) {
            LOG_INFO("[PdbResolver]", "Resolved symbol: ", context.address);
            return context.address;
        }

        // �ռ����ˣ�ͨ������ǩ������
        LOG_WARN("[PdbResolver]", "Using fallback signature scan for: ", decoratedName);

        // UpdateBackground ������������
        const uint8_t signature[] = {
            0x48, 0x8B, 0xC4,       // mov rax, rsp
            0x55,                   // push rbp
            0x53,                   // push rbx
            0x56,                   // push rsi
            0x57,                   // push rdi
            0x41, 0x54,             // push r12
            0x41, 0x55,             // push r13
            0x41, 0x56,             // push r14
            0x41, 0x57,             // push r15
            0x48, 0x8D, 0x68, 0xA1, // lea rbp, [rax-5Fh]
            0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, // sub rsp, 0A8h
            0x45, 0x33, 0xED,       // xor r13d, r13d
            0x0F, 0x29, 0x70, 0xA8, // movaps [rax-58h], xmm6
            0x48, 0x8B, 0xF1,       // mov rsi, rcx
            0x0F, 0x29, 0x78, 0x98  // movaps [rax-68h], xmm7
        };
        size_t sigLength = sizeof(signature);

        MODULEINFO modInfo;
        GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo));

        uint8_t* startAddr = static_cast<uint8_t*>(modInfo.lpBaseOfDll);
        uint8_t* endAddr = startAddr + modInfo.SizeOfImage - sigLength;

        for (uint8_t* ptr = startAddr; ptr < endAddr; ptr++) {
            if (memcmp(ptr, signature, sigLength) == 0) {
                LOG_WARN("[PdbResolver]", "Found via signature at: ",
                    reinterpret_cast<uintptr_t>(ptr));
                return reinterpret_cast<uintptr_t>(ptr);
            }
        }

        LOG_ERROR("[PdbResolver]", "All resolution attempts failed for: ", decoratedName);
        return 0;
    }
}

DWORD WINAPI ConfigUpdateThread(LPVOID lpParam) {
    pRemoteConfig = static_cast<Remote_Config*>(
        MapViewOfFile(hConfigMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Remote_Config))
        );

    // �������ֵ��ʾ
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

        MessageBoxW(NULL, ss.str().c_str(), L"Remote Config Values", MB_OK);
        };
    
    while (!shouldExit) {
        if (pRemoteConfig) {
            // ��ʾ����ֵ��ÿ��ѭ������ʾ��
            ShowConfigValues(pRemoteConfig);

            if (!pRemoteConfig->enabled) {
                HookManager::RemoveHooks();

                // ��ʾ����֪ͨ
                MessageBoxW(NULL, L"Disabling hooks and unloading DLL", L"Config Update", MB_OK);

                if (pRemoteConfig) {
                    UnmapViewOfFile(pRemoteConfig);
                    pRemoteConfig = NULL;
                }
                if (hConfigMap) {
                    CloseHandle(hConfigMap);
                    hConfigMap = NULL;
                }

                CreateThread(NULL, 0, [](LPVOID) -> DWORD {
                    Sleep(1000);
                    FreeLibraryAndExitThread(GetModuleHandle(L"FileBrowsWindowPatch.dll"), 0);
                    return 0;
                    }, NULL, 0, NULL);

                return 0;
            }

            // �ӹ����ڴ��ȡ����
            HookManager::Config newConfig;
            newConfig.effType = pRemoteConfig->effType;
            newConfig.blendColor = pRemoteConfig->blendColor;
            newConfig.automatic_acquisition_color = pRemoteConfig->automatic_acquisition_color;
            newConfig.automatic_acquisition_color_transparency = pRemoteConfig->automatic_acquisition_color_transparency;
            newConfig.imagePath = pRemoteConfig->imagePath;
            newConfig.imageOpacity = pRemoteConfig->imageOpacity;
            newConfig.imageBlurRadius = pRemoteConfig->imageBlurRadius;
            newConfig.smallborder = pRemoteConfig->smallborder;

            // �̰߳�ȫ��������
            {
                HookManager::m_config = newConfig;
            }
        }
        Sleep(100); // ÿ100ms���һ��
    }
    return 0;
}

int HookManager::GetDetourHookCount() {
    int count = 0;
    if (OriginalCreateWindowExW) count++;
    if (OriginalDestroyWindow) count++;
    if (OriginalBeginPaint) count++;
    if (OriginalEndPaint) count++;
    if (OriginalFillRect) count++;
    if (OriginalDrawTextW) count++;
    if (OriginalDrawTextExW) count++;
    if (OriginalExtTextOutW) count++;
    if (OriginalCreateCompatibleDC) count++;
    if (OriginalGetThemeColor) count++;
    if (OriginalDrawThemeText) count++;
    if (OriginalDrawThemeTextEx) count++;
    if (OriginalRegisterClassExW) count++;
    if (OriginalDwmSetWindowAttribute) count++;
    if (OriginalDrawThemeBackground) count++;
    if (OriginalDrawThemeBackgroundEx) count++;
    if (OriginalPatBlt) count++;
    return count;
}


void HookManager::InstallHooks() {
    LOG_INFO("[HookManager.cpp][InstallHooks]", "Starting hook installation");

    if (is_installHooks){
        LOG_WARN("[HookManager.cpp][InstallHooks]", "Repeat Hook");
	    return;
    }
    is_installHooks = true;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    HMODULE hUser32 = LoadLibrary(L"user32.dll");
    if (hUser32) {
        SetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
    }

    /*HMODULE hDui70 = LoadLibrary(L"dui70.dll");
    if (!hDui70) {
        LOG_ERROR("[HookManager.cpp][InstallHooks]", "Failed to load dui70.dll");
    }
    else {
        // ʹ����������ȡ������ַ
        OriginalPaintBackground = (PaintBackground_t)GetProcAddress(hDui70,
            "?PaintBackground@Element@DirectUI@@QEAAXPEAUHDC__@@PEAVValue@2@AEBUtagRECT@@222@Z");

        if (!OriginalPaintBackground) {
            LOG_ERROR("[HookManager.cpp][InstallHooks]", "Failed to find PaintBackground");
        }
    }*/

    /*if (hDui70) {
        // ��ȡ Element::Paint �ĺ�����ַ
        OriginalElementPaint = (Element_Paint_t)GetProcAddress(hDui70, "?Paint@Element@DirectUI@@UEAAXPEAUHDC__@@PEBUtagRECT@@1PEAU4@2@Z");
    }
    else {
        LOG_ERROR("[HookManager.cpp][InstallHooks]", "Failed to load dui70.dll");
    }*/


    // ��ʼ��ԭʼ����ָ��
    OriginalCreateWindowExW = CreateWindowExW;
    OriginalDestroyWindow = DestroyWindow;
    OriginalBeginPaint = BeginPaint;
    OriginalEndPaint = EndPaint;
    OriginalFillRect = FillRect;
    OriginalDrawTextW = DrawTextW;
    OriginalDrawTextExW = DrawTextExW;
    OriginalExtTextOutW = ExtTextOutW;

	OriginalCreateCompatibleDC = CreateCompatibleDC;
    OriginalGetThemeColor = GetThemeColor;
    OriginalDrawThemeText = DrawThemeText;
    OriginalDrawThemeTextEx = DrawThemeTextEx;
    OriginalDrawThemeBackground = DrawThemeBackground;
    OriginalDrawThemeBackgroundEx = DrawThemeBackgroundEx;

    OriginalPatBlt = PatBlt;
    OriginalRegisterClassExW = RegisterClassExW;
    OriginalDwmSetWindowAttribute = DwmSetWindowAttribute;

    OriginalAlphaBlend = AlphaBlend;
    OriginalGdiGradientFill = GdiGradientFill;
    OriginalRectangle = Rectangle;
    OriginalSetBkColor = SetBkColor;
    OriginalSetDCBrushColor = SetDCBrushColor;
    OriginalSetDCPenColor = SetDCPenColor;
    OriginalGetDCBrushColor = GetDCBrushColor;
    OriginalGetDCPenColor = GetDCPenColor;
    OriginalDwmExtendFrameIntoClientArea = DwmExtendFrameIntoClientArea;

    OriginalBitBlt = BitBlt;
    OriginalGdiAlphaBlend = GdiAlphaBlend;
    OriginalStretchBlt = StretchBlt;
    /*HMODULE hModule = GetModuleHandleW(L"api-ms-win-core-processthreads-l1-1-0.dll");
    if (hModule == NULL) {
        hModule = LoadLibraryW(L"api-ms-win-core-processthreads-l1-1-0.dll");
    }

    if (hModule != NULL) {
        OriginalCreateProcessW = (PCreateProcessW)GetProcAddress(hModule, "CreateProcessW");
    }

    HMODULE hXaml = LoadLibrary(L"Windows.UI.Xaml.dll");
    if (!hXaml) {
        LOG_ERROR("[HookManager.cpp][InstallHooks]", "Failed to load Windows.UI.Xaml.dll");
    }
    else {
        const char* decoratedName = "?UpdateBackground@Hub@DirectUI@@AEAAJXZ";
        uintptr_t functionAddress = PdbResolver::FindFunctionAddress("Windows.UI.Xaml.dll", decoratedName);

        if (!functionAddress) {
            const char* alternativeNames[] = {
                "?UpdateBackground@Hub@DirectUI@@QEAAJXZ",
                "DirectUI::Hub::UpdateBackground",
                nullptr
            };

            for (int i = 0; alternativeNames[i]; i++) {
                functionAddress = PdbResolver::FindFunctionAddress("Windows.UI.Xaml.dll", alternativeNames[i]);
                if (functionAddress) {
                    LOG_DEBUG("[InstallHooks]", "Used alternative name: ", alternativeNames[i]);
                    break;
                }
            }
        }

        if (functionAddress) {
            typedef __int64(__fastcall* UpdateBackground_t)(void* pThis);
            static UpdateBackground_t OriginalUpdateBackground = reinterpret_cast<UpdateBackground_t>(functionAddress);

            DetourAttach(&(PVOID&)OriginalUpdateBackground, HookedUpdateBackground);
            LOG_INFO("[InstallHooks]", "Hooked DirectUI::Hub::UpdateBackground at 0x", std::hex, functionAddress);

        }
        else {
            LOG_ERROR("[InstallHooks]", "Failed to resolve UpdateBackground function");
        }
    }*/

    // ���ӹ���
    DetourAttach(&(PVOID&)OriginalCreateWindowExW, HookedCreateWindowExW);
    DetourAttach(&(PVOID&)OriginalDestroyWindow, HookedDestroyWindow);
    
    DetourAttach(&(PVOID&)OriginalBeginPaint, HookedBeginPaint);
    DetourAttach(&(PVOID&)OriginalEndPaint, HookedEndPaint);
    
    DetourAttach(&(PVOID&)OriginalFillRect, HookedFillRect);
    DetourAttach(&(PVOID&)OriginalDrawTextW, HookedDrawTextW);
    DetourAttach(&(PVOID&)OriginalDrawTextExW, HookedDrawTextExW);
    DetourAttach(&(PVOID&)OriginalExtTextOutW, HookedExtTextOutW);
    DetourAttach(&(PVOID&)OriginalCreateCompatibleDC, HookedCreateCompatibleDC);
    DetourAttach(&(PVOID&)OriginalGetThemeColor, HookedGetThemeColor);
    DetourAttach(&(PVOID&)OriginalDrawThemeText, HookedDrawThemeText);
    DetourAttach(&(PVOID&)OriginalDrawThemeTextEx, HookedDrawThemeTextEx);
    DetourAttach(&(PVOID&)OriginalRegisterClassExW, HookedRegisterClassExW);
    //DetourAttach(&(PVOID&)OriginalDwmSetWindowAttribute, HookedDwmSetWindowAttribute);

    DetourAttach(&(PVOID&)OriginalAlphaBlend, HookedAlphaBlend);
    DetourAttach(&(PVOID&)OriginalGdiGradientFill, HookedGradientFill);
    DetourAttach(&(PVOID&)OriginalRectangle, HookedRectangle);
	DetourAttach(&(PVOID&)OriginalSetBkColor, HookedSetBkColor);
    DetourAttach(&(PVOID&)OriginalSetDCBrushColor, HookedSetDCBrushColor);
    DetourAttach(&(PVOID&)OriginalSetDCPenColor, HookedSetDCPenColor);
    DetourAttach(&(PVOID&)OriginalGetDCPenColor, HookedGetDCPenColor);
    DetourAttach(&(PVOID&)OriginalDwmExtendFrameIntoClientArea, HookedOriginalDwmExtendFrameIntoClientArea);
    DetourAttach(&(PVOID&)OriginalBitBlt, HookedBitBlt);
    DetourAttach(&(PVOID&)OriginalGdiAlphaBlend, HookedGdiAlphaBlend);
    DetourAttach(&(PVOID&)OriginalStretchBlt, HookedStretchBlt);
    //DetourAttach(&(PVOID&)OriginalCreateProcessW, HookedCreateProcessW);

    DWORD build = WindowsVersion::GetBuildNumber();
    LOG_DEBUG("[HookManager.cpp][InstallHooks]", "Windows build number: ", build);

    DetourAttach(&(PVOID&)OriginalDrawThemeBackground, HookedDrawThemeBackground);

    DetourAttach(&(PVOID&)OriginalDrawThemeBackgroundEx, HookedDrawThemeBackgroundEx);
    DetourAttach(&(PVOID&)OriginalPatBlt, HookedPatBlt);

    if (OriginalPaintBackground && OriginalElementPaint) {
        DetourAttach(&(PVOID&)OriginalPaintBackground, HookedPaintBackground);
        DetourAttach(&(PVOID&)OriginalElementPaint, HookedElementPaint);
    } 
    else {
        LOG_ERROR("[HookManager.cpp][InstallHooks]", "Failed to find Element::Paint || Element::Background");
    }

    LONG error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        LogDetourError(error);
        is_installHooks = false; // ���ð�װ״̬
        return;
    }

    // ��ʼ������
    RefreshConfig();

    LOG_INFO("[HookManager.cpp][InstallHooks]", "Hooks installed successfully");
   /* hConfigMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, L"Global\\FileExplorerBlur");
    if (!hConfigMap) {
        hConfigMap = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(Remote_Config),
            L"Global\\FileExplorerBlur"
        );
    }

    if (hConfigMap) {
        //shouldExit = true;
        //hThread = CreateThread(NULL, 0, ConfigUpdateThread, NULL, 0, NULL);
        return;
    }
    MessageBoxW(NULL, L"[HookManager.cpp][InstallHooks] Error, unable to link to remote channel", L"Error", MB_OK);*/
}

struct Hub_BackgroundFields {
    char padding[0x5E0]; // ��䵽Ŀ���Ա֮ǰ��ƫ��
    bool m_isBackgroundStatic;
    bool m_isBackgroundValid;
    // ע�⣺�������ǲ����ĺ���ĳ�Ա�����Բ���Ҫ����
};

BOOL WINAPI HookManager::HookedCreateProcessW(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation
) {
    // ���ȵ���ԭʼ������������
    BOOL result = OriginalCreateProcessW(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        dwCreationFlags | CREATE_SUSPENDED, // ��������Ա�ע��
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation
    );

    if (!result) {
        LOG_ERROR("[HookManager][HookedCreateProcessW]", "Process creation failed");
        return FALSE;
    }

    // ����Ƿ�����Դ������
    bool isExplorer = false;
    if (lpApplicationName) {
        std::wstring appName(lpApplicationName);
        isExplorer = appName.find(L"explorer.exe") != std::wstring::npos;
    }
    if (!isExplorer && lpCommandLine) {
        std::wstring cmdLine(lpCommandLine);
        isExplorer = cmdLine.find(L"explorer.exe") != std::wstring::npos;
    }

    if (isExplorer) {
        LOG_INFO("[HookManager][HookedCreateProcessW]", "Injecting into new explorer process");

        // ��ȡ��ǰDLL·��
        WCHAR dllPath[MAX_PATH];
        GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

        // ��Ŀ������з����ڴ�
        LPVOID remotePath = VirtualAllocEx(
            lpProcessInformation->hProcess,
            NULL,
            (wcslen(dllPath) + 1) * sizeof(WCHAR),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!remotePath) {
            LOG_ERROR("[HookManager][HookedCreateProcessW]", "Memory allocation failed");
            ResumeThread(lpProcessInformation->hThread);
            return result;
        }

        // д��DLL·��
        WriteProcessMemory(
            lpProcessInformation->hProcess,
            remotePath,
            dllPath,
            (wcslen(dllPath) + 1) * sizeof(WCHAR),
            NULL
        );

        // ��ȡLoadLibrary��ַ
        LPTHREAD_START_ROUTINE loadLibAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"),
            "LoadLibraryW"
        );

        // ����Զ���̼߳���DLL
        HANDLE hThread = CreateRemoteThread(
            lpProcessInformation->hProcess,
            NULL,
            0,
            loadLibAddr,
            remotePath,
            0,
            NULL
        );

        if (!hThread) {
            LOG_ERROR("[HookManager][HookedCreateProcessW]", "Remote thread creation failed");
            VirtualFreeEx(lpProcessInformation->hProcess, remotePath, 0, MEM_RELEASE);
        }
        else {
            // �ȴ�DLL�������
            WaitForSingleObject(hThread, INFINITE);

            // ��ȡDLLģ����
            DWORD exitCode;
            GetExitCodeThread(hThread, &exitCode);
            HMODULE hInjectedDll = (HMODULE)exitCode;

            if (hInjectedDll) {
                // ��ȡSetRemote_Config������ַ
                auto setConfigAddr = (void (WINAPI*)(Remote_Config*))GetProcAddress(
                    hInjectedDll,
                    "SetRemote_Config"
                );

                if (setConfigAddr) {
                    // ��Ŀ����̷��������ڴ�
                    LPVOID remoteConfig = VirtualAllocEx(
                        lpProcessInformation->hProcess,
                        NULL,
                        sizeof(Remote_Config),
                        MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE
                    );

                    if (remoteConfig) {

                        Remote_Config pRemoteConfig;
                        pRemoteConfig.effType = HookManager::m_config.effType;
                        pRemoteConfig.blendColor = HookManager::m_config.blendColor;
                        pRemoteConfig.automatic_acquisition_color = HookManager::m_config.automatic_acquisition_color;
                        pRemoteConfig.automatic_acquisition_color_transparency = HookManager::m_config.automatic_acquisition_color_transparency;

                    	wcscpy_s(pRemoteConfig.imagePath, HookManager::m_config.imagePath.c_str());

                        pRemoteConfig.imageOpacity = HookManager::m_config.imageOpacity;
                        pRemoteConfig.imageBlurRadius = HookManager::m_config.imageBlurRadius;
                        pRemoteConfig.smallborder = HookManager::m_config.smallborder;
                        // д�뵱ǰ����
                        WriteProcessMemory(
                            lpProcessInformation->hProcess,
                            remoteConfig,
                            &pRemoteConfig,
                            sizeof(Remote_Config),
                            NULL
                        );

                        // �����̵߳������ú���
                        HANDLE hConfigThread = CreateRemoteThread(
                            lpProcessInformation->hProcess,
                            NULL,
                            0,
                            (LPTHREAD_START_ROUTINE)setConfigAddr,
                            remoteConfig,
                            0,
                            NULL
                        );

                        if (hConfigThread) {
                            WaitForSingleObject(hConfigThread, INFINITE);
                            CloseHandle(hConfigThread);
                        }
                        VirtualFreeEx(lpProcessInformation->hProcess, remoteConfig, 0, MEM_RELEASE);
                    }
                }
            }

            CloseHandle(hThread);
            VirtualFreeEx(lpProcessInformation->hProcess, remotePath, 0, MEM_RELEASE);
        }
    }

    // �ָ����߳�
    ResumeThread(lpProcessInformation->hThread);
    return result;
}

__int64 __fastcall HookManager::HookedUpdateBackground(void* pThis)
{
    //Hub_BackgroundFields* hub = static_cast<Hub_BackgroundFields*>(pThis);
    LOG_INFO("[HookManager.cpp][HookedUpdateBackground]", "Modified Hub background properties");
    //hub->m_isBackgroundValid = false;
    return 1;
}

void HookManager::LogDetourError(LONG error) {
    std::string detourError;
    switch (error) {
    case ERROR_INVALID_BLOCK: detourError = "ERROR_INVALID_BLOCK"; break;
    case ERROR_INVALID_HANDLE: detourError = "ERROR_INVALID_HANDLE"; break;
    case ERROR_INVALID_OPERATION: detourError = "ERROR_INVALID_OPERATION"; break;
    case ERROR_NOT_ENOUGH_MEMORY: detourError = "ERROR_NOT_ENOUGH_MEMORY"; break;
    case ERROR_NO_MORE_ITEMS: detourError = "ERROR_NO_MORE_ITEMS"; break;
    default: detourError = "Unknown error (" + std::to_string(error) + ")";
    }

    DWORD lastError = GetLastError();
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer, 0, NULL);

    std::string systemError = messageBuffer ? messageBuffer : "Unknown system error";

    LOG_ERROR("[HookManager.cpp][InstallHooks]",
        "DetourTransactionCommit failed! "
        "Detour error: ", detourError, ", "
        "System error: ", systemError, " (", lastError, ")");

    if (messageBuffer) LocalFree(messageBuffer);

    // ��ϸ����״̬��־
    LOG_DEBUG("[HookManager.cpp][InstallHooks]", "Hook attachment status:");
#define LOG_HOOK_STATUS(func) \
        LOG_DEBUG("    " #func ": ", Original##func ? "Attached" : "Not attached")

    LOG_HOOK_STATUS(CreateWindowExW);
    LOG_HOOK_STATUS(DestroyWindow);
    LOG_HOOK_STATUS(BeginPaint);
    LOG_HOOK_STATUS(EndPaint);
    LOG_HOOK_STATUS(FillRect);
    LOG_HOOK_STATUS(DrawTextW);
    LOG_HOOK_STATUS(DrawTextExW);
    LOG_HOOK_STATUS(ExtTextOutW);
    LOG_HOOK_STATUS(CreateCompatibleDC);
    LOG_HOOK_STATUS(GetThemeColor);
    LOG_HOOK_STATUS(DrawThemeText);
    LOG_HOOK_STATUS(DrawThemeTextEx);
    LOG_HOOK_STATUS(DrawThemeBackground);
    LOG_HOOK_STATUS(DrawThemeBackgroundEx);
    LOG_HOOK_STATUS(PatBlt);
    LOG_HOOK_STATUS(RegisterClassExW);
    LOG_HOOK_STATUS(DwmSetWindowAttribute);
    LOG_HOOK_STATUS(AlphaBlend);
    LOG_HOOK_STATUS(GdiGradientFill);
    LOG_HOOK_STATUS(Rectangle);
    LOG_HOOK_STATUS(SetBkColor);
    LOG_HOOK_STATUS(SetDCBrushColor);
    LOG_HOOK_STATUS(SetDCPenColor);
}

void __fastcall HookManager::HookedElementPaint(
    void* pThis,
    HDC hdc,
    const RECT* prcBounds,
    const RECT* prcInvalid,
    RECT* prcBorder,
    RECT* prcContent
) {
    // �ؼ����裺����ԭʼ�����߼�
    // 1. ֱ����ձ���Ϊ͸��
    // if (prcBounds) {
       //  HBRUSH hTransparent = CreateSolidBrush(0x00000000); // ��ȫ͸��
        //  FillRect(hdc, prcBounds, hTransparent);
        //  DeleteObject(hTransparent);
        // }

    // 2. ��ѡ�������߿�/���ݻ���
    // �����Ҫ��ȫ�������л��ƣ�ֱ�ӷ��ؼ��ɣ�
    // return;

    // 3. ���豣���Ǳ������֣�����ԭʼ������������������
    // ����ͨ���޸Ĳ�����ƭԭʼ����
    // RECT emptyRect = { 0, 0, 0, 0 };
    OriginalElementPaint(
        pThis,
        hdc,
        prcBounds,
        prcInvalid,
        prcBorder,  // ��ƭ������Ϊ�ޱ߿�
        prcContent   // ��ƭ������Ϊ������
    );
}

// AlphaBlend ����ʵ��
BOOL WINAPI HookManager::HookedAlphaBlend(
    HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
    HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc,
    BLENDFUNCTION blendFunction)
{
	LOG_DEBUG("[HookManager][AlphaBlend]", "Skipped in transparency mode");
	return TRUE;
}

// GdiGradientFill ����ʵ��
BOOL WINAPI HookManager::HookedGradientFill(
    HDC hdc, PTRIVERTEX pVertex, ULONG nVertex,
    PVOID pMesh, ULONG nMesh, ULONG ulMode)
{
        LOG_DEBUG("[HookManager][GradientFill]", "Skipped in transparency mode");
        return TRUE; 
}

COLORREF HookManager::MakeTransparent(COLORREF color) {
    // ����Alphaͨ������RGB��Ϊ0 (��ȫ͸��)
    return (color & 0xFF000000);
}

void ApplyTransparency(void* pValue1) {
    int& bgType = *(int*)((BYTE*)pValue1 + 8);
    COLORREF& color = *(COLORREF*)((BYTE*)pValue1 + 0x10);

    switch (bgType)
    {
    // ��ɫ����
        
    case 2: {
        color = 0x00000000; // ��ȫ͸��
        break;

        // ���䱳��
        }
    case 0: case 1: case 3: case 4:
	    {
		    // ��ȡ������ɫ���� (ƫ�������ڷ�������)
    		TRIVERTEX * vertices = *(TRIVERTEX**)((BYTE*)pValue1 + 0x18);
    		int vertexCount = *(int*)((BYTE*)pValue1 + 0x20);

    		for (int i = 0; i < vertexCount; i++) {
    			// ֻ����Alphaͨ����RGB��Ϊ0
    			vertices[i].Red = 0;
    			vertices[i].Green = 0;
    			vertices[i].Blue = 0;
    		}
    		break;
	    }

    case 6:{
    		break;
    }
    }
}

void __fastcall HookManager::HookedPaintBackground(
    void* pThis,
    void* EDX,
    HDC hdc,
    void* pValue1,
    const RECT* pRect1,
    const RECT* pRect2,
    const void* pValue3,
    const void* pValue4
) {
    int originalBgType = *(int*)((BYTE*)pValue1 + 8);
    COLORREF originalColor = *(COLORREF*)((BYTE*)pValue1 + 0x10);
    void* originalGradient = nullptr;
    int originalVertexCount = 0;

    if (originalBgType >= 0 && originalBgType <= 4) {
        originalGradient = *(void**)((BYTE*)pValue1 + 0x18);
        originalVertexCount = *(int*)((BYTE*)pValue1 + 0x20);
    }

	ApplyTransparency(pValue1);

    if (OriginalPaintBackground) {
        OriginalPaintBackground(pThis, EDX, hdc, pValue1, pRect1, pRect2, pValue3, pValue4);
    }

    *(int*)((BYTE*)pValue1 + 8) = originalBgType;
    *(COLORREF*)((BYTE*)pValue1 + 0x10) = originalColor;

    if (originalBgType >= 0 && originalBgType <= 4) {
        *(void**)((BYTE*)pValue1 + 0x18) = originalGradient;
        *(int*)((BYTE*)pValue1 + 0x20) = originalVertexCount;
    }
}

BOOL HookManager::HookedGdiAlphaBlend(HDC hdcDest, 
    int xoriginDest, 
    int yoriginDest, 
    int wDest, int hDest, 
    HDC hdcSrc, int xoriginSrc, 
    int yoriginSrc, int wSrc, 
    int hSrc, BLENDFUNCTION ftn)
{
    /*
     * ����������ʵ��ͼ����޸�
     */

    return OriginalGdiAlphaBlend(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, ftn);
}

BOOL __stdcall HookManager::HookedStretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop)
{
    return OriginalStretchBlt(hdcDest, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, wSrc, hSrc, rop);
}

// Rectangle ����ʵ��
BOOL WINAPI HookManager::HookedRectangle(
    HDC hdc, int left, int top, int right, int bottom)
{
	LOG_DEBUG("[HookManager][Rectangle]", "Skipped in transparency mode");
	return TRUE;
}

// SetBkColor ����ʵ�� - ͸��ģʽ������Ҫ���ã�����Ӱ����Ⱦ
COLORREF WINAPI HookManager::HookedSetBkColor(HDC hdc, COLORREF color)
{
	LOG_DEBUG("[HookManager][SetBkColor]", "Set background color in transparency mode: ", color);
    return OriginalSetBkColor(hdc, color);
}

// SetDCBrushColor ����ʵ��
COLORREF WINAPI HookManager::HookedSetDCBrushColor(HDC hdc, COLORREF color)
{
	LOG_DEBUG("[HookManager][SetDCBrushColor]", "Set DC brush color in transparency mode: ", color);
    return OriginalSetDCBrushColor(hdc, color);
}

// SetDCPenColor ����ʵ��
COLORREF WINAPI HookManager::HookedSetDCPenColor(HDC hdc, COLORREF color)
{
	LOG_DEBUG("[HookManager][SetDCPenColor]", "Set DC pen color in transparency mode: ", color);
    return OriginalSetDCPenColor(hdc, color);
}

// GetDCPenColor ����ʵ�� - ������������
COLORREF WINAPI HookManager::HookedGetDCPenColor(HDC hdc)
{
    return OriginalGetDCPenColor(hdc);
}

BOOL WINAPI HookManager::HookedBitBlt(
    HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    /*// ����һ����Ŀ����ݵ���ʱDC
    HDC hTempDC = CreateCompatibleDC(hdc);
    if (!hTempDC) {
        LOG_ERROR("[HookManager.cpp][HookedBitBlt]", "Failed to create compatible DC");
        return OriginalBitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
    }

    // ����͸��λͼ
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hTransparentBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hTransparentBmp) {
        DeleteDC(hTempDC);
        LOG_ERROR("[HookManager.cpp][HookedBitBlt]", "Failed to create transparent bitmap");
        return OriginalBitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
    }

    // ��λͼ�������Ϊȫ͸��
    if (pBits) {
        memset(pBits, 0, cx * cy * 4); // 32λ = 4�ֽ�/����
    }

    // ѡ��DC
    HGDIOBJ hOldBmp = SelectObject(hTempDC, hTransparentBmp);

    // ִ��͸������
    BOOL result = OriginalBitBlt(hdc, x, y, cx, cy, hTempDC, 0, 0, SRCCOPY);

    // ������Դ
    SelectObject(hTempDC, hOldBmp);
    DeleteObject(hTransparentBmp);
    DeleteDC(hTempDC);*/

    //return result;
    return OriginalBitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
}


COLORREF WINAPI HookManager::HookedOriginalDwmExtendFrameIntoClientArea(
    HWND hWnd,
    _In_ const MARGINS* pMarInset)
{
    LOG_DEBUG("[HookedDwmExtendFrameIntoClientArea]",
        "Original margins: ",
        pMarInset->cxLeftWidth, ", ",
        pMarInset->cxRightWidth, ", ",
        pMarInset->cyTopHeight, ", ",
        pMarInset->cyBottomHeight);

    MARGINS margins = { -1, -1, -1, -1 };
    return OriginalDwmExtendFrameIntoClientArea(hWnd, &margins);
}


void HookManager::RemoveHooks() {
    LOG_INFO("[HookManager.cpp][RemoveHooks]", "Removing hooks");
    if (!is_installHooks){
        LOG_INFO("[HookManager.cpp][RemoveHooks]", "Repeated uninstall");
	    return;
    }
    is_installHooks = false;

    Gdiplus::GdiplusStartupInput StartupInput;
    int ret = Gdiplus::GdiplusStartup(&m_gdiplusToken, &StartupInput, NULL);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // �Ƴ�����
    DetourDetach(&(PVOID&)OriginalCreateWindowExW, HookedCreateWindowExW);
    DetourDetach(&(PVOID&)OriginalDestroyWindow, HookedDestroyWindow);
    DetourDetach(&(PVOID&)OriginalBeginPaint, HookedBeginPaint);
    DetourDetach(&(PVOID&)OriginalEndPaint, HookedEndPaint);
    DetourDetach(&(PVOID&)OriginalFillRect, HookedFillRect);
    DetourDetach(&(PVOID&)OriginalDrawTextW, HookedDrawTextW);
    DetourDetach(&(PVOID&)OriginalDrawTextExW, HookedDrawTextExW);
    DetourDetach(&(PVOID&)OriginalExtTextOutW, HookedExtTextOutW);
    DetourDetach(&(PVOID&)OriginalCreateCompatibleDC, HookedCreateCompatibleDC);
    DetourDetach(&(PVOID&)OriginalGetThemeColor, HookedGetThemeColor);
    DetourDetach(&(PVOID&)OriginalDrawThemeText, HookedDrawThemeText);
    DetourDetach(&(PVOID&)OriginalDrawThemeTextEx, HookedDrawThemeTextEx);
    DetourDetach(&(PVOID&)OriginalDrawThemeBackground, HookedDrawThemeBackground);
    DetourDetach(&(PVOID&)OriginalDrawThemeBackgroundEx, HookedDrawThemeBackgroundEx);
    DetourDetach(&(PVOID&)OriginalPatBlt, HookedPatBlt);
    DetourDetach(&(PVOID&)OriginalRegisterClassExW, HookedRegisterClassExW);
    //DetourDetach(&(PVOID&)OriginalDwmSetWindowAttribute, HookedDwmSetWindowAttribute);

    DetourTransactionCommit();

    // ������Դ
    if (m_clearBrush) {
        DeleteObject(m_clearBrush);
        m_clearBrush = nullptr;
    }

    if (transparentBrush) {
        DeleteObject(transparentBrush);
        transparentBrush = nullptr;
    }
    LOG_INFO("[HookManager.cpp][RemoveHooks]", "Hooks removed");
}

ATOM WINAPI HookManager::HookedRegisterClassExW(CONST WNDCLASSEXW* lpWndClass) {
    // ����Ƿ���Ŀ�괰����
    if (lpWndClass && lpWndClass->lpszClassName) {
        std::wstring className(lpWndClass->lpszClassName);

        if (transparentClasses.find(className) != transparentClasses.end()) {
            WNDCLASSEXW modifiedClass = *lpWndClass;
            modifiedClass.hbrBackground = transparentBrush;
            modifiedClass.style |= CS_HREDRAW | CS_VREDRAW;
            return OriginalRegisterClassExW(&modifiedClass);
        }
    }

    // ��Ŀ���࣬����ע��
    return OriginalRegisterClassExW(lpWndClass);
}

DWORD WindowsVersion::GetBuildNumber() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) {
        return 0;
    }

    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    auto RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
        GetProcAddress(hNtdll, "RtlGetVersion"));

    if (!RtlGetVersion) {
        return 0;
    }

    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    if (RtlGetVersion(&rovi) != 0) {
        return 0;
    }

    return rovi.dwBuildNumber;
}

bool WindowsVersion::IsBuildOrGreater(DWORD buildNumber) {
    return GetBuildNumber() >= buildNumber;
}

DWORD HookManager::GetSystemThemeColor() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        return color & 0x00FFFFFF; // ���� RGB ����
    }
    return 0x000000;
}

bool HookManager::SetTaskbarTransparency(HWND hwnd, HookManager::AccentState accentState, COLORREF color, BYTE alpha) {
    LOG_DEBUG("[HookManager.cpp][SetTaskbarTransparency]",
        "Setting transparency for window: ", hwnd,
        " state: ", static_cast<int>(accentState));

    if (!SetWindowCompositionAttribute) {
        LOG_ERROR("[HookManager.cpp][SetTaskbarTransparency]", "SetWindowCompositionAttribute not available");
        return false;
    }

    // Disable effects if requested
    if (accentState == ACCENT_DISABLED) {
        WINCOMPATTRDATA data = { 19 /*WCA_ACCENT_POLICY*/, nullptr, 0 };
        SetWindowCompositionAttribute(hwnd, &data);
        return true;
    }

    // If color is -1, use system theme color with custom alpha
    bool useCustomAlpha = false;
    if (color == static_cast<COLORREF>(-1)) {
        color = GetSystemThemeColor();
        useCustomAlpha = true; // Only use alpha parameter when color is -1
    }

    // Set AccentFlags
    DWORD flags = 0;
    switch (accentState) {
    case ACCENT_ENABLE_GRADIENT:
        flags = 2 | 0x2;
        break;
    case ACCENT_ENABLE_TRANSPARENTGRADIENT:
        flags = 3 | 0x2;
        break;
    case ACCENT_ENABLE_BLURBEHIND:
        flags = 0x20 | 0x2;
        break;
    case ACCENT_ENABLE_ACRYLICBLURBEHIND:
        flags = 0x20 | 0x40 | 0x80 | 0x100 | 0x2; // Typical acrylic flags
        break;
    default:
        flags = 2 | 0x2;
    }

    // Extract color components
    BYTE r = GetRValue(color);
    BYTE g = GetGValue(color);
    BYTE b = GetBValue(color);
    BYTE finalAlpha = useCustomAlpha ? alpha : 0xFF; // Use alpha only for system color

    // Construct ABGR format gradientColor
    DWORD gradientColor =
        (static_cast<DWORD>(finalAlpha) << 24) |  // Alpha
        (static_cast<DWORD>(b) << 16) |           // Blue
        (static_cast<DWORD>(g) << 8) |            // Green
        r;                                        // Red

    AccentPolicy accent = {
        accentState,
        flags,
        gradientColor,
        0
    };

    WINCOMPATTRDATA data = {
        19, // WCA_ACCENT_POLICY
        &accent,
        sizeof(accent)
    };

    // Apply changes
    WINCOMPATTRDATA reset = { 19, nullptr, 0 };
    SetWindowCompositionAttribute(hwnd, &reset);
    bool result = SetWindowCompositionAttribute(hwnd, &data) != FALSE;
    if (!result) {
        LOG_ERROR("[HookManager.cpp][SetTaskbarTransparency]", "SetWindowCompositionAttribute failed");
    }

    return result;
}

void HookManager::RefreshConfig() {
    if (!m_clearBrush) {
        m_clearBrush = CreateSolidBrush(0x00000000);
    }
    LOG_INFO("[HookManager.cpp][RefreshConfig]", "Refreshing configuration");
    // ��ConfigManager��ȡ����
    const auto& config = ConfigManager::GetConfig();

    // ����Ч������
    switch (config.effectType) {
    case ConfigManager::EffectType::Acrylic:
        m_config.effType = 1;
        LOG_DEBUG("[HookManager.cpp][RefreshConfig]", "Effect type: Acrylic");
        break;
    case ConfigManager::EffectType::Mica:
        m_config.effType = 2;
        LOG_DEBUG("[HookManager.cpp][RefreshConfig]", "Effect type: Mica");
        break;
    case ConfigManager::EffectType::Blur:
        m_config.effType = 3;
        LOG_DEBUG("[HookManager.cpp][RefreshConfig]", "Effect type: Blur");
        break;
    default:
        m_config.effType = 0;
        LOG_DEBUG("[HookManager.cpp][RefreshConfig]", "Effect type: None");
    }
    m_config.blendColor = config.blendColor.GetValue() | (config.blendColor.GetAlpha() << 24);
    m_config.imagePath = config.imagePath;
    m_config.imageOpacity = config.imageOpacity;
    m_config.imageBlurRadius = config.imageBlurRadius;
    m_config.smallborder = true;

    m_config.automatic_acquisition_color = config.automatic_acquisition_color;
    if (config.automatic_acquisition_color && config.automatic_acquisition_color_transparency != -1){
        m_config.automatic_acquisition_color_transparency = config.automatic_acquisition_color_transparency;
        OutputDebugStringW(L"[HookManager.cpp][RefreshConfig] You didn't set the opacity parameter");
    }else {
        m_config.automatic_acquisition_color_transparency = 50;
    }
    LOG_INFO("[HookManager.cpp][RefreshConfig]", "Configuration refreshed");
}

int WINAPI HookManager::HookedDrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
    // ����������λ��Ѵ�����߳�
    if ((format & DT_CALCRECT) || m_drawtextState.find(GetCurrentThreadId()) != m_drawtextState.end()) {
        return OriginalDrawTextW(hdc, lpchText, cchText, lprc, format);
    }

    HRESULT hr = S_OK;
    DTTOPTS opts = { sizeof(DTTOPTS) };
    opts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR | DTT_CALLBACK;
    opts.crText = GetTextColor(hdc);
    opts.pfnDrawTextCallback = [](HDC hdc, LPWSTR text, int len, LPRECT rect, UINT fmt, LPARAM) -> int {
        return OriginalDrawTextW(hdc, text, len, rect, fmt);
        };

    auto drawFunc = [&](HDC hDC) {
        HTHEME hTheme = OpenThemeData(nullptr, L"Menu");
        if (hTheme) {
            hr = OriginalDrawThemeTextEx(
                hTheme, hDC, 0, 0,
                lpchText, cchText,
                format, lprc, &opts
            );
            CloseThemeData(hTheme);
        }
        };

    if (!AlphaBuffer(hdc, lprc, drawFunc)) {
        hr = OriginalDrawTextW(hdc, lpchText, cchText, lprc, format);
    }

    return SUCCEEDED(hr) ? 1 : hr;
}

int __stdcall HookManager::HookedDrawTextExW(
    HDC hdc, LPWSTR lpchText, int cchText,
    LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp)
{
    static std::unordered_map<DWORD, bool> thList;
    DWORD curTh = GetCurrentThreadId();

    if (!lpdtp && !(format & DT_CALCRECT) &&
        thList.find(curTh) == thList.end() &&
        m_drawtextState.find(curTh) == m_drawtextState.end())
    {
        thList.insert(std::make_pair(curTh, true));
        auto ret = HookedDrawTextW(hdc, lpchText, cchText, lprc, format);
        thList.erase(curTh);
        return ret;
    }
    return OriginalDrawTextExW(hdc, lpchText, cchText, lprc, format, lpdtp);
}

bool HookManager::IsDUIThread() {
    auto iter = m_DUIList.find(GetCurrentThreadId());
    return iter != m_DUIList.end();
}

BOOL __stdcall HookManager::HookedExtTextOutW(
    HDC hdc, int x, int y, UINT option,
    const RECT* lprect, LPCWSTR lpString,
    UINT c, const INT* lpDx)
{
    std::wstring str;
    if (lpString) str = lpString;

    static std::unordered_map<DWORD, bool> thList;
    DWORD curTh = GetCurrentThreadId();

    if (IsDUIThread() && !(option & ETO_GLYPH_INDEX) &&
        !(option & ETO_IGNORELANGUAGE) &&
        thList.find(curTh) == thList.end() &&
        str != L"" &&
        m_drawtextState.find(curTh) == m_drawtextState.end())
    {
        thList.insert(std::make_pair(curTh, true));

        RECT rect = { 0 };
        if ((option & ETO_OPAQUE || option & ETO_CLIPPED) && lprect)
            rect = *lprect;
        else
            OriginalDrawTextW(hdc, lpString, c, &rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);

        if (!lpDx) {
            HookedDrawTextW(hdc, lpString, c, &rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
        else {
            RECT rc = { x, y, x + (rect.right - rect.left),
                        y + (rect.bottom - rect.top) };

            if (option & ETO_CLIPPED)
            {
                SaveDC(hdc);
                IntersectClipRect(hdc, rect.left, rect.top, rect.right, rect.bottom);
            }

            HRESULT hr = S_OK;
            DTTOPTS opts = { sizeof(DTTOPTS) };
            opts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR | DTT_CALLBACK;
            opts.crText = GetTextColor(hdc);
            opts.pfnDrawTextCallback = [](HDC hdc, LPWSTR text,
                int len, LPRECT rect, UINT format, LPARAM) -> int {
                    return OriginalDrawTextW(hdc, text, len, rect, format);
                };

            // ʹ��AlphaBuffer���л���
            auto drawFunc = [&](HDC hDC) {
                HTHEME hTheme = OpenThemeData(nullptr, L"Menu");
                if (!hTheme) return;

                std::wstring batchStr;
                bool batch = true;

                batchStr += lpString[0];

                int srcExtra = GetTextCharacterExtra(hdc);
                SetTextCharacterExtra(hDC, lpDx[0]);

                RECT batchRc = rc;
                for (UINT i = 0; i < c; i++)
                {
                    if (i != 0) {
                        if (lpDx[i] == lpDx[i - 1]) {
                            if (!batch)
                            {
                                batch = true;
                                SetTextCharacterExtra(hDC, lpDx[i]);
                            }
                            batchStr += lpString[i];
                        }
                        else
                        {
                            // ���Ƶ�ǰ����
                            hr = OriginalDrawThemeTextEx(hTheme, hDC, 0, 0,
                                batchStr.c_str(), batchStr.length(),
                                DT_LEFT | DT_TOP | DT_SINGLELINE, &batchRc, &opts);

                            batch = false;
                            batchStr = lpString[i];
                            SetTextCharacterExtra(hDC, lpDx[i]);
                            batchRc.left = rc.left;
                        }
                    }

                    // �������һ��
                    if (i == c - 1)
                    {
                        hr = OriginalDrawThemeTextEx(hTheme, hDC, 0, 0,
                            batchStr.c_str(), batchStr.length(),
                            DT_LEFT | DT_TOP | DT_SINGLELINE, &batchRc, &opts);
                    }

                    rc.left += lpDx[i];
                }
                SetTextCharacterExtra(hDC, srcExtra);
                CloseThemeData(hTheme);
                };

            if (!AlphaBuffer(hdc, &rc, drawFunc))
            {
                hr = OriginalExtTextOutW(hdc, x, y, option, lprect, lpString, c, lpDx);
            }

            if (option & ETO_CLIPPED)
                RestoreDC(hdc, -1);
        }

        thList.erase(curTh);
        return TRUE;
    }
    return OriginalExtTextOutW(hdc, x, y, option, lprect, lpString, c, lpDx);
}

HDC __stdcall HookManager::HookedCreateCompatibleDC(HDC hDC)
{
    HDC retDC = OriginalCreateCompatibleDC(hDC);
    auto iter = m_DUIList.find(GetCurrentThreadId());
    if (iter != m_DUIList.end()) {
        if (iter->second.hDC == hDC) {
            iter->second.hDC = retDC;
            return retDC;
        }
    }

    DWORD build = WindowsVersion::GetBuildNumber();
    if (build < 22000) {
        HWND wnd = WindowFromDC(hDC);
        if (wnd && GetWindowClassName(wnd) == L"NetUIHWND") {
            m_ribbonPaint[GetCurrentThreadId()] = std::make_pair(wnd, retDC);
        }
    }
    return retDC;
}

BOOL WINAPI HookManager::HookedPatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) {
    // �޸�ѡ���Alpha
    if (IsDUIThread() && rop == PATCOPY) {
        static std::unordered_map<DWORD, bool> thList;

        DWORD curThread = GetCurrentThreadId();
        if (thList.find(curThread) != thList.end())
            return OriginalPatBlt(hdc, x, y, w, h, rop);
        thList.insert(std::make_pair(curThread, true));
        HBRUSH hbr = (HBRUSH)SelectObject(hdc, GetSysColorBrush(COLOR_WINDOW));
        LOGBRUSH lbr;
        GetObjectW(hbr, sizeof(lbr), &lbr);
        Gdiplus::SolidBrush brush(Gdiplus::Color::Transparent);
        brush.SetColor(Gdiplus::Color::MakeARGB(200, GetRValue(lbr.lbColor),
            GetGValue(lbr.lbColor), GetBValue(lbr.lbColor)));
        Gdiplus::Graphics gp(hdc);
        gp.FillRectangle(&brush, Gdiplus::Rect(x, y, w, h));
        SelectObject(hdc, hbr);
        thList.erase(curThread);
        return TRUE;
    }
    return OriginalPatBlt(hdc, x, y, w, h, rop);
}

HRESULT WINAPI HookManager::HookedGetThemeColor(
    HTHEME hTheme, int iPartId, int iStateId, int iPropId, COLORREF* pColor)
{
    HRESULT hr = OriginalGetThemeColor(hTheme, iPartId, iStateId, iPropId, pColor);
    std::wstring className = GetThemeClassName(hTheme);

    LOG_DEBUG("[HookManager.cpp][HookedGetThemeColor]",
        "When the program tries to get the color of the component, the intercepted component is called:",
        className);

    // ���ؼ��ؼ�������Ϊ��ɫ��ʵ��͸��Ч��
    if (iPropId == TMT_FILLCOLOR && IsDUIThread())
    {
        if (((className == L"ItemsView" || className == L"ExplorerStatusBar" || className == L"ExplorerNavPane")
            && (iPartId == 0 && iStateId == 0))
            || (className == L"ReadingPane" && iPartId == 1 && iStateId == 0) // FILL Color
            || (className == L"ProperTree" && iPartId == 2 && iStateId == 0)
            || (className == L"CommandModule")
            || (className == L"Header")
            || (className == L"TryHarder")
            || (className == L"Edit")
            || (className == L"Tooltip")
            || (className == L"ReadingPane")
            || (className == L"PreviewPane")
            || (className == L"MediaStyle")
            || (className == L"ListViewStyle")
            || (className == L"ControlPanelStyle")
            || (className == L"InfoBar")
            || (className == L"FileExplorerBannerContainer")
            || (className == L"ExplorerNavPane")
            )
        {
            *pColor = RGB(0, 0, 0);
        }
    }
    return hr;
}

std::wstring HookManager::GetThemeClassName(HTHEME hTheme)
{
    typedef HRESULT(WINAPI* GetThemeClass)(HTHEME, LPCTSTR, int);
    static auto getname = (GetThemeClass)GetProcAddress(GetModuleHandleW(L"uxtheme"), MAKEINTRESOURCEA(74));

    std::wstring ret;
    if (getname)
    {
        wchar_t buffer[255] = { 0 };
        HRESULT hr = getname(hTheme, buffer, 255);
        return SUCCEEDED(hr) ? buffer : L"";
    }
    return ret;
}

HRESULT HookManager::_DrawThemeTextEx(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCTSTR pszText,
    int cchText, DWORD dwTextFlags, LPCRECT pRect, const DTTOPTS* pOptions)
{
    HRESULT ret = S_OK;

    if (pOptions && !(pOptions->dwFlags & DTT_CALCRECT) && !(pOptions->dwFlags & DTT_COMPOSITED))
    {
        DTTOPTS Options = *pOptions;
        Options.dwFlags |= DTT_COMPOSITED;

        HRESULT ret = S_OK;

        auto fun = [&](HDC hDC)
            {
                auto tid = GetCurrentThreadId();
                m_drawtextState.insert(std::make_pair(tid, true));
                ret = OriginalDrawThemeTextEx(hTheme, hDC, iPartId, iStateId, pszText, cchText, dwTextFlags,
                    (LPRECT)pRect, &Options);
                m_drawtextState.erase(tid);
            };

        if (!AlphaBuffer(hdc, (LPRECT)pRect, fun))
            goto Org;

        return ret;
    }
    else {
    Org:
        ret = OriginalDrawThemeTextEx(hTheme, hdc, iPartId, iStateId, pszText, cchText, dwTextFlags, (LPRECT)pRect, pOptions);
    }
    return ret;
}

HRESULT __stdcall HookManager::HookedDrawThemeText(
    HTHEME hTheme, HDC hdc, int iPartId, int iStateId,
    LPCWSTR pszText, int cchText, DWORD dwTextFlags,
    DWORD dwTextFlags2, LPCRECT pRect)
{
    HRESULT ret = S_OK;

    DTTOPTS Options = { sizeof(DTTOPTS) };
    RECT Rect = *pRect;
    return _DrawThemeTextEx(hTheme, hdc, iPartId, iStateId, pszText, cchText, dwTextFlags, &Rect, &Options);
}

HRESULT __stdcall HookManager::HookedDrawThemeTextEx(
    HTHEME hTheme, HDC hdc, int iPartId, int iStateId,
    LPCWSTR pszText, int cchText, DWORD dwTextFlags,
    LPCRECT pRect, const DTTOPTS* pOptions)
{
    if (pOptions && !(pOptions->dwFlags & DTT_CALCRECT) &&
        !(pOptions->dwFlags & DTT_COMPOSITED))
    {
        DTTOPTS Options = *pOptions;
        Options.dwFlags |= DTT_COMPOSITED;

        HRESULT hr = S_OK;
        auto fun = [&](HDC hDC) {
            auto tid = GetCurrentThreadId();
            m_drawtextState.insert(std::make_pair(tid, true));
            hr = OriginalDrawThemeTextEx(hTheme, hDC, iPartId, iStateId,
                pszText, cchText, dwTextFlags, (LPRECT)pRect, &Options);
            m_drawtextState.erase(tid);
            };

        if (!AlphaBuffer(hdc, (LPRECT)pRect, fun)) {
            hr = OriginalDrawThemeTextEx(hTheme, hdc, iPartId, iStateId,
                pszText, cchText, dwTextFlags, (LPRECT)pRect, pOptions);
        }
        return hr;
    }

    return OriginalDrawThemeTextEx(hTheme, hdc, iPartId, iStateId,
        pszText, cchText, dwTextFlags, (LPRECT)pRect, pOptions);
}

bool PaintScroll(HDC hdc, int iPartId, int iStateId, LPCRECT pRect)
{
    if ((iPartId == SBP_UPPERTRACKVERT || iPartId == SBP_LOWERTRACKVERT
        || iPartId == SBP_UPPERTRACKHORZ || iPartId == SBP_LOWERTRACKHORZ
        || iPartId == SBP_ARROWBTN)
        && (iStateId == 1 || iStateId == 2 || iStateId == 3 || iStateId == 5 || iStateId == 9 || iStateId == 13))
    {
        return true;
    }
    if (iPartId == SBP_THUMBBTNVERT || iPartId == SBP_THUMBBTNHORZ)
    {
        Gdiplus::Graphics draw(hdc);
        draw.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        UINT dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        float roundValue = ((float)dpiY / 96.f) * (iStateId == 1 ? 3.f : 4.f);

        Gdiplus::SolidBrush brush(Gdiplus::Color(135, 135, 135));

        int x = pRect->left;
        int y = pRect->top;
        int width = pRect->right - pRect->left;
        int height = pRect->bottom - pRect->top;

        if (iPartId == SBP_THUMBBTNVERT)
        {
            int newWidth = int((float)width / (iStateId == 1 ? 2.4f : 1.8f));
            x += (width - newWidth) / 2;
            width = newWidth;
        }
        else
        {
            int newHeight = int((float)height / (iStateId == 1 ? 2.4f : 1.8f));
            y += (height - newHeight) / 2;
            height = newHeight;
        }

        Gdiplus::Rect rc(x, y, width, height);

        RoundRectPath path(rc, roundValue);
        Gdiplus::Rect rc1 = rc;
        rc1.Width += (INT)roundValue;
        RoundRectPath pathClip(rc1, roundValue);
        draw.SetClip(&pathClip);
        draw.FillPath(&brush, &path);
        draw.ResetClip();
        return true;
    }
}

HRESULT __stdcall HookManager::HookedDrawThemeBackground(
    HTHEME hTheme, HDC hdc, int iPartId, int iStateId,
    LPCRECT pRect, LPCRECT pClipRect)
{
    std::wstring className = GetThemeClassName(hTheme);
    LOG_DEBUG("[HookManager.cpp][HookedDrawThemeBackground]", "Get the component when drawing the theme:", className);
   
    // �� OutputDebugStringA �����һ��
    /*{
        std::string msg = "[HookManager.cpp][HookedDrawThemeBackground] className = "
            + std::string(className.begin(), className.end()) + "\n";
        OutputDebugStringA(msg.c_str());
    }*/
    
    if (className == L"Rebar" && (iPartId == RP_BACKGROUND || iPartId == RP_BAND) && iStateId == 0)
    {
        return S_OK;
    }

    if (className == L"Rebar" && (iPartId == RP_BACKGROUND || iPartId == RP_BAND) && iStateId == 0)
    {
        OriginalFillRect(hdc, pRect, m_clearBrush);
        return S_OK;
    }
    if (className == L"AddressBand" || className == L"SearchBox"
        && iPartId == 1 && (iStateId == 1 || iStateId == 2))
    {
        return S_OK;
    }
    if (className == L"ScrollBar")
    {
        if (PaintScroll(hdc, iPartId, iStateId, pRect))
            return S_OK;
    }
    return OriginalDrawThemeBackground(hTheme, hdc, iPartId, iStateId, pRect, pClipRect);
}

HRESULT __stdcall HookManager::HookedDrawThemeBackgroundEx(
    HTHEME hTheme, HDC hdc, int iPartId, int iStateId,
    LPCRECT pRect, const DTBGOPTS* pOptions)
{
    std::wstring className = GetThemeClassName(hTheme);
    LOG_DEBUG("[HookManager.cpp][HookedDrawThemeBackgroundEx]", "Get the component when drawing the theme:", className);

    // �� OutputDebugStringA �����һ��
    {
        std::string msg = "[HookManager.cpp][HookedDrawThemeBackgroundEx] className = "
            + std::string(className.begin(), className.end()) + "\n";
        OutputDebugStringA(msg.c_str());
    }
    if (className == L"Header") {
        if ((iPartId == HP_HEADERITEM && iStateId == HIS_NORMAL) ||
            (iPartId == HP_HEADERITEM && iStateId == HIS_SORTEDNORMAL) ||
            (iPartId == 0 && iStateId == HIS_NORMAL))
            return S_OK;
    }
    else if (className == L"Rebar") {
        if ((iPartId == RP_BACKGROUND || iPartId == RP_BAND) && iStateId == 0) {
            return S_OK;
        }
    }
    else if (className == L"CommandModule" && IsDUIThread()) {
        if (iPartId == 1 && iStateId == 0) {
            FillRect(hdc, pRect, m_clearBrush);
            return S_OK;
        }
    }
    else if (className == L"PreviewPane") {
        //if (iPartId == 1 && iStateId == 1) // 3 3
            return S_OK;
    }
    return OriginalDrawThemeBackgroundEx(hTheme, hdc, iPartId, iStateId, pRect, pOptions);
}


void HookManager::StartAero(HWND hwnd, int type, COLORREF color, bool blend){
    if (SetWindowCompositionAttribute)
    {
        ACCENTPOLICY policy = { type == 0 ? 3 : 4, 0, 0, 0 };
        if (blend)
        {
            policy.nFlags = 3;
            policy.nColor = color;
        }
        else if (type == 1)
        {
            policy.nFlags = 1;
            policy.nColor = 0x00FFFFFF;
        }
        else
        {
            policy.nFlags = 0;
            policy.nColor = 0;
        }
        WINCOMPATTRDATA data = { 19, &policy, sizeof(ACCENTPOLICY) };
        SetWindowCompositionAttribute(hwnd, &data);
    }
}

inline BYTE M_GetAValue(COLORREF rgba) {
    return BYTE(ULONG(rgba >> 24) & 0xff);
}

// ����ģ��Ч������
void HookManager::SetWindowBlur(HWND hWnd) {
    LOG_DEBUG("[HookManager.cpp][SetWindowBlur]", "Setting blur for window: ", hWnd);
    bool isBlend = M_GetAValue(m_config.blendColor) != 0;
    DWORD build = WindowsVersion::GetBuildNumber();

    try {
    if (build >= 22000)
    {
        if (build >= 22500)
        {
            RECT pRect;
            GetWindowRect(hWnd, &pRect);
            OnWindowSize(hWnd, pRect.bottom - pRect.top);
            if (m_config.effType == 1)
            {
                int type = 0;
                OriginalDwmSetWindowAttribute(hWnd, 1029, &type, sizeof(type));

                DWM_BLURBEHIND blur = { 0 };
                blur.dwFlags = DWM_BB_ENABLE;
                blur.fEnable = TRUE;

                DwmEnableBlurBehindWindow(hWnd, &blur);

                if (isBlend){
	                StartAero(hWnd, 1, m_config.blendColor, true);
                }
                else {
                    DWM_SYSTEMBACKDROP_TYPE type1 = DWMSBT_TRANSIENTWINDOW;
                    OriginalDwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &type1, sizeof(type1));
                }
            }
            else if (m_config.effType == 0) {
	            if (isBlend)
	            {
                    SetTaskbarTransparency(hWnd, AccentState::ACCENT_ENABLE_TRANSPARENTGRADIENT, m_config.blendColor);
	            }
	            else
	            {
                    if (m_config.automatic_acquisition_color_transparency == -1) {
                        m_config.automatic_acquisition_color_transparency = 50;
                    }
                    SetTaskbarTransparency(hWnd, AccentState::ACCENT_ENABLE_TRANSPARENTGRADIENT, -1, m_config.automatic_acquisition_color_transparency);
	            }
                
            }
            LOG_DEBUG("[HookManager.cpp][SetWindowBlur]", "Using Windows 11 blur method");
            return;
        }

        switch (m_config.effType)
        {
        case 0:
        {
            int type = 0;
            OriginalDwmSetWindowAttribute(hWnd, 1029, &type, sizeof(type));

            StartAero(hWnd, 0, m_config.blendColor, isBlend);
            break;
        }
        case 1:
        {
            //ȡ����������MicaЧ��
            int type = 0;
            HRESULT hr = OriginalDwmSetWindowAttribute(hWnd, 1029, &type, sizeof(type));

            StartAero(hWnd, 1, m_config.blendColor, isBlend);

            //���ñ�������ɫ
            COLORREF color = m_config.blendColor;
            OriginalDwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &color, sizeof(color));
            break;
        }
        case 2:
            StartAero(hWnd, 2, 0, false);
        }
        LOG_DEBUG("[HookManager.cpp][SetWindowBlur]", "Using Windows 11 blur method");
    }
    else
    {
	    StartAero(hWnd, m_config.effType == 1 ? 0 : 1, m_config.blendColor, isBlend);
        LOG_DEBUG("[HookManager.cpp][SetWindowBlur]", "Using Windows 10 blur method");
    }
    }
    catch (const std::exception& e) {
        LOG_ERROR("[HookManager.cpp][SetWindowBlur]", "Exception in SetWindowBlur: ", e.what());
    }
    catch (...) {
        LOG_ERROR("[HookManager.cpp][SetWindowBlur]", "Unknown exception in SetWindowBlur");
    }

    LOG_DEBUG("[HookManager.cpp][SetWindowBlur]", "Blur set successfully for window: ", hWnd);
}


void HookManager::OnWindowSize(HWND hWnd, int newHeight)
{
    MARGINS margin = { -1 };
    if (M_GetAValue(m_config.blendColor) != 0 && m_config.effType == 1) {
        margin.cyTopHeight = GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(hWnd)) + 10;
    }
    else
    {
        margin = { 0 };
        margin.cyTopHeight = newHeight;
    }
    OriginalDwmExtendFrameIntoClientArea(hWnd, &margin);
    DwmFlush();
}

HRESULT HookManager::DwmUpdateAccentBlurRect(HWND hWnd, RECT* prc)
{
    typedef HRESULT(WINAPI* function)(HWND, RECT*);
    static HMODULE hModule = GetModuleHandleW(L"dwmapi.dll");
    if (hModule)
    {
        static function pfun = (function)GetProcAddress(hModule, MAKEINTRESOURCEA(159));
        return pfun(hWnd, prc);
    }
    return E_NOTIMPL;
}

// ���໯���ڹ���
LRESULT WINAPI HookManager::WndSubProc(
    HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    WindowEffect* pWindowEffect = nullptr;
    {
        std::lock_guard<std::mutex> lock(effectMutex);
        auto it = windowEffects.find(hWnd);
        if (it != windowEffects.end()) {
            pWindowEffect = &it->second;
        }
    }

    // ������´��ڳߴ��Lambda�����������ظ����룩
    auto UpdateWindowSize = [hWnd]() {
        RECT rect;
        GetWindowRect(hWnd, &rect);
        OnWindowSize(hWnd, rect.bottom - rect.top);
        };

    auto RenderBackgroundImage = [hWnd](HDC hdc) -> bool {
        if (HookManager::m_config.imagePath.empty()) {
            return false;
        }

        // ����ͼƬ
        HBITMAP hBitmap = (HBITMAP)LoadImageW(
            NULL,
            HookManager::m_config.imagePath.c_str(),
            IMAGE_BITMAP,
            0, 0,
            LR_LOADFROMFILE | LR_CREATEDIBSECTION
        );

        if (!hBitmap) {
            return false;
        }

        // ��ȡ���ڿͻ����ߴ�
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // �����ڴ�DC
        HDC hMemDC = CreateCompatibleDC(hdc);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);

        // ��ȡλͼ�ߴ�
        BITMAP bm;
        GetObject(hBitmap, sizeof(BITMAP), &bm);

        // ����͸����
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(HookManager::m_config.imageOpacity * 255), 0 };

        // ��ȾͼƬ����������������ڣ�
        OriginalAlphaBlend(
            hdc,
            rcClient.left, rcClient.top,
            rcClient.right - rcClient.left,
            rcClient.bottom - rcClient.top,
            hMemDC,
            0, 0,
            bm.bmWidth,
            bm.bmHeight,
            bf
        );

        // ������Դ
        SelectObject(hMemDC, hOldBmp);
        DeleteDC(hMemDC);
        DeleteObject(hBitmap);
        return true;
        };

    // ���� dwRefData ��֧����
    if (dwRefData == 0) {
        switch (message) {
        case WM_SIZE: {
            int height = HIWORD(lparam);
            OnWindowSize(hWnd, height);
            break;
        }

        case WM_ACTIVATE: {
            LRESULT ret = DefSubclassProc(hWnd, message, wparam, lparam);
            UpdateWindowSize();  // ʹ��Lambdaͳһ���³ߴ�
            SetWindowBlur(hWnd); // ���ڼ���ʱ����ģ��Ч��
            return ret;
        }
        case WM_SYSCOMMAND: {
            if (wparam == SC_MAXIMIZE || wparam == SC_RESTORE) {
                LRESULT ret = DefSubclassProc(hWnd, message, wparam, lparam);
                UpdateWindowSize();  // ���/�ָ�ʱ���³ߴ�
                return ret;
            }
            break;
        }
        }
    }
    else if (dwRefData == 1 && message == WM_WINDOWPOSCHANGED) {
        LRESULT ret = DefSubclassProc(hWnd, message, wparam, lparam);
        LPWINDOWPOS pos = (LPWINDOWPOS)lparam;

        RECT rc = { -1 };
        if (!IsZoomed(hWnd)) {
            DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc));
            OffsetRect(&rc, -rc.left, -rc.top);

            int width = (pos->cx - (rc.right - rc.left)) / 2;
            rc.left += width;
            rc.right += width;
        }
        DwmUpdateAccentBlurRect(hWnd, &rc);
        return ret;
    }

    // ȫ����Ϣ����������dwRefData��
    switch (message) {
        // ���ر���������Ϣ
    case WM_USER_REDRAW:
        RedrawWindow(hWnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_FRAME);
        return 0;
    case WM_SIZE:
        for (auto& pair : HookManager::s_imageCache) {
            pair.second.needsUpdate = true;
        }
        break;
    case WM_ERASEBKGND: {
        if (pWindowEffect) {
            HDC hdc = (HDC)wparam;
            RECT rc;
            GetClientRect(hWnd, &rc);

            // �ȳ�����ȾͼƬ
            if (!RenderBackgroundImage(hdc)) {
                // ͼƬ��Ⱦʧ��ʱ���˵�͸������
                FillRect(hdc, &rc, transparentBrush);
            }
            return 1;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // �ȵ���Ĭ�ϻ���
        LRESULT ret = DefSubclassProc(hWnd, message, wparam, lparam);

        // �ٵ���ͼƬ
        if (pWindowEffect) {
            RenderBackgroundImage(hdc);
        }

        EndPaint(hWnd, &ps);
        return ret;
    }

    case WM_NCCALCSIZE:
        if (lparam) {
            UINT dpi = GetDpiForWindow(hWnd);
            int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
            int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
            int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            params->rgrc[0].left += frameX + padding;
            params->rgrc[0].right -= frameX + padding;
            params->rgrc[0].bottom -= frameY + padding;
            return 0;
        }
        break;

    case WM_SETTINGCHANGE:
        if (lparam && CompareStringOrdinal(
            reinterpret_cast<LPCWCH>(lparam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
        {
            SetWindowBlur(hWnd);  // ����仯ʱ����ģ��Ч��
        }
        break;

    case WM_DPICHANGED: {
        LRESULT ret = DefSubclassProc(hWnd, message, wparam, lparam);
        auto iter = m_DUIList.find(GetCurrentThreadId());
        if (iter != m_DUIList.end()) {
            iter->second.treeDraw = true;
        }
        UpdateWindowSize();  // DPI�仯ʱ���³ߴ�
        return ret;
    }
    }

    return DefSubclassProc(hWnd, message, wparam, lparam);
}

struct ComUninitGuard {
    ~ComUninitGuard() { CoUninitialize(); }
};

std::wstring ConvertTolower(std::wstring str){
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

bool ForceSetOpacityFromUIA(CComPtr<IUIAutomationElement> target, double newOpacity)
{
    if (!target) {
        LOG_ERROR("[ForceSetOpacityFromUIA]", L"target is null");
        return false;
    }

    // 1) ������ NativeWindowHandle��HWND��
    VARIANT varHwnd;
    VariantInit(&varHwnd);
    HRESULT hr = target->GetCurrentPropertyValue(UIA_NativeWindowHandlePropertyId, &varHwnd);
    wchar_t tmp[256];
    swprintf_s(tmp, L"[ForceSetOpacityFromUIA] GetCurrentPropertyValue(UIA_NativeWindowHandle) hr=0x%08X vt=%d\n", hr, varHwnd.vt);
    OutputDebugStringW(tmp);
    LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

    HWND hwnd = nullptr;
    if (SUCCEEDED(hr)) {
        if (varHwnd.vt == VT_I4) {
            hwnd = (HWND)(INT_PTR)(varHwnd.intVal);
        }
        else if (varHwnd.vt == VT_I8) {
            hwnd = (HWND)(INT_PTR)(varHwnd.llVal);
        }
        else if (varHwnd.vt == VT_UI4) {
            hwnd = (HWND)(UINT_PTR)varHwnd.uintVal;
        }
        else {
            // ������ VT_EMPTY / VT_NULL
            hwnd = nullptr;
        }
    }
    VariantClear(&varHwnd);

    if (hwnd == nullptr) {
        OutputDebugStringW(L"[ForceSetOpacityFromUIA] NativeWindowHandle is null. Trying runtime class / runtime id fallback.\n");
        LOG_WARN("[ForceSetOpacityFromUIA]", L"NativeWindowHandle is null");
    }
    else {
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] found HWND = 0x%p\n", hwnd);
        OutputDebugStringW(tmp);
        LOG_INFO("[ForceSetOpacityFromUIA]", tmp);
    }

    // 2) ����õ� hwnd�������̹����������ǵ�ǰ���̻�����ע��Ľ��̲ſ���ֱ�Ӳ����ڴ����
    if (hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] HWND owner pid=%u curpid=%u\n", pid, GetCurrentProcessId());
        OutputDebugStringW(tmp);
        LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

        if (pid != GetCurrentProcessId()) {
            // �������ͬ���̣��Կɳ��� AccessibleObjectFromWindow �� native OM
            OutputDebugStringW(L"[ForceSetOpacityFromUIA] HWND not in current process - will try AccessibleObjectFromWindow(OBJID_NATIVEOM)\n");
            LOG_DEBUG("[ForceSetOpacityFromUIA]", L"HWND not same process");
        }
        else {
            OutputDebugStringW(L"[ForceSetOpacityFromUIA] HWND is in current process - we can try process-local tricks\n");
            LOG_DEBUG("[ForceSetOpacityFromUIA]", L"HWND same process");
        }

        // 2a) ���� AccessibleObjectFromWindow -> IDispatch��Native OLE Object Model��
        CComPtr<IDispatch> pDisp;
        HRESULT hrAO = ::AccessibleObjectFromWindow(hwnd, OBJID_NATIVEOM, IID_IDispatch, (void**)&pDisp);
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] AccessibleObjectFromWindow hr=0x%08X pDisp=%p\n", hrAO, (void*)pDisp.p);
        OutputDebugStringW(tmp);
        LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

        if (SUCCEEDED(hrAO) && pDisp) {
            // �ɹ��õ�ĳ�� native object �� IDispatch ���� ���� QueryInterface IUnknown -> IInspectable
            CComPtr<IUnknown> pUnk;
            hr = pDisp->QueryInterface(IID_IUnknown, (void**)&pUnk);
            swprintf_s(tmp, L"[ForceSetOpacityFromUIA] pDisp->QueryInterface(IUnknown) hr=0x%08X unk=%p\n", hr, (void*)pUnk.p);
            OutputDebugStringW(tmp);
            LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

            if (SUCCEEDED(hr) && pUnk) {
                // ���� QueryInterface �� IInspectable���ܿ���ʧ�ܣ�
                IInspectable* pInspect = nullptr;
                hr = pUnk->QueryInterface(__uuidof(IInspectable), (void**)&pInspect);
                swprintf_s(tmp, L"[ForceSetOpacityFromUIA] pUnk->QI(IInspectable) hr=0x%08X pInspect=%p\n", hr, (void*)pInspect);
                OutputDebugStringW(tmp);
                LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

                if (SUCCEEDED(hr) && pInspect) {
                    // �ɹ��õ� IInspectable������ QI IUIElement �� put_Opacity
                    ABI::Windows::UI::Xaml::IUIElement* pUi = nullptr;
                    hr = pInspect->QueryInterface(__uuidof(ABI::Windows::UI::Xaml::IUIElement), (void**)&pUi);
                    swprintf_s(tmp, L"[ForceSetOpacityFromUIA] pInspect->QI(IUIElement) hr=0x%08X pUi=%p\n", hr, (void*)pUi);
                    OutputDebugStringW(tmp);
                    LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

                    if (SUCCEEDED(hr) && pUi) {
                        // �������� opacity
                        HRESULT hrSet = pUi->put_Opacity(newOpacity);
                        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] pUi->put_Opacity hr=0x%08X\n", hrSet);
                        OutputDebugStringW(tmp);
                        LOG_INFO("[ForceSetOpacityFromUIA]", tmp);
                        pUi->Release();
                        pInspect->Release();
                        return SUCCEEDED(hrSet);
                    }
                    if (pInspect) pInspect->Release();
                }
            }
        }

        // 2b) ��� AccessibleObjectFromWindow ������ hwnd ���ڵ�ǰ���̣����Դ� hwnd ��ȡ������������ʽ��
        if (GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentProcessId()) {
            // ���Զ�ȡ GWLP_USERDATA / DWLP_USER �ȣ���Щ������ָ��������
            LONG_PTR userData = GetWindowLongPtr(hwnd, GWLP_USERDATA);
            swprintf_s(tmp, L"[ForceSetOpacityFromUIA] GetWindowLongPtr(GWLP_USERDATA) = 0x%p\n", (void*)userData);
            OutputDebugStringW(tmp);
            LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);

            // ��һ�ֳ����洢��GetProp(hwnd, L"XamlSource" or similar) ���� ����һЩ���� Atom ����
            const wchar_t* propNames[] = { L"XamlSource", L"DesktopWindowXamlSource", L"DesktopXamlSource", L"XamlIsland" };
            for (auto name : propNames) {
                HANDLE hProp = GetProp(hwnd, name);
                swprintf_s(tmp, L"[ForceSetOpacityFromUIA] GetProp(%s) = 0x%p\n", name, hProp);
                OutputDebugStringW(tmp);
                LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);
                if (hProp) {
                    // ����Ǹ�ָ�룬���԰������� IUnknown ָ��ʹ�ã��ǳ�Σ�գ�������
                    IUnknown* pTry = reinterpret_cast<IUnknown*>(hProp);
                    if (pTry) {
                        // ���� QI IInspectable
                        IInspectable* pInspect = nullptr;
                        HRESULT hrTry = pTry->QueryInterface(__uuidof(IInspectable), (void**)&pInspect);
                        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] try GetProp->QI(IInspectable) hr=0x%08X pInspect=%p\n", hrTry, (void*)pInspect);
                        OutputDebugStringW(tmp);
                        LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);
                        if (SUCCEEDED(hrTry) && pInspect) {
                            ABI::Windows::UI::Xaml::IUIElement* pUi = nullptr;
                            HRESULT hrQi = pInspect->QueryInterface(__uuidof(ABI::Windows::UI::Xaml::IUIElement), (void**)&pUi);
                            if (SUCCEEDED(hrQi) && pUi) {
                                HRESULT hrSet = pUi->put_Opacity(newOpacity);
                                swprintf_s(tmp, L"[ForceSetOpacityFromUIA] put_Opacity via GetProp succeeded hr=0x%08X\n", hrSet);
                                OutputDebugStringW(tmp);
                                LOG_INFO("[ForceSetOpacityFromUIA]", tmp);
                                pUi->Release();
                                pInspect->Release();
                                return SUCCEEDED(hrSet);
                            }
                            if (pInspect) pInspect->Release();
                        }
                    }
                }
            }
        }
    } // end if hwnd

    // 3) ���û�� HWND �����Ϸ���ʧ�ܣ�����ͨ�� UIA �� RuntimeId / AutomationId ��ȡ������Ϣ������־��������������ֶ�ƥ��
    // ��ȡ runtime class name
    BSTR bstrName = nullptr;
    hr = target->get_CurrentName(&bstrName);
    if (SUCCEEDED(hr) && bstrName) {
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] UIA Name: %s\n", bstrName);
        OutputDebugStringW(tmp);
        LOG_INFO("[ForceSetOpacityFromUIA]", tmp);
        SysFreeString(bstrName);
    }

    // ��� ControlType / AutomationId / RuntimeId ����Ϊ����
    VARIANT varAutoId; VariantInit(&varAutoId);
    hr = target->GetCurrentPropertyValue(UIA_AutomationIdPropertyId, &varAutoId);
    if (SUCCEEDED(hr) && varAutoId.vt == VT_BSTR && varAutoId.bstrVal) {
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] AutomationId: %s\n", varAutoId.bstrVal);
        OutputDebugStringW(tmp);
        LOG_INFO("[ForceSetOpacityFromUIA]", tmp);
    }
    VariantClear(&varAutoId);

    // ����ԣ�ö����Ŀ��Ԫ����ͬ�Ķ��� DesktopChildSiteBridge ���ڣ��������Ӵ����������Բ�����ֻ�Ǽ�¼��Ϣ��
    HWND top = nullptr;
    if (hwnd) {
        // �ҵ����ͬ�ര��
        top = GetAncestor(hwnd, GA_ROOT);
        swprintf_s(tmp, L"[ForceSetOpacityFromUIA] topAncestor = 0x%p\n", top);
        OutputDebugStringW(tmp);
        LOG_DEBUG("[ForceSetOpacityFromUIA]", tmp);
    }

    OutputDebugStringW(L"[ForceSetOpacityFromUIA] all automated attempts failed; need in-process XAML diagnostics or WinAppSDK runtime\n");
    LOG_ERROR("[ForceSetOpacityFromUIA]", L"all attempts failed; need IXamlDiagnostics2 or in-process XAML object");

    return false;
}

HWND WINAPI HookManager::HookedCreateWindowExW(
    DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
    DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    HWND hWnd = OriginalCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle,
        X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (!hWnd) {
        LOG_ERROR("[HookManager.cpp][HookedCreateWindowExW]", "Failed to create window, error: ", GetLastError());
        return hWnd;
    }
    std::wstring className;
    if (hWnd) {
        className = GetWindowClassName(hWnd);
    }
    // ����������� Microsoft.UI.Content.DesktopChildSiteBridge��ִ�� TAPSite::Install
    if (className == L"Microsoft.UI.Content.DesktopChildSiteBridge" || className == L"Microsoft.UI.Content.DesktopWindowHost") {
        // �� hWnd ��Ϊ�̲߳������룬InstallUdk ��ȴ������� XAML ���������� attach
        if (!is)
        {
            wil::unique_handle handle(CreateThread(nullptr, 0, TAPSite::InstallUdk, nullptr, 0, nullptr));
            if (!handle) {
                LOG_ERROR("[HookManager.cpp][HookedCreateWindowExW]", "����TAPSite�߳�ʧ��\n");
            }
            else {
                LOG_INFO("[HookManager.cpp][HookedCreateWindowExW]", "Detected DesktopChildSiteBridge, spawn install thread for hwnd ", (uintptr_t)hWnd);
            }
            is = true;
        }
        wil::unique_handle handle(CreateThread(nullptr, 0, TAPSite::InstallUdk, (LPVOID)hWnd, 0, nullptr));
        if (!handle) {
            LOG_ERROR("[HookManager.cpp][HookedCreateWindowExW]", "����TAPSite�߳�ʧ��\n");
        }
        else {
            LOG_INFO("[HookManager.cpp][HookedCreateWindowExW]", "Detected DesktopChildSiteBridge, spawn install thread for hwnd ", (uintptr_t)hWnd);
        }
    }

    // �޸�Blur��Edit��Alpha
    if (IsDUIThread()) {
        if (ConvertTolower(className) == L"edit")
        {
            SetWindowLongW(hWnd, GWL_EXSTYLE, GetWindowLongW(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
        }
    }
    // explorer window
    if (className == L"DirectUIHWND" && GetWindowClassName(hWndParent) == L"SHELLDLL_DefView")
    {
        // �������Ҹ���
        HWND parent = GetParent(hWndParent);
        if (GetWindowClassName(parent) == L"ShellTabWindowClass")
        {
            parent = GetParent(parent);
            {
                std::lock_guard<std::mutex> lock(effectMutex);
                windowEffects.emplace(parent, WindowEffect(parent));
            }

            // ����Blur
            SetWindowBlur(parent);

            // 22H2
            DWORD build = WindowsVersion::GetBuildNumber();
            if (build >= 22500)
            {
                SetWindowSubclass(parent, HookManager::WndSubProc, 0, (DWORD_PTR)0);
            }
            //win 10
            else if (build < 22000)
            {
                SetWindowSubclass(parent, HookManager::WndSubProc, 0, (DWORD_PTR)1);
            }
            else {
                SetWindowSubclass(parent, HookManager::WndSubProc, 0, (DWORD_PTR)3);
            }

            // ��¼���б��� Add to list
            DWORD tid = GetCurrentThreadId();

            DUIData data;
            data.hWnd = hWnd;
            data.mainWnd = parent;
            m_DUIList[tid] = data;
        }
        LOG_INFO("[HookManager.cpp][HookedCreateWindowExW]", "Explorer window detected, setting blur");
    }
    else if (className == L"SysTreeView32")
    {
        HWND parent = GetParent(hWndParent);  // NamespaceTreeControl
        parent = GetParent(parent);          // CtrlNotifySink
        parent = GetParent(parent);          // DirectUIHWND
        SetWindowBlur(parent);
        parent = GetParent(parent);          // DUIViewWndClassName
        std::wstring mainWndCls = GetWindowClassName(parent);

        if (mainWndCls == L"ShellTabWindowClass")
        {
            m_DUIList[GetCurrentThreadId()].TreeWnd = hWnd;
            {
                std::lock_guard<std::mutex> lock(effectMutex);
                if (windowEffects.find(parent) == windowEffects.end()) {
                    windowEffects.emplace(parent, WindowEffect(parent));
                    SetWindowSubclass(parent, HookManager::WndSubProc, 0, (DWORD_PTR)0);
                }
            }
            /*
            std::thread([]() {
                // �����߳��г�ʼ�� COM
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                /*HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(hr)) {
                    LOG_ERROR("[XamlTreeScanner]", L"CoInitializeEx failed: " + std::to_wstring(hr));
                    return;
                }
                ComUninitGuard comGuard;

                LOG_INFO("[XamlTreeScanner]", L"Waiting for UI to load...");
                //std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                efftype
                try {
                    LOG_INFO("[XamlTreeScanner]", L"Initializing scanner...");
                    XamlTreeScanner scanner;
                    if (!scanner.GetAutomation()) {
                        LOG_ERROR("[XamlTreeScanner]", L"UIAutomation ��ʼ��ʧ��");
                        return;
                    }

                    // ɨ������Ԫ��
                    scanner.ScanAllElements();
                    auto elements = scanner.GetElements();
                    LOG_INFO("[XamlTreeScanner]", L"ɨ�赽Ԫ������: " + std::to_wstring(elements.size()));
                    if (elements.empty()) {
                        LOG_ERROR("[XamlTreeScanner]", L"û��ɨ�赽�κ�Ԫ�أ��˳�");
                        return;
                    }

                    // ====================== ���ҡ�����Ԫ�ؼ��丸�� ======================
                    LOG_INFO("[XamlTreeScanner]", L"Finding '����' element and its parents...");
                    CComPtr<IUIAutomationTreeWalker> walker;
                    scanner.GetAutomation()->get_ControlViewWalker(&walker);
                    if (!walker) {
                        LOG_ERROR("[XamlTreeScanner]", L"ControlViewWalker ��ȡʧ��");
                        return;
                    }

                    CComPtr<IUIAutomationElement> targetCommandBar;


                    std::vector<CComPtr<IUIAutomationElement>> targets;
                    for (auto& e : elements) {
                        if (scanner.GetElementName(e) == L"����") {
                            targets.push_back(e);
                        }
                    }

                    if (targets.empty()) {
                        LOG_WARN("[XamlTreeScanner]", L"No '����' elements found");
                    }
                    else {
                        for (auto& te : targets) {
                            // �ռ�����
                            std::vector<CComPtr<IUIAutomationElement>> chain;
                            CComPtr<IUIAutomationElement> cur = te;
                            while (cur) {
                                chain.push_back(cur);
                                CComPtr<IUIAutomationElement> parent;
                                if (FAILED(walker->GetParentElement(cur, &parent)) || !parent) break;
                                cur = parent;
                            }
                            std::reverse(chain.begin(), chain.end());

                            // ���
                            std::wstring addr = L"Element@" + std::to_wstring(reinterpret_cast<uintptr_t>(te.p));
                            LOG_INFO("[XamlTreeScanner]", L"----- Parent Chain for '����' (" + addr + L") -----");
                            int lvl = 0;
                            for (auto& node : chain) {
                                std::wstring name = scanner.GetElementName(node);
                                if (name.empty()) name = L"(No Name)";
                                CONTROLTYPEID ct; node->get_CurrentControlType(&ct);
                                std::wstring type = L"ControlType" + std::to_wstring(ct);
                                switch (ct) {
                                case UIA_WindowControlTypeId: type = L"Window"; break;
                                case UIA_PaneControlTypeId:   type = L"Pane";   break;
                                case UIA_ButtonControlTypeId: type = L"Button"; break;
                                case UIA_TextControlTypeId:   type = L"Text";   break;
                                }
                                CComBSTR aid; node->get_CurrentAutomationId(&aid);
                                std::wstring id = (aid && aid.Length()) ? std::wstring(aid, SysStringLen(aid)) : L"";

                                // ===== ������ʶ��Ŀ��Ԫ�� =====
                                if (ct == 50040 && id == L"FileExplorerCommandBar") {
                                    targetCommandBar = node; // ����Ŀ��Ԫ��
                                    LOG_INFO("[XamlTreeScanner]", L"�ҵ�Ŀ��Ԫ��: FileExplorerCommandBar");
                                }

                                std::wstring indent(lvl * 2, L' ');
                                std::wstring log = indent + L"[" + type + L"] " + name;
                                if (!id.empty()) log += L" (ID: " + id + L")";
                                LOG_INFO("[XamlTreeScanner]", log.c_str());
                                lvl++;
                            }
                            LOG_INFO("[XamlTreeScanner]", L"----------------------------------------");
                        }
                    }

                    if (targetCommandBar) {
                        LOG_INFO("[XamlTreeScanner]", L"�����޸�FileExplorerCommandBar����͸���ȡ�");
                        ForceSetOpacityFromUIA(targetCommandBar, 0.0);
                    }
                    else {
                        LOG_WARN("[XamlTreeScanner]", L"δ�ҵ�FileExplorerCommandBarԪ��");
                    }
                }
                catch (const std::exception& e) {
                    std::string err = "Exception: "; err += e.what();
                    LOG_ERROR("[XamlTreeScanner]", std::wstring(err.begin(), err.end()));
                }
                catch (winrt::hresult_error const& e) {
                    std::wstring msg = e.message().c_str();
                    std::wstringstream ss;
                    ss << L"WinRT �쳣: " << msg << L" (HRESULT: 0x" << std::hex << static_cast<uint32_t>(e.code()) << L")";
                    LOG_ERROR("[XamlTreeScanner]", ss.str().c_str());
                }
                catch (...) {
                    LOG_ERROR("[XamlTreeScanner]", L"Unknown exception in scanning thread");
                }
                }).detach();*/
        }
    }
    return hWnd;
}

HWND HookManager::FindExplorerMainWindow(HWND hChild) {
    HWND parent = hChild;
    HWND lastParent = hChild;

    // ���ϱ������ڲ㼶
    while (parent) {
        lastParent = parent;
        parent = GetParent(parent);

        wchar_t className[256];
        if (GetClassNameW(lastParent, className, ARRAYSIZE(className))) {
            if (wcscmp(className, L"ShellTabWindowClass") == 0 ||
                wcscmp(className, L"ExplorerWClass") == 0 ||
                wcscmp(className, L"CabinetWClass") == 0)
            {
                return lastParent;
            }
        }
    }
    return nullptr;
}

std::wstring HookManager::GetWindowClassName(HWND hWnd)
{
    wchar_t className[256] = { 0 };
    GetClassNameW(hWnd, className, ARRAYSIZE(className));
    return className;
}

bool HookManager::AlphaBuffer(HDC hdc, LPRECT pRc, std::function<void(HDC)> fun)
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    BP_PAINTPARAMS bpParam;
    bpParam.cbSize = sizeof(BP_PAINTPARAMS);
    bpParam.dwFlags = BPPF_ERASE;
    bpParam.prcExclude = nullptr;
    bpParam.pBlendFunction = &bf;
    HDC hDC = 0;
    HPAINTBUFFER pbuffer = BeginBufferedPaint(hdc, pRc, BPBF_TOPDOWNDIB, &bpParam, &hDC);
    if (pbuffer && hDC && fun)
    {
        //����ԭDC��Ϣ
        SelectObject(hDC, GetCurrentObject(hdc, OBJ_FONT));
        SetBkMode(hDC, GetBkMode(hdc));
        SetBkColor(hDC, GetBkColor(hdc));
        SetTextAlign(hDC, GetTextAlign(hdc));
        SetTextCharacterExtra(hDC, GetTextCharacterExtra(hdc));
        fun(hDC);
        EndBufferedPaint(pbuffer, TRUE);

        return true;
    }
    return false;
}

BOOL WINAPI HookManager::HookedDestroyWindow(HWND hWnd) {
    {
        //std::lock_guard<std::mutex> lock(effectMutex);
        auto it = windowEffects.find(hWnd);
        if (it != windowEffects.end()) {
            LOG_DEBUG("[HookManager.cpp][HookedDestroyWindow]", "Removing window effect for: ", hWnd);
            windowEffects.erase(it);
        }

        auto iter = m_DUIList.find(GetCurrentThreadId());
        if (iter != m_DUIList.end())
        {
            if (iter->second.hWnd == hWnd) {
                m_DUIList.erase(iter);
            }
        }
    }
    BOOL result = OriginalDestroyWindow(hWnd);
    if (!result) {
        LOG_ERROR("[HookManager.cpp][HookedDestroyWindow]", "DestroyWindow failed, error: ", GetLastError());
    }

    return result;
}

HDC WINAPI HookManager::HookedBeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) {
    HDC hDC = OriginalBeginPaint(hWnd, lpPaint);
    auto iter = m_DUIList.find(GetCurrentThreadId());
    if (iter != m_DUIList.end()) {
        if (iter->second.hWnd == hWnd) {
            iter->second.srcDC = hDC;
            iter->second.hDC = hDC;
            iter->second.treeDraw = false;
        }
        else if (iter->second.TreeWnd == hWnd) {
            iter->second.srcDC = hDC;
            iter->second.hDC = hDC;
            iter->second.treeDraw = true;
        }
    }

    //HBRUSH hTransparentBrush = CreateSolidBrush(0x00000000);
    //FillRect(hDC, &lpPaint->rcPaint, lpPaint);
    //DeleteObject(hTransparentBrush);
    return hDC;
}

BOOL WINAPI HookManager::HookedEndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint) {
    DWORD curThread = GetCurrentThreadId();
    auto iter = m_DUIList.find(curThread);

    if (iter != m_DUIList.end()) {
        if (iter->second.hWnd == hWnd
            || iter->second.TreeWnd == hWnd)
        {
            iter->second.srcDC = 0;
            iter->second.hDC = 0;
            iter->second.treeDraw = false;
        }
    }

    auto ribiter = m_ribbonPaint.find(curThread);
    if (ribiter != m_ribbonPaint.end())
        m_ribbonPaint.erase(ribiter);

    BOOL ret = OriginalEndPaint(hWnd, lpPaint);
    return ret;
}

UINT HookManager::CalcRibbonHeightForDPI(HWND hWnd, UINT src, bool normal, bool offsets)
{
    static auto GetWindowDPI = [](HWND hwnd) -> UINT
        {
            typedef UINT(WINAPI* pfnGetWindowDPI)(HWND hwnd);
            static const auto& GetWindowDPI = (pfnGetWindowDPI)GetProcAddress(GetModuleHandleW(L"User32"), MAKEINTRESOURCEA(2707));
            if (GetWindowDPI)
            {
                return GetWindowDPI(hwnd);
            }
            else
            {
                HDC hdc = GetDC(0);
                int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
                ReleaseDC(0, hdc);
                return (UINT)dpi;
            }
        };

    static auto MsoScaleForWindowDPI = [](HWND hwnd)
        {
            return fmaxf(1.0, (float)GetWindowDPI(hwnd) / 96.f);
        };

    float scale = MsoScaleForWindowDPI(hWnd);
    if (scale != 1.f) {
        if (!normal) {
            float offset = round(1.5f * scale);
            float height = round((float)src * scale);
            if (offsets)
                height -= offset;
            else if (scale == 2.f)
                height += 2;
            else if (scale < 1.7)
                height -= 1;

            return (UINT)height;
        }
        else
        {
            return (UINT)round(scale * (float)src);
        }
    }
    return src;
}

bool HookManager::CompareColor(COLORREF color1, COLORREF color2)
{
    return GetRValue(color1) == GetRValue(color2)
        && GetGValue(color1) == GetGValue(color2)
        && GetBValue(color1) == GetBValue(color2);
}

int WINAPI HookManager::HookedFillRect(HDC hDC, const RECT* lprc, HBRUSH hbr) {
    int ret = S_OK;
    DWORD curThread = GetCurrentThreadId();
    bool backgroundRendered = false; // ����������Ƿ�����Ⱦ����

    // ����Ƿ���Ҫ��ȾͼƬ����
    if (!HookManager::m_config.imagePath.empty()) {
        auto iter = m_DUIList.find(curThread);
        if (iter != m_DUIList.end() && iter->second.hDC == hDC) {
            // ��������ͼƬ�������ظ����أ�
            static std::unordered_map<std::wstring, BitmapCache> s_imageCache;
            auto& cache = s_imageCache[HookManager::m_config.imagePath];

            // �״μ��ػ�ͼƬ·���仯ʱ���¼���
            if (!cache.hBitmap || cache.path != HookManager::m_config.imagePath) {
                // �ͷž���Դ
                if (cache.hBitmap) {
                    DeleteObject(cache.hBitmap);
                    cache.hBitmap = nullptr;
                }

                // ʹ��GDI+���������ʽͼƬ
                Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromFile(
                    HookManager::m_config.imagePath.c_str()
                );

                if (pBitmap && pBitmap->GetLastStatus() == Gdiplus::Ok) {
                    pBitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &cache.hBitmap);
                    cache.path = HookManager::m_config.imagePath;
                    cache.width = pBitmap->GetWidth();
                    cache.height = pBitmap->GetHeight();
                }
                delete pBitmap;
            }

            // �ɹ�����ͼƬ����Ⱦ
            if (cache.hBitmap) {
                // ֻ����һ��ԭʼ�����Ϊ����
                ret = OriginalFillRect(hDC, lprc, hbr);

                HDC hMemDC = CreateCompatibleDC(hDC);
                HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, cache.hBitmap);

                // �������λ�úͳߴ磨���У�
                int drawX = (iter->second.width - cache.width) / 2;
                int drawY = (iter->second.height - cache.height) / 2;

                // ����͸���Ȼ��
                BLENDFUNCTION bf = {
                    AC_SRC_OVER,
                    0,
                    static_cast<BYTE>(HookManager::m_config.imageOpacity * 255),
                    AC_SRC_ALPHA
                };
                // �滻ԭ�еĻ��ƴ���
                RECT rcClient;
                GetClientRect(iter->second.hWnd, &rcClient);  // ��ȡʵ�ʿͻ�����С

                // ��ȾͼƬ
                OriginalAlphaBlend(
                    hDC,
                    rcClient.left, rcClient.top,   // �ӿͻ������Ͻǿ�ʼ
                    rcClient.right - rcClient.left, // ʹ�ÿͻ������
                    rcClient.bottom - rcClient.top, // ʹ�ÿͻ����߶�
                    hMemDC,
                    0, 0,
                    cache.width,
                    cache.height,
                    bf
                );

                // ������Դ
                SelectObject(hMemDC, hOldBmp);
                DeleteDC(hMemDC);

                backgroundRendered = true; // �������Ⱦ

                // ֱ�������ﴦ������߼�
                if (iter->second.refresh) {
                    SendMessageW(iter->second.hWnd, WM_THEMECHANGED, 0, 0);

                    DWORD build = WindowsVersion::GetBuildNumber();
                    if (build >= 22600) {
                        SetWindowBlur(iter->second.mainWnd);
                    }

                    iter->second.refresh = false;
                    InvalidateRect(iter->second.hWnd, nullptr, TRUE);
                }

                COLORREF color = RGB(0, 0, 0);
                if (!CompareColor(TreeView_GetBkColor(iter->second.TreeWnd), color)) {
                    TreeView_SetBkColor(iter->second.TreeWnd, color);
                }

                return ret; // ��ǰ���أ������������
            }
        }
    }

    // δ��Ⱦ����ʱ��ִ�������߼�
    if (!backgroundRendered) {
        auto iter = m_DUIList.find(curThread);
        if (iter != m_DUIList.end()) {
            if (iter->second.hDC == hDC) {
                ret = OriginalFillRect(hDC, lprc, hbr);
                if (iter->second.refresh) {
                    SendMessageW(iter->second.hWnd, WM_THEMECHANGED, 0, 0);

                    DWORD build = WindowsVersion::GetBuildNumber();
                    if (build >= 22600) {
                        SetWindowBlur(iter->second.mainWnd);
                    }

                    iter->second.refresh = false;
                    InvalidateRect(iter->second.hWnd, nullptr, TRUE);
                }

                COLORREF color = RGB(0, 0, 0);
                if (!CompareColor(TreeView_GetBkColor(iter->second.TreeWnd), color)) {
                    TreeView_SetBkColor(iter->second.TreeWnd, color);
                }
                return ret;
            }
        }

        auto ribiter = m_ribbonPaint.find(curThread);
        if (ribiter != m_ribbonPaint.end()) {
            if (ribiter->second.second == hDC) {
                RECT rcWnd;
                GetWindowRect(ribiter->second.first, &rcWnd);

                if (lprc->bottom == CalcRibbonHeightForDPI(ribiter->second.first, 26, false)
                    || lprc->bottom == CalcRibbonHeightForDPI(ribiter->second.first, 23, false, false)
                    || lprc->bottom == 1
                    || (lprc->bottom - lprc->top == CalcRibbonHeightForDPI(ribiter->second.first, 1)
                        && (rcWnd.right - rcWnd.left) == lprc->right))
                {
                    hbr = (HBRUSH)GetStockObject(BLACK_BRUSH);
                }
                return OriginalFillRect(hDC, lprc, hbr);
            }
        }
    }

    return OriginalFillRect(hDC, lprc, hbr);
}

HRESULT WINAPI HookManager::HookedDwmSetWindowAttribute(
    HWND hwnd,
    DWORD dwAttribute,
    _In_reads_bytes_(cbAttribute) LPCVOID pvAttribute,
    DWORD cbAttribute)
{
    DWORD pid = 0;
    DWORD tid = GetCurrentThreadId();
    GetWindowThreadProcessId(hwnd, &pid);

    wchar_t className[256] = { 0 };
    wchar_t windowTitle[256] = { 0 };
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    GetWindowTextW(hwnd, windowTitle, ARRAYSIZE(windowTitle));

    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstringstream ss;
    ss << L"HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd)
        << L", PID: " << std::dec << pid
        << L", TID: " << tid
        << L", Attribute: " << dwAttribute << L" (0x" << std::hex << dwAttribute << L")"
        << L", DataPtr: 0x" << reinterpret_cast<uintptr_t>(pvAttribute)
        << L", DataSize: " << std::dec << cbAttribute
        << L", Class: '" << className << L"'"
        << L", Title: '" << windowTitle << L"'"
        << L", Time: " << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"." << st.wMilliseconds;

    LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", ss.str().c_str());

    switch (dwAttribute) {
    case 2: // DWMWA_NCRENDERING_ENABLED
        LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", "  DWMWA_NCRENDERING_ENABLED: Controls non-client rendering");
        break;
    case 3: // DWMWA_NCRENDERING_POLICY
        if (pvAttribute && cbAttribute >= sizeof(DWORD)) {
            DWORD policy = *static_cast<const DWORD*>(pvAttribute);
            LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", L"  DWMWA_NCRENDERING_POLICY: " , policy);
        }
        break;
    case 4: // DWMWA_TRANSITIONS_FORCEDISABLED
        LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", "  DWMWA_TRANSITIONS_FORCEDISABLED");
        break;
    case 5: // DWMWA_ALLOW_NCPAINT
        LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", "  DWMWA_ALLOW_NCPAINT");
        break;
    case 19: // DWMWA_USE_IMMERSIVE_DARK_MODE
        if (pvAttribute && cbAttribute >= sizeof(BOOL)) {
            BOOL darkMode = *static_cast<const BOOL*>(pvAttribute);
            LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", L"  DWMWA_USE_IMMERSIVE_DARK_MODE: " , (darkMode ? "Yes" : "No"));
        }
        break;
    case 20: // DWMWA_WINDOW_CORNER_PREFERENCE
        if (pvAttribute && cbAttribute >= sizeof(DWORD)) {
            DWORD cornerPref = *static_cast<const DWORD*>(pvAttribute);
            LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", L"  DWMWA_WINDOW_CORNER_PREFERENCE: " , cornerPref);
        }
        break;
    case 38: // DWMWA_SYSTEMBACKDROP_TYPE (Windows 11+)
        if (pvAttribute && cbAttribute >= sizeof(DWORD)) {
            DWORD backdropType = *static_cast<const DWORD*>(pvAttribute);
            LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", L"  DWMWA_SYSTEMBACKDROP_TYPE: " , backdropType);
        }
        break;
    default:
        LOG_DEBUG("[HookManager.cpp][HookedDwmSetWindowAttribute]", L"  Unknown attribute: " , dwAttribute);
        break;
    }
    return S_OK;
}