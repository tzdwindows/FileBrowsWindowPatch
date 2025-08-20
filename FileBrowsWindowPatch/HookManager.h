#pragma once
#include "pch.h"
#include "WindowEffect.h"
#include <dwmapi.h>
#include <vssym32.h>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include "ImageRenderer.h"


#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

#define WM_USER_REDRAW                         0x114514
typedef __int64(__fastcall* UpdateBackground_t)(void* pThis);

struct BitmapCache {
    HBITMAP hBitmap = nullptr;
    std::wstring path;
    int width = 0;
    int height = 0;
};

typedef BOOL(WINAPI* PCreateProcessW)(
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
    );

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINCOMPATTRDATA {
    DWORD attribute;
    PVOID pData;
    ULONG dataSize;
} WINCOMPATTRDATA;

typedef void(__fastcall* PaintBackground_t)(
    void* pThis,                // this ָ��
    void* EDX,                  // �����Ĵ��� (x86)
    HDC hdc,                    // �豸������
    void* pValue,               // DirectUI::Value*
    const RECT* pRect1,         // �������� 1
    const RECT* pRect2,         // �������� 2
    const void* pValue2,        // ��������
    const void* pValue3         // ��������
    );


struct ACCENTPOLICY {
    int nAccentState;
    int nFlags;
    int nColor;
    int nAnimationId;
};

typedef void(__fastcall* Element_Paint_t)(
    void* pThis,
    HDC hdc,
    const RECT* prcBounds,
    const RECT* prcInvalid,
    RECT* prcBorder,
    RECT* prcContent
    );


#define DWMSBT_DISABLE DWMSBT_NONE

// ����SetWindowCompositionAttribute����
typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);
extern pfnSetWindowCompositionAttribute SetWindowCompositionAttribute;

// ����GetThemeClassName����
typedef HRESULT(WINAPI* pfnGetThemeClassName)(HTHEME, LPWSTR, int);
extern pfnGetThemeClassName GetThemeClassName;

namespace WindowsVersion {
    bool IsBuildOrGreater(DWORD buildNumber);
    DWORD GetBuildNumber();
}

#pragma pack(push, 1) // ȷ���ڴ����
struct Remote_Config {
    int effType = 0;
    COLORREF blendColor = 0;
    bool automatic_acquisition_color = false;
    int automatic_acquisition_color_transparency = -1;
    wchar_t imagePath[256] = {}; // ʹ�ù̶������ַ�����
    float imageOpacity = 0.8f;
    int imageBlurRadius = 5;
    bool smallborder = true;

    bool enabled = true;

    int NotificationLevel = true;
};
#pragma pack(pop)

class RoundRectPath : public Gdiplus::GraphicsPath
{

public:
    RoundRectPath(Gdiplus::Rect rc, float round)
    {
        using namespace Gdiplus;
        REAL x = (float)rc.X, y = (float)rc.Y;
        REAL width = (float)rc.Width, height = (float)rc.Height;

        REAL elWid = 2 * round;
        REAL elHei = 2 * round;
        //if (elWid > width) {
        //    elWid = width;
        //    elWid += elWid / 2;
        //}
        //if (elHei > height) {
        //    elHei = height;
        //    elHei += elHei / 2;
        //}

        AddArc(x, y, elWid, elHei, 180, 90); // ���Ͻ�Բ��
        AddLine(x + round, y, x + width - round, y); // �ϱ�
        AddArc(x + width - elWid, y, elWid, elHei, 270, 90); // ���Ͻ�Բ��
        AddLine(x + width, y + round, x + width, y + height - round);// �ұ�
        AddArc(x + width - elWid, y + height - elHei, elWid, elHei, 0, 90); // ���½�Բ��
        AddLine(x + width - round, y + height, x + round, y + height); // �±�
        AddArc(x, y + height - elHei, elWid, elHei, 90, 90);
        AddLine(x, y + round, x, y + height - round);

        CloseAllFigures();

    }
};

struct WinSize {
    int cx = 0;
    int cy = 0;
};

struct ImageCache {
    HBITMAP hBitmap = nullptr;
    std::wstring path;
    int width = 0;
    int height = 0;
    WinSize lastWinSize = { 0,0 };
    float lastOpacity = 0.0f;
    bool needsUpdate = true;
};

class HookManager {
public:
    static int GetDetourHookCount();
    static void InstallHooks();
    static void RemoveHooks();
    static void RefreshConfig();

    struct Config {
        int effType = 0;        // Ч������ 0=Blur 1=Acrylic 2=Mica 3=blur
        COLORREF blendColor = 0; // �����ɫ

        bool automatic_acquisition_color = false;
        int automatic_acquisition_color_transparency = -1;

        std::wstring imagePath;
        float imageOpacity = 0.8f;
        int imageBlurRadius = 5;
        bool smallborder = true;
    };

    static Config m_config;
private:
    // ԭʼ����ָ��
    static inline decltype(&CreateWindowExW) OriginalCreateWindowExW = nullptr;
    static inline decltype(&DestroyWindow) OriginalDestroyWindow = nullptr;
    static inline decltype(&BeginPaint) OriginalBeginPaint = nullptr;
    static inline decltype(&EndPaint) OriginalEndPaint = nullptr;
    static inline decltype(&FillRect) OriginalFillRect = nullptr;
    static inline decltype(&DrawTextW) OriginalDrawTextW = nullptr;
    static inline decltype(&DrawTextExW) OriginalDrawTextExW = nullptr;
    static inline decltype(&ExtTextOutW) OriginalExtTextOutW = nullptr;
    static inline decltype(&CreateCompatibleDC) OriginalCreateCompatibleDC = nullptr;
    static inline decltype(&GetThemeColor) OriginalGetThemeColor = nullptr;
    static inline decltype(&DrawThemeText) OriginalDrawThemeText = nullptr;
    static inline decltype(&DrawThemeTextEx) OriginalDrawThemeTextEx = nullptr;
    static inline decltype(&DrawThemeBackground) OriginalDrawThemeBackground = nullptr;
    static inline decltype(&DrawThemeBackgroundEx) OriginalDrawThemeBackgroundEx = nullptr;
    static inline decltype(&PatBlt) OriginalPatBlt = nullptr;
    static inline decltype(&RegisterClassExW) OriginalRegisterClassExW = nullptr;
    static inline decltype(&DwmSetWindowAttribute) OriginalDwmSetWindowAttribute = nullptr;

    static inline decltype(&AlphaBlend) OriginalAlphaBlend = nullptr;
    static inline decltype(&GdiGradientFill) OriginalGdiGradientFill = nullptr;
    static inline decltype(&Rectangle) OriginalRectangle = nullptr;
    static inline decltype(&SetBkColor) OriginalSetBkColor = nullptr;
    static inline decltype(&SetDCBrushColor) OriginalSetDCBrushColor = nullptr;
    static inline decltype(&SetDCPenColor) OriginalSetDCPenColor = nullptr;
    static inline decltype(&GetDCBrushColor) OriginalGetDCBrushColor = nullptr;
    static inline decltype(&GetDCPenColor) OriginalGetDCPenColor = nullptr;
    static inline decltype(&DwmExtendFrameIntoClientArea) OriginalDwmExtendFrameIntoClientArea = nullptr;
    static inline decltype(&BitBlt) OriginalBitBlt = nullptr;
    static inline decltype(&GdiAlphaBlend) OriginalGdiAlphaBlend = nullptr;
    static inline decltype(&StretchBlt) OriginalStretchBlt = nullptr;
    static inline PCreateProcessW OriginalCreateProcessW = NULL;

    // ����ԭʼ����ָ��
    static UpdateBackground_t OriginalUpdateBackground;
    // ���Ӻ���
    static HWND WINAPI HookedCreateWindowExW(
        DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
        DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
        HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
    static void LogDetourError(LONG error);
    static void __fastcall HookedElementPaint(void* pThis, HDC hdc, const RECT* prcBounds, const RECT* prcInvalid, RECT* prcBorder, RECT* prcContent);
    static PaintBackground_t OriginalPaintBackground;
    static BOOL WINAPI HookedCreateProcessW(
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
    );

    static BOOL __stdcall HookedAlphaBlend(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, BLENDFUNCTION blendFunction);

    static BOOL WINAPI HookedDestroyWindow(HWND hWnd);
    static HDC WINAPI HookedBeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
    static BOOL WINAPI HookedEndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint);
    static UINT CalcRibbonHeightForDPI(HWND hWnd, UINT src, bool normal = true, bool offsets = true);
    static bool CompareColor(COLORREF color1, COLORREF color2);
    static int WINAPI HookedFillRect(HDC hDC, const RECT* lprc, HBRUSH hbr);
    static HRESULT __stdcall HookedDwmSetWindowAttribute(HWND hwnd,DWORD dwAttribute,_In_reads_bytes_(cbAttribute) LPCVOID pvAttribute,DWORD cbAttribute);
    static int WINAPI HookedDrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);
    static int WINAPI HookedDrawTextExW(HDC hdc, LPWSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp);
    static bool IsDUIThread();
    static BOOL WINAPI HookedExtTextOutW(HDC hdc, int x, int y, UINT option, const RECT* lprect, LPCWSTR lpString, UINT c, const INT* lpDx);
    static HDC WINAPI HookedCreateCompatibleDC(HDC hDC);
    static HRESULT WINAPI HookedGetThemeColor(HTHEME hTheme, int iPartId, int iStateId, int iPropId, COLORREF* pColor);
    static std::wstring GetThemeClassName(HTHEME hTheme);
    static HRESULT _DrawThemeTextEx(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCTSTR pszText, int cchText, DWORD dwTextFlags, LPCRECT pRect, const DTTOPTS* pOptions);
    static HRESULT WINAPI HookedDrawThemeText(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int cchText, DWORD dwTextFlags, DWORD dwTextFlags2, LPCRECT pRect);
    static HRESULT WINAPI HookedDrawThemeTextEx(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int cchText, DWORD dwTextFlags, LPCRECT pRect, const DTTOPTS* pOptions);
    static HRESULT WINAPI HookedDrawThemeBackground(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCRECT pRect, LPCRECT pClipRect);
    static HRESULT WINAPI HookedDrawThemeBackgroundEx(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCRECT pRect, const DTBGOPTS* pOptions);
    static void StartAero(HWND hwnd, int type, COLORREF color, bool blend);
    static BOOL WINAPI HookedPatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop);
    static ATOM __stdcall HookedRegisterClassExW(const WNDCLASSEXW* lpWndClass);
    static void __fastcall HookedPaintBackground(
        void* pThis,
        void* EDX,
        HDC hdc,
        void* pValue,
        const RECT* pRect1,
        const RECT* pRect2,
        const void* pValue2,
        const void* pValue3
    );
    static long long HookedUpdateBackground(void* pThis);

    static BOOL HookedGdiAlphaBlend(
         HDC           hdcDest,
        int           xoriginDest,
         int           yoriginDest,
        int           wDest,
         int           hDest,
         HDC           hdcSrc,
         int           xoriginSrc,
         int           yoriginSrc,
         int           wSrc,
         int           hSrc,
         BLENDFUNCTION ftn
    );

    static BOOL WINAPI HookedStretchBlt(_In_ HDC hdcDest, _In_ int xDest, _In_ int yDest, _In_ int wDest, _In_ int hDest, _In_opt_ HDC hdcSrc, _In_ int xSrc, _In_ int ySrc, _In_ int wSrc, _In_ int hSrc, _In_ DWORD rop);
    
    // ��������
    static HWND FindExplorerMainWindow(HWND hChild);
    static std::wstring GetWindowClassName(HWND hWnd);
    static bool AlphaBuffer(HDC hdc, LPRECT pRc, std::function<void(HDC)> fun);
    static void SetWindowBlur(HWND hWnd);
    static void ApplyAcrylicEffect(HWND hWnd, DWORD build, COLORREF color, bool isBlend, BYTE alpha);
    static void ApplyMicaEffect(HWND hWnd, DWORD build, COLORREF color, bool isBlend, BYTE alpha);
    static void ApplyBlurEffect(HWND hWnd, DWORD build, COLORREF color, bool isBlend, BYTE alpha);
    static void OnWindowSize(HWND hWnd, int newHeight);
    static HRESULT DwmUpdateAccentBlurRect(HWND hWnd, RECT* prc);
    static LRESULT WINAPI WndSubProc(HWND hWnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static ULONG_PTR m_gdiplusToken;
    static  Element_Paint_t OriginalElementPaint;

    /*
		ACCENT_DISABLED (0)
		����������Ч���ָ����ڻ���������Ĭ����ۡ�
		ACCENT_ENABLE_GRADIENT (1)
		���ý���ɫ���������ڻ�����������ʾһ�ִ��н������ɫЧ��������͸����
		ACCENT_ENABLE_TRANSPARENTGRADIENT (2)
		����͸�����䱳��������һ�����ƣ�������һ����͸���ȣ��ñ������ݲ��ֿɼ���
		ACCENT_ENABLE_BLURBEHIND (3)
		����ģ����Blur Behind��Ч�������ڻ���������������ë����Ч������������ݻᱻģ������
		ACCENT_ENABLE_ACRYLICBLURBEHIND (4)
		�����ǿ���ģ����Acrylic Blur��Ч��������ͨģ�����в�θк͹���У��� Windows 10/11 �ϵ� Fluent Design ���
		ACCENT_INVALID_STATE (5)
		�Ƿ�״̬����ʾ��ǰ״̬��Ч�����ʱʹ��""��
	*/
    enum AccentState {
        ACCENT_DISABLED = 0,
        ACCENT_ENABLE_GRADIENT = 1,
        ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
        ACCENT_ENABLE_BLURBEHIND = 3,
        ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
        ACCENT_INVALID_STATE = 5
    };

    static DWORD GetSystemThemeColor();
    static bool SetTaskbarTransparency(HWND hwnd ,HookManager::AccentState accentState,COLORREF color, BYTE alpha = 50);

    struct AccentPolicy {
        AccentState AccentState;
        DWORD AccentFlags;
        DWORD GradientColor;
        DWORD AnimationId;
    };

    struct DUIData {
        HWND hWnd = 0;        // DirectUIHWND
        HWND mainWnd = 0;     // Explorer����
        HWND TreeWnd = 0;     // TreeView���
        HDC hDC = 0;          // ��ǰDC
        HDC srcDC = 0;        // ԭʼDC

        int width = 0;
        int height = 0;

        bool treeDraw = false;
        bool refresh = true;
    };
    struct DUIThreadData {
        HWND hWnd, mainWnd, treeWnd;
        bool treeDraw = false;
    };

    static std::map<DWORD, DUIThreadData> duiThreads;
    static HBRUSH m_clearBrush;
    static std::map<HWND, WindowEffect> windowEffects;
    static std::mutex effectMutex;
    static std::unordered_map<DWORD, DUIData> m_DUIList;           // DUI�߳�����
    static std::unordered_map<DWORD, bool> m_drawtextState;        // �ı�����״̬
    static std::unordered_map<DWORD, std::pair<HWND, HDC>> m_ribbonPaint; // Ribbon����״̬
    static std::set<std::wstring> transparentClasses;
    static HBRUSH transparentBrush;

    static bool is_installHooks;
    static BOOL __stdcall HookedGradientFill(HDC hdc, PTRIVERTEX pVertex, ULONG nVertex, PVOID pMesh, ULONG nMesh, ULONG ulMode);
    static COLORREF MakeTransparent(COLORREF color);
    static BOOL __stdcall HookedRectangle(HDC hdc, int left, int top, int right, int bottom);
    static COLORREF __stdcall HookedSetBkColor(HDC hdc, COLORREF color);
    static COLORREF __stdcall HookedSetDCBrushColor(HDC hdc, COLORREF color);
    static COLORREF __stdcall HookedSetDCPenColor(HDC hdc, COLORREF color);
    static COLORREF __stdcall HookedGetDCPenColor(HDC hdc);
    static  BOOL __stdcall HookedBitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop);
    static COLORREF __stdcall HookedOriginalDwmExtendFrameIntoClientArea(HWND hWnd, const MARGINS* pMarInset);
    static std::unordered_map<std::wstring, ImageCache> s_imageCache;
};
