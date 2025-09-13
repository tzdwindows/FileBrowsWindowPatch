// CrashMonitor.cpp - �ִ��� UI �汾
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

// ��ɫ���� - �ִ�����ɫ����
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

// �ؼ��ߴ�ͼ��
#define MARGIN 20
#define PADDING 16
#define BUTTON_HEIGHT 32
#define BUTTON_WIDTH 140
#define SCROLLBAR_WIDTH 12

static int g_scrollOffset = 0;
static bool g_darkMode = false;

// --------------------------- �ִ��� UI �������� ---------------------------
namespace ModernUI {
    void EnableDarkMode(HWND hWnd, bool enable) {
        g_darkMode = enable;

        // ���ô�����ɫģʽ
        BOOL dark = enable ? TRUE : FALSE;
        DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));

        // ���·ǿͻ���
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

        // ����Բ�ǰ�ť
        DrawRoundedRect(hdc, rect, radius, bgBrush, CreatePen(PS_SOLID, 1, COLOR_ACCENT));

        // �����ı�
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

// --------------------------- ���� Logger����ʾ�ã� ---------------------------
class SimpleLogger {
public:
    static SimpleLogger& GetInstance() {
        static SimpleLogger inst;
        return inst;
    }
    void Initialize() { /* ����չ */ }
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

// --------------------------- CrashMonitor ������ ---------------------------
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

// --------------------------- ���ߺ�������ȡӲ��/ϵͳ��Ϣ ---------------------------
static std::string GetHardwareInfo() {
    std::ostringstream ss;
    // CPU / ϵͳ��Ϣ
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

    // �ڴ���Ϣ
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        ss << "\nPhysical memory (MB): " << (mem.ullTotalPhys / 1024 / 1024)
            << " total, " << (mem.ullAvailPhys / 1024 / 1024) << " available";
    }
    // �������
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

// --------------------------- �������ջ���� ---------------------------
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
    // ʹ�� CaptureStackBackTrace ���򵥣������ӵ�ǰ�̣߳�����Ϊ�˸�ͨ�ã����ṩ�� CONTEXT�����ǿ���ʹ�� StackWalk64��
    // Ϊ�˼����ԣ������ȳ���ʹ�� RtlCaptureStackBackTrace��CaptureStackBackTrace��
    void* backtrace[62];
    USHORT frames = 0;

    // ������쳣����������Ҫ�����߳������ĵ� StackWalk64����ʵ�֣��������� CaptureStackBackTrace����ǰ�̣߳�
    frames = CaptureStackBackTrace(0, _countof(backtrace), backtrace, nullptr);

    ss << "Stack trace (most recent first):\n";
    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = (DWORD64)(backtrace[i]);
        ss << "#" << i << " " << AddrToSymbol(hProc, addr) << "\n";
    }
    return ss.str();
}

// --------------------------- �ִ�������ʵ�� ---------------------------
static ATOM RegisterCrashDialogClass(HINSTANCE hInst);
static LRESULT CALLBACK CrashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ��һЩ���ڴ�������ʾ��������Ϊȫ�֣��̰߳�ȫ���ڴ˳����㹻��
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

// �����ļ�ѡ��Ի��򣬷��� Unicode ·��
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
    ofn.lpstrTitle = L"ѡ�� PDB �ļ��ֶ�����";
    if (GetOpenFileName(&ofn)) {
        return std::wstring(szFile);
    }
    return L"";
}

static bool LoadPdbForModule(HWND owner, HANDLE hProc) {
    std::wstring pdbPath = BrowseForPdb(owner);
    if (pdbPath.empty()) return false;

    // �� wide string תΪ UTF-8 (�� ANSI)����Ϊ��������ʹ�� SymLoadModuleExA������ char*��
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, pdbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) {
        MessageBox(owner, L"·��ת��ʧ�ܡ�", L"Load PDB Failed", MB_ICONERROR);
        return false;
    }
    std::string pdbPathUtf8;
    pdbPathUtf8.resize(utf8len);
    WideCharToMultiByte(CP_UTF8, 0, pdbPath.c_str(), -1, &pdbPathUtf8[0], utf8len, nullptr, nullptr);
    // ȥ��ĩβ�� '\0'����ѡ��
    if (!pdbPathUtf8.empty() && pdbPathUtf8.back() == '\0') pdbPathUtf8.pop_back();

    // ʹ�� ANSI �汾�� SymLoadModuleEx����ʽ���� A �汾�������� char*
    DWORD64 base = SymLoadModuleEx(hProc, nullptr, pdbPathUtf8.c_str(), nullptr, 0, 0, nullptr, 0);
    if (base == 0) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        swprintf_s(msg, L"���� PDB ʧ�� (SymLoadModuleExA ���� 0)��������: %u\n��ȷ�� PDB ���Ӧģ��ƥ�䡣", err);
        MessageBox(owner, msg, L"Load PDB Failed", MB_ICONERROR);
        return false;
    }
    else {
        MessageBox(owner, L"�ɹ����� PDB��", L"PDB Loaded", MB_ICONINFORMATION);
        return true;
    }
}

static bool ExportMiniDump(HWND owner, HANDLE hProc, DWORD pid, PEXCEPTION_POINTERS pException) {
    ULONG handler = RemoveVectoredExceptionHandler(CrashMonitor::VectoredHandler);
    // ����Ϊ .dmp
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrFilter = L"Minidump (*.dmp)\0*.dmp\0All Files\0*.*\0";
    ofn.lpstrTitle = L"��������ת��Ϊ";
    if (!GetSaveFileName(&ofn)) return false;

    HANDLE hFile = CreateFile(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(owner, L"�޷�����ת���ļ���", L"Error", MB_ICONERROR);
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = pException;
    mei.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(hProc, pid, hFile, MiniDumpNormal, (pException ? &mei : nullptr), nullptr, nullptr);
    CloseHandle(hFile);
    if (!ok) {
        MessageBox(owner, L"д�� mini dump ʧ�ܡ�", L"Error", MB_ICONERROR);
        if (handler) AddVectoredExceptionHandler(1, CrashMonitor::VectoredHandler);
        return false;
    }
    else {
        MessageBox(owner, L"ת��ɹ���", L"Success", MB_ICONINFORMATION);
        if (handler) AddVectoredExceptionHandler(1, CrashMonitor::VectoredHandler);
        return true;
    }
}

// ��ȡ��ť��������
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

// Crash ������Ϣ����
LRESULT CALLBACK CrashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // ������ɫģʽ���
        ModernUI::EnableDarkMode(hWnd, true);
        g_darkMode = true;

        // ���ô���Բ��
        DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

        // �����ӰЧ��
        const MARGINS shadow = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hWnd, &shadow);

        return 0;
    }

    case WM_ERASEBKGND: {
        return 1; // �����Ѿ������˱�������
    }

    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        int oldHover = g_hoverButton;
        g_hoverButton = 0;

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;

        // �������Ƿ��ڰ�ť��
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

        // �����������Խ�������뿪��Ϣ
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
                // ִ�а�ť����
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
                        "\n\nAbout:\n����QQ��3076584115\nGithub��https://github.com/tzdwindows/FileBrowsWindowPatch\n�����ύ��https://github.com/tzdwindows/FileBrowsWindowPatch/issues\n";
                    size_t len = full.size() + 1;
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), full.c_str(), len);
                        GlobalUnlock(hMem);
                        OpenClipboard(hWnd);
                        EmptyClipboard();
                        SetClipboardData(CF_TEXT, hMem);
                        CloseClipboard();
                        MessageBox(hWnd, L"�Ѹ��Ƶ�������", L"Copied", MB_OK);
                    }
                }
                break;
                case 4: // Close
                    DestroyWindow(hWnd);
                    break;
                case 5: // About
                    MessageBoxA(hWnd,
                        "����QQ��3076584115\n"
                        "Github: https://github.com/tzdwindows/FileBrowsWindowPatch\n"
                        "�����ύ: https://github.com/tzdwindows/FileBrowsWindowPatch/issues",
                        "����", MB_ICONINFORMATION);
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

        // ˫�������
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

        // ���Ʊ���
        FillRect(hdcMem, &clientRect, ModernUI::GetBackgroundBrush());

        // ���ƿ�Ƭ����
        RECT cardRect = {
            MARGIN,
            MARGIN,
            clientRect.right - MARGIN,
            clientRect.bottom - BUTTON_HEIGHT - MARGIN * 2
        };
        ModernUI::DrawRoundedRect(hdcMem, cardRect, 8, ModernUI::GetCardBrush(),
            CreatePen(PS_SOLID, 1, ModernUI::GetBorderColor()));

        // �����ı���ɫ
        SetTextColor(hdcMem, ModernUI::GetTextColor());
        SetBkMode(hdcMem, TRANSPARENT);

        // ���Ʊ���
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

        // ������Ϣ
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

        // ���ƶ�ջ��ǩ
        RECT stackLabelRect = {
            cardRect.left + PADDING,
            msgRect.bottom + 20 - g_scrollOffset,
            cardRect.right - PADDING,
            msgRect.bottom + 40 - g_scrollOffset
        };
        SetTextColor(hdcMem, COLOR_ERROR);
        DrawTextA(hdcMem, "�����ջ��ģ��!���� + ƫ�ƣ�:", -1, &stackLabelRect, DT_LEFT | DT_SINGLELINE);

        // ���ƶ�ջ����
        SetTextColor(hdcMem, ModernUI::GetTextColor());
        RECT stackRect = {
            cardRect.left + PADDING,
            stackLabelRect.bottom + 5 - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.bottom - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.stack.c_str(), -1, &stackRect, DT_LEFT | DT_WORDBREAK);

        // ����Ӳ����Ϣ
        RECT hwRect = {
            cardRect.left + PADDING,
            stackRect.bottom + 20 - g_scrollOffset,
            cardRect.right - PADDING,
            cardRect.bottom - g_scrollOffset
        };
        DrawTextA(hdcMem, g_crashData.hwinfo.c_str(), -1, &hwRect, DT_LEFT | DT_WORDBREAK);

        // ���ư�ť
        int buttonY = clientRect.bottom - BUTTON_HEIGHT - MARGIN;
        const char* buttonTexts[] = {
            "����PDB(Symbol)",
            "��������ת��(.dmp)",
            "���ƴ�����Ϣ",
            "�ر�",
            "����"
        };

        for (int i = 0; i < 5; i++) {
            RECT buttonRect = GetButtonRect(i, 5, clientRect.right, buttonY);
            bool hover = (g_hoverButton == i + 1);
            bool pressed = (g_pressedButton == i + 1);
            ModernUI::DrawModernButton(hdcMem, buttonRect, buttonTexts[i], hover, pressed);
        }

        // ���ڴ�DC���ݸ��Ƶ���Ļ
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

        // ������Դ
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

// --------------------------- CrashMonitor ����ʵ�� ---------------------------
CrashMonitor::CrashMonitor() : isRunning_(false) {
    SimpleLogger::GetInstance().Initialize();
}

CrashMonitor::~CrashMonitor() {
    Stop();
}

LONG WINAPI CrashMonitor::UnhandledExceptionHandler(PEXCEPTION_POINTERS exception) {
    // ���� C++ �쳣 (0xe06d7363)
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
    if (exceptionCode == 0x6BA || // ��Ĵ������
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
    // תΪ�˹�������쳣��Ϣ������
    std::ostringstream oss;
    oss << "Received signal: " << sig;
    // ������С�� EXCEPTION_POINTERS-equivalent Ϊ nullptr���÷��쳣·����ʾ��Ϣ
    CrashMonitor monitor;
    // Build fake exception info? pass nullptr
    monitor.ShowCrashDialog(nullptr);
    abort();*/
}

BOOL WINAPI CrashMonitor::ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        // �����˳�������¼Ϊ����
        return TRUE;
    }
    LOG_FATAL("ConsoleCtrlHandler", "Application terminated by console control event: %u", ctrlType);
    return FALSE;
}

void CrashMonitor::Run() {
    if (isRunning_) return;
    isRunning_ = true;

    // ��ʼ�� DbgHelp ���Ŵ���
    HANDLE hProc = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (!SymInitialize(hProc, nullptr, TRUE)) {
        LOG_INFO("CrashMonitor", "SymInitialize failed: %u", GetLastError());
    }
    else {
        LOG_INFO("CrashMonitor", "DbgHelp initialized.");
    }

    // ���ø����쳣/�źŴ�����
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
        // ��������˵��
        CrashMonitor mon;
        mon.ShowCrashDialog(nullptr);
        abort();
        });

    // CRT �쳣ת�������� SEH תΪ C++ �쳣��
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
    // ���ܼ��Ƴ� vectored handler; �������������
    // �������
    SymCleanup(GetCurrentProcess());
}

// ��ʾ����������ֱ���û��رմ��ڣ�
void CrashMonitor::ShowCrashDialog(PEXCEPTION_POINTERS pException) {
    // ׼��ȫ������
    g_crashData.pException = pException;
    g_crashData.hProcess = GetCurrentProcess();
    g_crashData.title = "Ӧ�ó������쳣";
    std::ostringstream msg;
    if (pException && pException->ExceptionRecord) {
        msg << "�쳣����: 0x" << std::hex << pException->ExceptionRecord->ExceptionCode;
        msg << "\n�쳣��ַ: 0x" << std::hex << (DWORD_PTR)pException->ExceptionRecord->ExceptionAddress;
    }
    else {
        msg << "��⵽δ����Ĵ�����źš�";
    }
    g_crashData.message = msg.str();

    // �����ջ����ǰ�̣߳�
    g_crashData.stack = CaptureStackTraceString(GetCurrentProcess(), pException ? pException->ContextRecord : nullptr);

    // Ӳ����Ϣ
    g_crashData.hwinfo = GetHardwareInfo();

    // ע���� & ��������
    HINSTANCE hInst = GetModuleHandle(nullptr);
    g_hInstGlobal = hInst;
    RegisterCrashDialogClass(hInst);

    int width = 800, height = 600;
    RECT rc = { 0,0,width,height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowEx(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
        L"CrashDialogClass",
        L"���� - Ӧ�ó������",
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

// --------------------------- ʾ�������ʹ�� CrashMonitor ---------------------------
/*int main() {
    CrashMonitor crash;
    crash.Run();

    // ���ⴥ�������Բ��ԣ�ȡ��ע�����飩
    int* p = nullptr; *p = 0;

    MessageBoxA(nullptr, "CrashMonitor �����С��������쳣�Բ��ԡ�", "Info", MB_OK);
    return 0;
}*/