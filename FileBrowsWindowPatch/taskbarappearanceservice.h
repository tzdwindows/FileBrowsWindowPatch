#pragma once

#include <unknwn.h>
#include <windows.h>
#include <unordered_map>
#include <memory>
#include <optional>
#include <winrt/base.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <mutex>
#include <xamlOM.h>

// 任务栏画刷类型枚举
enum class TaskbarBrush
{
    SolidColor = 0,
    Acrylic = 1
};

// 控件信息结构
template<typename T>
struct ControlInfo {
    T control = nullptr;
    winrt::Windows::UI::Xaml::Media::Brush originalFill = nullptr;
    winrt::Windows::UI::Xaml::Media::Brush originalStroke = nullptr;
    winrt::Windows::UI::Xaml::Media::Brush customBrush = nullptr;   // 新增：存储自定义填充
    winrt::Windows::UI::Xaml::Media::Brush customStroke = nullptr;  // 新增：存储自定义描边
    float originalOpacity = 1.0f;  // 添加原始透明度
    std::optional<float> customOpacity = std::nullopt;  // 添加自定义透明度
    int64_t fillToken = 0;  // 改为int64_t
    int64_t strokeToken = 0; // 改为int64_t
    int64_t opacityToken = 0;  // 添加透明度回调令牌
};

// 任务栏信息结构
struct TaskbarInfo
{
    ControlInfo<winrt::Windows::UI::Xaml::Shapes::Shape> background;
    ControlInfo<winrt::Windows::UI::Xaml::Shapes::Shape> border;
    HWND window;
};

// 主要的TaskbarAppearanceService类
class TaskbarAppearanceService
    : public winrt::implements<TaskbarAppearanceService, winrt::Windows::Foundation::IInspectable>
{
public:
    TaskbarAppearanceService();
    ~TaskbarAppearanceService();

    // 核心接口方法
    HRESULT SetTaskbarAppearance(HWND taskbar, TaskbarBrush brush, UINT color);
    TaskbarInfo* GetTaskbarInfoNonConst(HWND taskbar);
    winrt::Windows::UI::Xaml::Media::Brush CreateBrush(TaskbarBrush brushType, UINT color);
    HRESULT SetTaskbarBlur(HWND taskbar, UINT color, float blurAmount);
    HRESULT ReturnTaskbarToDefaultAppearance(HWND taskbar);
    HRESULT SetTaskbarOpacity(HWND taskbar, float opacity);
    HRESULT RestoreAllTaskbarsToDefault();
    void UnregisterAllPropertyCallbacks();

    // 任务栏注册方法
    void RegisterTaskbar(uintptr_t frameHandle, HWND window);
    void RegisterTaskbarBackground(uintptr_t frameHandle, winrt::Windows::UI::Xaml::Shapes::Shape element);
    void UnregisterTaskbar(uintptr_t frameHandle);

    void HandleFillChange(winrt::Windows::UI::Xaml::Shapes::Shape element, winrt::Windows::UI::Xaml::Media::Brush newFill);
    void HandleStrokeChange(winrt::Windows::UI::Xaml::Shapes::Shape element, winrt::Windows::UI::Xaml::Media::Brush newStroke);
    void RegisterTaskbarBorder(uintptr_t frameHandle, winrt::Windows::UI::Xaml::Shapes::Shape element);

private:
    std::optional<TaskbarInfo> GetTaskbarInfo(HWND taskbar);
    void HandleOpacityChange(winrt::Windows::UI::Xaml::Shapes::Shape element, float newOpacity);
    void RestoreDefaultControlFill(const ControlInfo<winrt::Windows::UI::Xaml::Shapes::Shape>& info);
	static std::atomic<bool> m_isUpdating;

    std::unordered_map<uintptr_t, TaskbarInfo> m_Taskbars;
    winrt::Windows::System::DispatcherQueue m_XamlThreadQueue;
    std::mutex m_mutex;
};