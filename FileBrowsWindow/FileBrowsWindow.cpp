#define NOMINMAX  // 必须在包含windows.h之前定义
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <psapi.h>
#include <algorithm> 

// 添加必要的命名空间声明
using std::wstring;
using std::wistringstream;
using std::wcerr;
using std::wcout;
using std::wcin;
using std::endl;


// 错误处理宏
#define ERROR_MSG(msg) std::cerr << "Error: " << msg << " (Error code: " << GetLastError() << ")" << std::endl

// 配置结构（与DLL中的定义一致）
#pragma pack(push, 1) // 确保内存对齐
struct Remote_Config {
    int effType = 0;
    COLORREF blendColor = 0;
    bool automatic_acquisition_color = false;
    int automatic_acquisition_color_transparency = -1;
    wchar_t imagePath[256] = { 0 }; // 使用固定长度字符数组
    float imageOpacity = 0.8f;
    int imageBlurRadius = 5;
    bool smallborder = true;

    bool enabled = true;

    int NotificationLevel = 0; // 新添加的字段
};
#pragma pack(pop)

// 获取进程ID
DWORD GetProcessIdByName(const wchar_t* processName) {
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        ERROR_MSG("CreateToolhelp32Snapshot failed");
        return 0;
    }

    if (!Process32FirstW(hSnapshot, &pe32)) {
        ERROR_MSG("Process32First failed");
        CloseHandle(hSnapshot);
        return 0;
    }

    do {
        if (_wcsicmp(pe32.szExeFile, processName) == 0) {
            CloseHandle(hSnapshot);
            return pe32.th32ProcessID;
        }
    } while (Process32NextW(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return 0;
}

// 检查模块是否已加载
bool IsDllLoaded(DWORD processId, const wchar_t* dllName) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess == NULL) {
        return false;
    }

    HMODULE hModules[1024];
    DWORD cbNeeded;
    bool found = false;

    if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModName[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, hModules[i], szModName, MAX_PATH)) {
                if (wcsstr(szModName, dllName) != nullptr) {
                    found = true;
                    break;
                }
            }
        }
    }

    CloseHandle(hProcess);
    return found;
}

// 注入DLL到目标进程
bool InjectDll(DWORD processId, const wchar_t* dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess == NULL) {
        ERROR_MSG("OpenProcess failed");
        return false;
    }

    // 在目标进程中分配内存
    LPVOID pRemoteMemory = VirtualAllocEx(hProcess, NULL, (wcslen(dllPath) + 1) * sizeof(wchar_t), MEM_COMMIT, PAGE_READWRITE);
    if (pRemoteMemory == NULL) {
        ERROR_MSG("VirtualAllocEx failed");
        CloseHandle(hProcess);
        return false;
    }

    // 写入DLL路径到目标进程
    if (!WriteProcessMemory(hProcess, pRemoteMemory, dllPath, (wcslen(dllPath) + 1) * sizeof(wchar_t), NULL)) {
        ERROR_MSG("WriteProcessMemory failed");
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 获取LoadLibraryW函数地址
    LPTHREAD_START_ROUTINE pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (pLoadLibrary == NULL) {
        ERROR_MSG("GetProcAddress failed");
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 在目标进程中创建远程线程
    HANDLE hRemoteThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pRemoteMemory, 0, NULL);
    if (hRemoteThread == NULL) {
        ERROR_MSG("CreateRemoteThread failed");
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 等待线程结束
    WaitForSingleObject(hRemoteThread, INFINITE);

    // 清理
    VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
    CloseHandle(hRemoteThread);
    CloseHandle(hProcess);

    return true;
}

// 调用DLL中的导出函数
bool CallRemoteSetConfig(DWORD processId, HMODULE hDllModule, const Remote_Config* pConfig) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess == NULL) {
        ERROR_MSG("OpenProcess failed for remote call");
        return false;
    }

    // 获取导出函数地址
    FARPROC pSetRemoteConfig = GetProcAddress(hDllModule, "?SetRemote_Config@@YAXPEAURemote_Config@@@Z");
    if (pSetRemoteConfig == NULL) {
        ERROR_MSG("GetProcAddress for SetRemote_Config failed");
        CloseHandle(hProcess);
        return false;
    }

    // 在目标进程中分配内存用于配置结构
    LPVOID pRemoteConfig = VirtualAllocEx(hProcess, NULL, sizeof(Remote_Config), MEM_COMMIT, PAGE_READWRITE);
    if (pRemoteConfig == NULL) {
        ERROR_MSG("VirtualAllocEx for config failed");
        CloseHandle(hProcess);
        return false;
    }

    // 写入配置到目标进程
    if (!WriteProcessMemory(hProcess, pRemoteConfig, pConfig, sizeof(Remote_Config), NULL)) {
        ERROR_MSG("WriteProcessMemory for config failed");
        VirtualFreeEx(hProcess, pRemoteConfig, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 创建远程线程调用导出函数
    HANDLE hRemoteThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pSetRemoteConfig,
        pRemoteConfig, 0, NULL);

    if (hRemoteThread == NULL) {
        ERROR_MSG("CreateRemoteThread for SetRemote_Config failed");
        VirtualFreeEx(hProcess, pRemoteConfig, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 等待函数执行完成
    WaitForSingleObject(hRemoteThread, INFINITE);

    // 检查线程退出码
    DWORD exitCode;
    GetExitCodeThread(hRemoteThread, &exitCode);
    //if (exitCode != 0) {
    //    std::wcerr << L"SetRemote_Config returned error: " << exitCode << std::endl;
    //}

    // 清理
    CloseHandle(hRemoteThread);
    VirtualFreeEx(hProcess, pRemoteConfig, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return exitCode == 0;
}

// 获取DLL在目标进程中的模块句柄
HMODULE GetRemoteModuleHandle(DWORD processId, const wchar_t* dllName) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess == NULL) {
        return NULL;
    }

    HMODULE hModules[1024];
    DWORD cbNeeded;
    HMODULE hTargetModule = NULL;

    if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModName[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, hModules[i], szModName, MAX_PATH)) {
                if (wcsstr(szModName, dllName) != nullptr) {
                    hTargetModule = hModules[i];
                    break;
                }
            }
        }
    }

    CloseHandle(hProcess);
    return hTargetModule;
}

// 辅助：去除字符串首尾空白
static inline std::wstring TrimW(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) ++start;
    size_t end = s.size();
    while (end > start && iswspace(s[end - 1])) --end;
    return s.substr(start, end - start);
}

Remote_Config ParseCommandLine(int argc, wchar_t* argv[]) {
    Remote_Config config;
    bool explicitEnableSet = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::wstring key, value;

        // 分割键值对
        size_t pos = arg.find(L'=');
        if (pos != std::wstring::npos) {
            key = arg.substr(0, pos);
            value = arg.substr(pos + 1);

            // 转换为小写以便不区分大小写比较
            std::transform(key.begin(), key.end(), key.begin(), ::towlower);

            if (key == L"--efftype") {
                config.effType = _wtoi(value.c_str());
            }
            else if (key == L"--blendcolor") {
                std::wstring v = TrimW(value);
                if (v.empty()) {
                    wcerr << L"Invalid blendColor format (empty). Using default." << endl;
                    continue;
                }

                // 支持十六进制形式：#RRGGBB 或 #AARRGGBB 或 0xRRGGBB / 0xAARRGGBB
                bool parsed = false;
                if (v[0] == L'#' || (v.size() > 2 && (v[0] == L'0' && (v[1] == L'x' || v[1] == L'X')))) {
                    std::wstring hex = (v[0] == L'#') ? v.substr(1) : v.substr(2);
                    // 去掉可能的前导空白或0x前缀之外的空白
                    hex = TrimW(hex);

                    // 仅接受长度为6或8的十六进制字符串
                    if (hex.length() == 6 || hex.length() == 8) {
                        // 使用宽字符的无符号长整型解析
                        wchar_t* endptr = nullptr;
                        unsigned long val = wcstoul(hex.c_str(), &endptr, 16);
                        if (endptr != hex.c_str()) {
                            if (hex.length() == 6) {
                                int r = (val >> 16) & 0xFF;
                                int g = (val >> 8) & 0xFF;
                                int b = val & 0xFF;
                                config.blendColor = RGB(r, g, b);
                                parsed = true;
                            } else { // 8 digits: AARRGGBB
                                int a = (val >> 24) & 0xFF;
                                int r = (val >> 16) & 0xFF;
                                int g = (val >> 8) & 0xFF;
                                int b = val & 0xFF;
                                config.blendColor = RGB(r, g, b);
                                // 将 0-255 的 alpha 转换为 0-100 的百分比（四舍五入）
                                config.automatic_acquisition_color_transparency = static_cast<int>((a * 100 + 127) / 255);
                                parsed = true;
                            }
                        }
                    }
                    if (!parsed) {
                        wcerr << L"Invalid hex blendColor format. Expected #RRGGBB or #AARRGGBB or 0x... . Using default." << endl;
                    }
                } else {
                    // 支持 r,g,b[,a] 逗号分隔格式
                    std::wistringstream iss(v);
                    std::wstring token;
                    int components[4] = { 0, 0, 0, -1 };
                    int count = 0;
                    while (std::getline(iss, token, L',') && count < 4) {
                        token = TrimW(token);
                        if (token.empty()) break;
                        wchar_t* endptr = nullptr;
                        long val = wcstol(token.c_str(), &endptr, 10);
                        if (endptr == token.c_str()) {
                            // 解析失败
                            break;
                        }
                        components[count++] = static_cast<int>(val);
                    }

                    if (count >= 3) {
                        int r = std::min(255, std::max(0, components[0]));
                        int g = std::min(255, std::max(0, components[1]));
                        int b = std::min(255, std::max(0, components[2]));
                        config.blendColor = RGB(r, g, b);
                        if (count >= 4) {
                            int t = components[3];
                            config.automatic_acquisition_color_transparency = std::min(100, std::max(-1, t));
                        }
                        parsed = true;
                    } else {
                        wcerr << L"Invalid blendColor format. Expected 'r,g,b[,a]'. Using default." << endl;
                    }
                }
            }
            else if (key == L"--automatic_acquisition_color") {
                config.automatic_acquisition_color =
                    (value == L"true" || value == L"1" || value == L"yes");
            }
            else if (key == L"--automatic_acquisition_color_transparency") {
                try {
                    int transparency = std::stoi(std::string(value.begin(), value.end()));
                    config.automatic_acquisition_color_transparency =
                        std::min(100, std::max(-1, transparency));
                }
                catch (...) {
                    wcerr << L"Invalid transparency value. Using default." << endl;
                }
            }
            else if (key == L"--imagepath") {
                // 复制到固定长度数组
                wcsncpy_s(config.imagePath, value.c_str(), _TRUNCATE);
            }
            else if (key == L"--imageopacity") {
                config.imageOpacity = static_cast<float>(_wtof(value.c_str()));
            }
            else if (key == L"--imageblurradius") {
                config.imageBlurRadius = _wtoi(value.c_str());
            }
            else if (key == L"--enabled") {
                // 修复1：转换为小写确保大小写不敏感
                std::wstring valueLower = value;
                std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(), ::towlower);

                // 修复2：明确处理 true/false
                if (valueLower == L"false" || valueLower == L"0" || valueLower == L"no" || valueLower == L"disable") {
                    config.enabled = false;
                }
                else {
                    config.enabled = true;  // 任何非禁用值都视为true
                }
                explicitEnableSet = true; // 标记已显式设置
            }

            else if (key == L"--notificationlevel") {
                config.NotificationLevel = _wtoi(value.c_str());
            }
        }
    }

    return config;
}


int wmain(int argc, wchar_t* argv[]) {
    Remote_Config config;

    // 如果有参数则解析
    if (argc > 1) {
        config = ParseCommandLine(argc, argv);
    }
    else {
        // 默认配置
        config.enabled = true;
    }

    // 获取当前目录
    wchar_t currentDir[MAX_PATH];
    if (!GetCurrentDirectoryW(MAX_PATH, currentDir)) {
        ERROR_MSG("GetCurrentDirectory failed");
        return 1;
    }

    // 构建DLL完整路径
    wchar_t dllPath[MAX_PATH];
    if (swprintf_s(dllPath, MAX_PATH, L"%s\\FileBrowsWindowPatch.dll", currentDir) == -1) {
        ERROR_MSG("Constructing DLL path failed");
        return 1;
    }

    // 检查DLL文件是否存在
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        ERROR_MSG("DLL file not found");
        std::wcerr << L"Expected DLL path: " << dllPath << std::endl;
        return 1;
    }

    // 获取explorer.exe进程ID
    DWORD explorerPid = GetProcessIdByName(L"explorer.exe");
    if (explorerPid == 0) {
        ERROR_MSG("Could not find explorer.exe process");
        return 1;
    }

    std::wcout << L"Found explorer.exe with PID: " << explorerPid << std::endl;
    std::wcout << L"Attempting to inject: " << dllPath << std::endl;

    // 检查DLL是否已加载
    bool dllAlreadyLoaded = IsDllLoaded(explorerPid, L"FileBrowsWindowPatch.dll");

    if (!dllAlreadyLoaded) {
        // 注入DLL
        if (!InjectDll(explorerPid, dllPath)) {
            ERROR_MSG("Injection failed");
            return 1;
        }
        std::wcout << L"Injection successful!" << std::endl;

        // 等待DLL加载完成
        Sleep(1000);
    }
    else {
        std::wcout << L"DLL already loaded, skipping injection" << std::endl;
    }

    // 获取DLL在目标进程中的模块句柄
    HMODULE hLocalDll = LoadLibraryExW(dllPath, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (hLocalDll == NULL) {
        ERROR_MSG("Failed to load local DLL copy");
        return 1;
    }

    // 调用远程函数设置配置
    if (CallRemoteSetConfig(explorerPid, hLocalDll, &config)) {
        std::wcout << L"Configuration applied successfully!" << std::endl;
        FreeLibrary(hLocalDll);
        return 0;
    }
    else {
        ERROR_MSG("Failed to apply configuration");
        FreeLibrary(hLocalDll);
        return 1;
    }
}