// CrashMonitor.cpp - 现代化 UI 版本
#include "pch.h"

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Uxtheme.lib")
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tchar.h>
#include <atomic>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <wingdi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <csignal>
#include <Uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "ComDlg32.lib")

// 颜色定义 - 现代化配色方案
#define COLOR_BG_LIGHT RGB(250, 250, 250)
#define COLOR_BG_DARK RGB(32, 32, 32)
#define COLOR_CARD_LIGHT RGB(255, 255, 255)
#define COLOR_CARD_DARK RGB(43, 43, 43)
#define COLOR_TEXT_LIGHT RGB(30, 30, 30)
#define COLOR_TEXT_DARK RGB(240, 240, 240)
#define COLOR_ACCENT RGB(0, 120, 215)
#define COLOR_BORDER RGB(220, 220, 220)
#define COLOR_BORDER_DARK RGB(70, 70, 70)
#define COLOR_ERROR RGB(202, 80, 80)
#define COLOR_SUCCESS RGB(80, 160, 80)

// 控件尺寸和间距
#define MARGIN 20
#define PADDING 16
#define BUTTON_HEIGHT 32
#define BUTTON_WIDTH 140
#define SCROLLBAR_WIDTH 12

static int g_scrollOffset = 0;
static bool g_darkMode = false;

// --------------------------- 现代化 UI 辅助函数 ---------------------------
namespace ModernUI {
    void EnableDarkMode(HWND hWnd, bool enable) {
        g_darkMode = enable;

        // 设置窗口深色模式
        BOOL dark = enable ? TRUE : FALSE;
        DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));

        // 更新非客户区
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    HBRUSH GetBackgroundBrush() {
        static HBRUSH hBrushLight = CreateSolidBrush(COLOR_BG_LIGHT);
        static HBRUSH hBrushDark = CreateSolidBrush(COLOR_BG_DARK);
        return g_darkMode ? hBrushDark : hBrushLight;
    }

    HBRUSH GetCardBrush() {
        static HBRUSH hBrushLight = CreateSolidBrush(COLOR_CARD_LIGHT);
        static HBRUSH hBrushDark = CreateSolidBrush(COLOR_CARD_DARK);
        return g_darkMode ? hBrushDark : hBrushLight;
    }

    COLORREF GetTextColor() {
        return g_darkMode ? COLOR_TEXT_DARK : COLOR_TEXT_LIGHT;
    }

    COLORREF GetBorderColor() {
        return g_darkMode ? COLOR_BORDER_DARK : COLOR_BORDER;
    }

    void DrawRoundedRect(HDC hdc, RECT rect, int radius, HBRUSH brush, HPEN pen) {
        HRGN rgn = CreateRoundRectRgn(rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectClipRgn(hdc, rgn);

        FillRect(hdc, &rect, brush);
        if (pen) {
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
        }

        SelectClipRgn(hdc, NULL);
        DeleteObject(rgn);
    }

    void DrawModernButton(HDC hdc, RECT rect, const char* text, bool hover, bool pressed) {
        int radius = 6;
        HBRUSH bgBrush;
        COLORREF textColor;

        if (pressed) {
            bgBrush = CreateSolidBrush(RGB(
                GetRValue(COLOR_ACCENT) - 30,
                GetGValue(COLOR_ACCENT) - 30,
                GetBValue(COLOR_ACCENT) - 30
            ));
            textColor = RGB(255, 255, 255);
        }
        else if (hover) {
            bgBrush = CreateSolidBrush(RGB(
                GetRValue(COLOR_ACCENT) + 20,
                GetGValue(COLOR_ACCENT) + 20,
                GetBValue(COLOR_ACCENT) + 20
            ));
            textColor = RGB(255, 255, 255);
        }
        else {
            bgBrush = CreateSolidBrush(COLOR_ACCENT);
            textColor = RGB(255, 255, 255);
        }

        // 绘制圆角按钮
        DrawRoundedRect(hdc, rect, radius, bgBrush, CreatePen(PS_SOLID, 1, COLOR_ACCENT));

        // 绘制文本
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);

        HFONT hFont = CreateFont(14, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        DrawTextA(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        DeleteObject(bgBrush);
    }
}

// --------------------------- 简易 Logger（演示用） ---------------------------
class SimpleLogger {
public:
    static SimpleLogger& GetInstance() {
        static SimpleLogger inst;
        return inst;
    }
    void Initialize() { /* 可扩展 */ }
    void Fatal(const char* tag, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        std::lock_guard<std::mutex> lk(mutex_);
        std::cerr << "[FATAL][" << tag << "] " << buf << std::endl;
    }
    void Info(const char* tag, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        char buf[1024]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
        std::lock_guard<std::mutex> lk(mutex_);
        std::cerr << "[INFO][" << tag << "] " << buf << std::endl;
    }
private:
    std::mutex mutex_;
};
#define LOG_FATAL(tag, fmt, ...) SimpleLogger::GetInstance().Fatal(tag, fmt, __VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) SimpleLogger::GetInstance().Info(tag, fmt, __VA_ARGS__)
#define LOG_FATAL_EX(exc, tag, fmt, ...) SimpleLogger::GetInstance().Fatal(tag, fmt, __VA_ARGS__)

// --------------------------- CrashMonitor 类声明 ---------------------------
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
    void ShowCrashDialog(PEXCEPTION_POINTERS pException);
    std::atomic<bool> isRunning_;
};

// --------------------------- 工具函数：获取硬件/系统信息 ---------------------------
static std::string GetHardwareInfo() {
    std::ostringstream ss;
    // CPU / 系统信息
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ss << "Processor Architecture: ";
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: ss << "x64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: ss << "x86"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: ss << "ARM64"; break;
    default: ss << "Unknown"; break;
    }
    ss << "\nNumber of Processors: " << si.dwNumberOfProcessors;
    ss << "\nPage size: " << si.dwPageSize;
    ss << "\nAllocation granularity: " << si.dwAllocationGranularity;

    // 内存信息
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        ss << "\nPhysical memory (MB): " << (mem.ullTotalPhys / 1024 / 1024)
            << " total, " << (mem.ullAvailPhys / 1024 / 1024) << " available";
    }
    // 计算机名
    TCHAR name[256];
    DWORD size = _countof(name);
    if (GetComputerName(name, &size)) {
        std::wstring w(name);
        std::string s(w.begin(), w.end());
        ss << "\nComputer name: " << s;
    }
    // OS version (basic)
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma warning(push)
#pragma warning(disable:4996)
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        ss << "\nOS: " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion
            << " Build " << osvi.dwBuildNumber;
    }
#pragma warning(pop)
    return ss.str();
}

// --------------------------- 符号与堆栈解析 ---------------------------
static std::string AddrToSymbol(HANDLE hProc, DWORD64 addr) {
    std::ostringstream out;

    // module info
    IMAGEHLP_MODULE64 moduleInfo;
    ZeroMemory(&moduleInfo, sizeof(moduleInfo));
    moduleInfo.SizeOfStruct = sizeof(moduleInfo);
    if (SymGetModuleInfo64(hProc, addr, &moduleInfo)) {
        std::string moduleName = moduleInfo.ImageName ? moduleInfo.ImageName : "<unknown>";
        out << moduleName;
    }
    else {
        out << "<unknown module>";
    }

    // symbol
    BYTE symBuffer[sizeof(SYMBOL_INFO) + 1024];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symBuffer;
    ZeroMemory(pSymbol, sizeof(symBuffer));
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = 1024;
    DWORD64 displacement = 0;
    if (SymFromAddr(hProc, addr, &displacement, pSymbol)) {
        out << "!" << pSymbol->Name << " + 0x" << std::hex << displacement;
    }
    else {
        out << "!<no-symbol> + 0x" << std::hex << displacement;
    }

    // also show raw addr
    out << " (0x" << std::hex << addr << ")";
    return out.str();
}

static std::string CaptureStackTraceString(HANDLE hProc, CONTEXT* ctx) {
    std::ostringstream ss;
    // 使用 CaptureStackBackTrace 更简单（但它从当前线程）——为了更通用，若提供了 CONTEXT，我们可以使用 StackWalk64。
    // 为了兼容性，这里先尝试使用 RtlCaptureStackBackTrace（CaptureStackBackTrace）
    void* backtrace[62];
    USHORT frames = 0;

    // 如果有异常上下文且需要基于线程上下文的 StackWalk64，可实现，这里先做 CaptureStackBackTrace（当前线程）
    frames = CaptureStackBackTrace(0, _countof(backtrace), backtrace, nullptr);

    ss << "Stack trace (most recent first):\n";
    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = (DWORD64)(backtrace[i]);
        ss << "#" << i << " " << AddrToSymbol(hProc, addr) << "\n";
    }
    return ss.str();
}

// --------------------------- 现代化弹窗实现 ---------------------------
static ATOM RegisterCrashDialogClass(HINSTANCE hInst);
static LRESULT CALLBACK CrashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 将一些需在窗口中显示的数据作为全局（线程安全性在此场景足够）
struct CrashDialogData {
    std::string title;
    std::string message;
    std::string stack;
    std::string hwinfo;
    PEXCEPTION_POINTERS pException;
    HANDLE hProcess;
} g_crashData;

static HWND g_hCrashWnd = nullptr;
static HINSTANCE g_hInstGlobal = nullptr;
static int g_hoverButton = 0;
static int g_pressedButton = 0;

ATOM RegisterCrashDialogClass(HINSTANCE hInst) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = CrashWndProc;
    wc.lpszClassName = L"CrashDialogClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(nullptr, IDI_ERROR);
    return RegisterClassEx(&wc);
}

// 弹出文件选择对话框，返回 Unicode 路径
static std::wstring BrowseForPdb(HWND owner) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrFilter = L"Program Database (*.pdb)\0*.pdb\0All Files\0*.*\0";
    ofn.lpstrTitle = L"选择 PDB 文件手动加载";
    if (GetOpenFileName(&ofn)) {
        return std::wstring(szFile);
    }
    return L"";
}

static bool LoadPdbForModule(HWND owner, HANDLE hProc) {
    std::wstring pdbPath = BrowseForPdb(owner);
    if (pdbPath.empty()) return false;

    // 将 wide string 转为 UTF-8 (或 ANSI)，因为我们这里使用 SymLoadModuleExA（接受 char*）
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, pdbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) {
        MessageBox(owner, L"路径转换失败。", L"Load PDB Failed", MB_ICONERROR);
        return false;
    }
    std::string pdbPathUtf8;
    pdbPathUtf8.resize(utf8len);
    WideCharToMultiByte(CP_UTF8, 0, pdbPath.c_str(), -1, &pdbPathUtf8[0], utf8len, nullptr, nullptr);
    // 去掉末尾的 '\0'（可选）
    if (!pdbPathUtf8.empty() && pdbPathUtf8.back() == '\0') pdbPathUtf8.pop_back();

    // 使用 ANSI 版本的 SymLoadModuleEx（显式调用 A 版本），传入 char*
    DWORD64 base = SymLoadModuleEx(hProc, nullptr, pdbPathUtf8.c_str(), nullptr, 0, 0, nullptr, 0);
    if (base == 0) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        swprintf_s(msg, L"加载 PDB 失败 (SymLoadModuleExA 返回 0)。错误码: %u\n请确保 PDB 与对应模块匹配。", err);
        MessageBox(owner, msg, L"Load PDB Failed", MB_ICONERROR);
        return false;
    }
    else {
        MessageBox(owner, L"成功加载 PDB。", L"PDB Loaded", MB_ICONINFORMATION);
        return true;
    }
}

static bool ExportMiniDump(HWND owner, HANDLE hProc, DWORD pid, PEXCEPTION_POINTERS pException) {
    ULONG handler = RemoveVectoredExceptionHandler(CrashMonitor::VectoredHandler);
    // 保存为 .dmp
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrFilter = L"Minidump (*.dmp)\0*.dmp\0All Files\0*.*\0";
    ofn.lpstrTitle = L"导出进程转存为";
    if (!GetSaveFileName(&ofn)) return false;

    HANDLE hFile = CreateFile(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(owner, L"无法创建转存文件。", L"Error", MB_ICONERROR);
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pException;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(hProc, pid, hFile, MiniDumpNormal, (pException ? &mei : nullptr), nullptr, nullptr);
    CloseHandle(hFile);
    if (!ok) {
        MessageBox(owner, L"写入 mini dump 失败。", L"Error", MB_ICONERROR);
        if (handler) AddVectoredExceptionHandler(1, CrashMonitor::VectoredHandler);
        return false;
    }
    else {
        MessageBox(owner, L"转存成功。", L"Success", MB_ICONINFORMATION);
        if (handler) AddVectoredExceptionHandler(1, CrashMonitor::VectoredHandler);
        return true;
    }
}

// 获取按钮矩形区域
RECT GetButtonRect(int index, int total, int width, int yPos) {
    int buttonWidth = BUTTON_WIDTH;
    int spacing = (width - MARGIN * 2 - buttonWidth * total) / (total - 1);
    RECT rc = {
        MARGIN + index * (buttonWidth + spacing),
        yPos,
        MARGIN + index * (buttonWidth + spacing) + buttonWidth,
        yPos + BUTTON_HEIGHT
    };
    return rc;
}

// Crash 窗口消息处理
LRESULT CALLBACK CrashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 启用深色模式检测
        ModernUI::EnableDarkMode(hWnd, true);
        g_darkMode = true;

        // 设置窗口圆角
        DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

        // 添加阴影效果
        const MARGINS shadow = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hWnd, &shadow);

        return 0;
    }

    case WM_ERASEBKGND: {
        return 1; // 我们已经处理了背景绘制
    }

    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        int oldHover = g_hoverButton;
        g_hoverButton = 0;

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;

        // 检查鼠标是否在按钮上
        for (int i = 1; i <= 5; i++) {
            RECT buttonRect = GetButtonRect(i - 1, 5, clientRect.right, buttonY);
            if (PtInRect(&buttonRect, pt)) {
                g_hoverButton = i;
                break;
            }
        }

        if (oldHover != g_hoverButton) {
            InvalidateRect(hWnd, NULL, TRUE);
        }

        // 设置鼠标跟踪以接收鼠标离开消息
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);

        return 0;
    }

    case WM_MOUSELEAVE: {
        if (g_hoverButton != 0) {
            g_hoverButton = 0;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;

        for (int i = 1; i <= 5; i++) {
            RECT buttonRect = GetButtonRect(i - 1, 5, clientRect.right, buttonY);
            if (PtInRect(&buttonRect, pt)) {
                g_pressedButton = i;
                InvalidateRect(hWnd, NULL, TRUE);
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_pressedButton != 0) {
            int pressed = g_pressedButton;
            g_pressedButton = 0;
            InvalidateRect(hWnd, NULL, TRUE);

            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;

            RECT buttonRect = GetButtonRect(pressed - 1, 5, clientRect.right, buttonY);
            if (PtInRect(&buttonRect, pt)) {
                // 执行按钮操作
                switch (pressed) {
                case 1: // Load PDB
                    LoadPdbForModule(hWnd, g_crashData.hProcess);
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case 2: // Export dump
                {
                    DWORD pid = GetProcessId(g_crashData.hProcess);
                    ExportMiniDump(hWnd, g_crashData.hProcess, pid, g_crashData.pException);
                }
                break;
                case 3: // Copy info
                {
                    std::string full =
                        "Title: " + g_crashData.title + "\n\n" +
                        "Message: " + g_crashData.message + "\n\n" +
                        g_crashData.stack + "\n\n" +
                        "Hardware:\n" + g_crashData.hwinfo +
                        "\n\nAbout:\n作者QQ：3076584115\nGithub：https://github.com/tzdwindows/FileBrowsWindowPatch\n错误提交：https://github.com/tzdwindows/FileBrowsWindowPatch/issues\n";
                    size_t len = full.size() + 1;
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), full.c_str(), len);
                        GlobalUnlock(hMem);
                        OpenClipboard(hWnd);
                        EmptyClipboard();
                        SetClipboardData(CF_TEXT, hMem);
                        CloseClipboard();
                        MessageBox(hWnd, L"已复制到剪贴板", L"Copied", MB_OK);
                    }
                }
                break;
                case 4: // Close
                    DestroyWindow(hWnd);
                    break;
                case 5: // About
                    MessageBoxA(hWnd,
                        "作者QQ：3076584115\n"
                        "Github: https://github.com/tzdwindows/FileBrowsWindowPatch\n"
                        "错误提交: https://github.com/tzdwindows/FileBrowsWindowPatch/issues",
                        "关于", MB_ICONINFORMATION);
                    break;
                }
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scrollOffset -= delta / WHEEL_DELTA * 20;
        if (g_scrollOffset < 0) g_scrollOffset = 0;
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 双缓冲绘制
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

        // 绘制背景
        FillRect(hdcMem, &clientRect, ModernUI::GetBackgroundBrush());

        // 绘制卡片区域
        RECT cardRect = {
            MARGIN,
            MARGIN,
            clientRect.right - MARGIN,
            clientRect.bottom - BUTTON_HEIGHT - MARGIN * 2
        };
        ModernUI::DrawRoundedRect(hdcMem, cardRect, 8, ModernUI::GetCardBrush(),
            CreatePen(PS_SOLID, 1, ModernUI::GetBorderColor()));

        // 设置文本颜色
        SetTextColor(hdcMem, ModernUI::GetTextColor());
        SetBkMode(hdcMem, TRANSPARENT);

        // 绘制标题
        HFONT hFontTitle = CreateFont(20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFontTitle);

        RECT titleRect = {
            cardRect.left + PADDING,
            cardRect.top + PADDING - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.top + PADDING + 30 - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE);

        // 绘制消息
        HFONT hFontNormal = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SelectObject(hdcMem, hFontNormal);

        RECT msgRect = {
            cardRect.left + PADDING,
            titleRect.bottom + 10 - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.bottom - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.message.c_str(), -1, &msgRect, DT_LEFT | DT_WORDBREAK);

        // 绘制堆栈标签
        RECT stackLabelRect = {
            cardRect.left + PADDING,
            msgRect.bottom + 20 - g_scrollOffset,
            cardRect.right - PADDING,
            msgRect.bottom + 40 - g_scrollOffset
        };
        SetTextColor(hdcMem, COLOR_ERROR);
        DrawTextA(hdcMem, "错误堆栈（模块!函数 + 偏移）:", -1, &stackLabelRect, DT_LEFT | DT_SINGLELINE);

        // 绘制堆栈内容
        SetTextColor(hdcMem, ModernUI::GetTextColor());
        RECT stackRect = {
            cardRect.left + PADDING,
            stackLabelRect.bottom + 5 - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.bottom - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.stack.c_str(), -1, &stackRect, DT_LEFT | DT_WORDBREAK);

        // 绘制硬件信息
        RECT hwRect = {
            cardRect.left + PADDING,
            stackRect.bottom + 20 - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.bottom - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.hwinfo.c_str(), -1, &hwRect, DT_LEFT | DT_WORDBREAK);

        // 绘制按钮
        int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;
        const char* buttonTexts[] = {
            "加载PDB(Symbol)",
            "导出进程转存(.dmp)",
            "复制错误信息",
            "关闭",
            "关于"
        };

        for (int i = 0; i < 5; i++) {
            RECT buttonRect = GetButtonRect(i, 5, clientRect.right, buttonY);
            bool hover = (g_hoverButton == i + 1);
            bool pressed = (g_pressedButton == i + 1);
            ModernUI::DrawModernButton(hdcMem, buttonRect, buttonTexts[i], hover, pressed);
        }

        // 将内存DC内容复制到屏幕
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(hdcMem, hOldFont);
        SelectObject(hdcMem, hOld);
        DeleteObject(hFontTitle);
        DeleteObject(hFontNormal);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);
        g_hCrashWnd = nullptr;
        return 0;
    }

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

// --------------------------- CrashMonitor 方法实现 ---------------------------
CrashMonitor::CrashMonitor() : isRunning_(false) {
    SimpleLogger::GetInstance().Initialize();
}

CrashMonitor::~CrashMonitor() {
    Stop();
}

LONG WINAPI CrashMonitor::UnhandledExceptionHandler(PEXCEPTION_POINTERS exception) {
    // 跳过 C++ 异常 (0xe06d7363)
    if (exception->ExceptionRecord->ExceptionCode == 0xe06d7363||
        exception->ExceptionRecord->ExceptionCode == 0x7ffb02e600ac) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LOG_FATAL_EX(exception, "UnhandledExceptionHandler", "Application crashed with exception code: 0x%X", exception->ExceptionRecord->ExceptionCode);

    // Create dialog on main thread: Here we will create a new window thread to show UI,
    // but to keep things simple in this demo we create window synchronously.
    CrashMonitor monitor;
    monitor.ShowCrashDialog(exception);

    // Let system terminate after user interaction
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI CrashMonitor::VectoredHandler(EXCEPTION_POINTERS* ep) {
    /*DWORD exceptionCode = ep->ExceptionRecord->ExceptionCode;
    if (exceptionCode == 0x6BA || // 你的错误代码
        exceptionCode == 0x40010006 || // DBG_PRINTEXCEPTION_C
        exceptionCode == 0x40010008 || // DBG_PRINTEXCEPTION_WIDE_C
        exceptionCode == 0xe06d7363 ||
        exceptionCode == 0x4001000a ||
        exceptionCode == 0x80040155 ||
        exceptionCode == 0x7ffb02e600ac ||
        exceptionCode == 0x406D1388) { // SetThreadName exception
        return EXCEPTION_CONTINUE_SEARCH;
    }
    LOG_FATAL_EX(ep, "VectoredHandler", "Vectored exception caught: 0x%X", ep->ExceptionRecord->ExceptionCode);
    // Show dialog
    CrashMonitor monitor;
    monitor.ShowCrashDialog(ep);*/
    return EXCEPTION_CONTINUE_SEARCH;
}

void CrashMonitor::SignalHandler(int sig) {
    /*LOG_FATAL("SignalHandler", "Signal %d received", sig);
    // 转为人工制造的异常信息并弹窗
    std::ostringstream oss;
    oss << "Received signal: " << sig;
    // 构造最小的 EXCEPTION_POINTERS-equivalent 为 nullptr，用非异常路径显示信息
    CrashMonitor monitor;
    // Build fake exception info? pass nullptr
    monitor.ShowCrashDialog(nullptr);
    abort();*/
}

BOOL WINAPI CrashMonitor::ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        // 正常退出，不记录为崩溃
        return TRUE;
    }
    LOG_FATAL("ConsoleCtrlHandler", "Application terminated by console control event: %u", ctrlType);
    return FALSE;
}

void CrashMonitor::Run() {
    if (isRunning_) return;
    isRunning_ = true;

    // 初始化 DbgHelp 符号处理
    HANDLE hProc = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (!SymInitialize(hProc, nullptr, TRUE)) {
        LOG_INFO("CrashMonitor", "SymInitialize failed: %u", GetLastError());
    }
    else {
        LOG_INFO("CrashMonitor", "DbgHelp initialized.");
    }

    // 设置各类异常/信号处理器
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    AddVectoredExceptionHandler(1, VectoredHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGSEGV, SignalHandler);
    signal(SIGILL, SignalHandler);
    signal(SIGFPE, SignalHandler);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // C++ terminate handler
    std::set_terminate([]() {
        LOG_FATAL("terminate", "std::terminate called");
        // 弹出窗口说明
        CrashMonitor mon;
        mon.ShowCrashDialog(nullptr);
        abort();
        });

    // CRT 异常转换器（将 SEH 转为 C++ 异常）
    _set_se_translator([](unsigned int code, EXCEPTION_POINTERS* pExp) {
        LOG_FATAL("se_translator", "SEH translated, code: 0x%X", code);
        throw std::runtime_error("SEH exception");
        });
}

void CrashMonitor::Stop() {
    if (!isRunning_) return;
    isRunning_ = false;
    SetUnhandledExceptionFilter(nullptr);
    SetConsoleCtrlHandler(nullptr, FALSE);
    // 不能简单移除 vectored handler; 程序结束会清理
    // 清理符号
    SymCleanup(GetCurrentProcess());
}

// 显示弹窗（阻塞直到用户关闭窗口）
void CrashMonitor::ShowCrashDialog(PEXCEPTION_POINTERS pException) {
    // 准备全局数据
    g_crashData.pException = pException;
    g_crashData.hProcess = GetCurrentProcess();
    g_crashData.title = "应用程序发生异常";
    std::ostringstream msg;
    if (pException && pException->ExceptionRecord) {
        msg << "异常代码: 0x" << std::hex << pException->ExceptionRecord->ExceptionCode;
        msg << "\n异常地址: 0x" << std::hex << (DWORD_PTR)pException->ExceptionRecord->ExceptionAddress;
    }
    else {
        msg << "检测到未处理的错误或信号。";
    }
    g_crashData.message = msg.str();

    // 捕获堆栈（当前线程）
    g_crashData.stack = CaptureStackTraceString(GetCurrentProcess(), pException ? pException->ContextRecord : nullptr);

    // 硬件信息
    g_crashData.hwinfo = GetHardwareInfo();

    // 注册类 & 创建窗口
    HINSTANCE hInst = GetModuleHandle(nullptr);
    g_hInstGlobal = hInst;
    RegisterCrashDialogClass(hInst);

    int width = 800, height = 600;
    RECT rc = { 0,0,width,height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowEx(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
        L"CrashDialogClass",
        L"错误 - 应用程序崩溃",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
        width, height,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hwnd) {
        LOG_FATAL("CrashMonitor", "CreateWindowEx failed: %u", GetLastError());
        return;
    }

    g_hCrashWnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // simple message loop until window closes
    MSG msgloop;
    while (GetMessage(&msgloop, nullptr, 0, 0)) {
        TranslateMessage(&msgloop);
        DispatchMessage(&msgloop);
    }
}

// --------------------------- 示例：如何使用 CrashMonitor ---------------------------
/*int main() {
    CrashMonitor crash;
    crash.Run();

    // 故意触发崩溃以测试（取消注释试验）
    int* p = nullptr; *p = 0;

    MessageBoxA(nullptr, "CrashMonitor 已运行。请制造异常以测试。", "Info", MB_OK);
    return 0;
}*/