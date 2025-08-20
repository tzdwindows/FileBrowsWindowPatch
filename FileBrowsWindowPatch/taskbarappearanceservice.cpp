#include "pch.h"
#include "taskbarappearanceservice.h"
#include <mutex>
#include <atomic>

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuc = winrt::Windows::UI::Composition;
namespace wfn = winrt::Windows::Foundation::Numerics;

TaskbarAppearanceService::TaskbarAppearanceService()
    : m_XamlThreadQueue(winrt::Windows::System::DispatcherQueue::GetForCurrentThread())
{
}

TaskbarAppearanceService::~TaskbarAppearanceService()
{
    UnregisterAllPropertyCallbacks();
    RestoreAllTaskbarsToDefault();
}

HRESULT TaskbarAppearanceService::SetTaskbarAppearance(HWND taskbar, TaskbarBrush brush, UINT color)
{
    try
    {
        if (auto info = GetTaskbarInfoNonConst(taskbar))
        {
            if (info->background.control)
            {
                auto newBrush = CreateBrush(brush, color);
                info->background.customBrush = newBrush;
                m_isUpdating = true;
                info->background.control.Fill(newBrush);
                m_isUpdating = false;
            }
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

TaskbarInfo* TaskbarAppearanceService::GetTaskbarInfoNonConst(HWND taskbar)
{
    std::lock_guard lock(m_mutex);
    for (auto& [handle, info] : m_Taskbars)
    {
        if (GetAncestor(info.window, GA_PARENT) == taskbar)
        {
            return &info;
        }
    }
    return nullptr;
}

wuxm::Brush TaskbarAppearanceService::CreateBrush(TaskbarBrush brushType, UINT color)
{
    const winrt::Windows::UI::Color tint = {
        static_cast<uint8_t>((color >> 24) & 0xFF), // A
        static_cast<uint8_t>(color & 0xFF),         // R
        static_cast<uint8_t>((color >> 8) & 0xFF),  // G
        static_cast<uint8_t>((color >> 16) & 0xFF)  // B
    };

    if (brushType == TaskbarBrush::Acrylic) {
        wuxm::AcrylicBrush acrylic;
        acrylic.BackgroundSource(wuxm::AcrylicBackgroundSource::Backdrop);
        acrylic.TintColor(tint);
        acrylic.TintOpacity(tint.A / 255.0);
        return acrylic;
    }
    else {
        wuxm::SolidColorBrush solid;
        solid.Color(tint);
        return solid;
    }
}

HRESULT TaskbarAppearanceService::SetTaskbarBlur(HWND taskbar, UINT color, float blurAmount)
{
    try
    {
        if (const auto info = GetTaskbarInfo(taskbar))
        {
            if (info->background.control && info->background.originalFill)
            {
                const winrt::Windows::UI::Color tint = {
                    static_cast<uint8_t>((color >> 24) & 0xFF),
                    static_cast<uint8_t>(color & 0xFF),
                    static_cast<uint8_t>((color >> 8) & 0xFF),
                    static_cast<uint8_t>((color >> 16) & 0xFF)
                };

                wuxm::SolidColorBrush blurBrush;
                blurBrush.Color(tint);
                blurBrush.Opacity(0.8); // 模拟模糊效果的透明度  

                info->background.control.Fill(blurBrush);
            }
        }

        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT TaskbarAppearanceService::ReturnTaskbarToDefaultAppearance(HWND taskbar)
{
    try
    {
        std::lock_guard lock(m_mutex);
        for (auto& [handle, info] : m_Taskbars)
        {
            if (GetAncestor(info.window, GA_PARENT) == taskbar)
            {
                // 清除自定义属性
                info.background.customBrush = nullptr;
                info.background.customOpacity = std::nullopt;

                // 恢复默认
                RestoreDefaultControlFill(info.background);
                break;
            }
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT TaskbarAppearanceService::SetTaskbarOpacity(HWND taskbar, float opacity)
{
    try
    {
        if (auto info = GetTaskbarInfoNonConst(taskbar))
        {
            if (info->background.control)
            {
                // 存储自定义透明度
                info->background.customOpacity = opacity;

                // 应用新透明度
                m_isUpdating = true;
                info->background.control.Opacity(opacity);
                m_isUpdating = false;
            }
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT TaskbarAppearanceService::RestoreAllTaskbarsToDefault()
{
    try
    {
        std::lock_guard lock(m_mutex);
        for (auto& [handle, info] : m_Taskbars)
        {
            // 清除自定义属性
            info.background.customBrush = nullptr;
            info.background.customOpacity = std::nullopt;
            info.border.customBrush = nullptr;
            info.border.customOpacity = std::nullopt;

            // 恢复默认
            RestoreDefaultControlFill(info.background);
            RestoreDefaultControlFill(info.border);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}


void TaskbarAppearanceService::RegisterTaskbarBorder(uintptr_t frameHandle, wux::Shapes::Shape element)
{
    std::lock_guard lock(m_mutex);
    if (const auto it = m_Taskbars.find(frameHandle); it != m_Taskbars.end())
    {
        auto& border = it->second.border;

        // 注销旧回调
        if (border.control)
        {
            if (border.fillToken != 0)
            {
                border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), border.fillToken);
                border.fillToken = {};
            }
            if (border.strokeToken != 0)
            {
                border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), border.strokeToken);
                border.strokeToken = {};
            }
        }

        border.control = element;
        border.originalFill = element.Fill();
        border.originalStroke = element.Stroke();

        border.fillToken = element.RegisterPropertyChangedCallback(
            wux::Shapes::Shape::FillProperty(),
            [this](wux::DependencyObject const& sender, wux::DependencyProperty const& dp) {
                if (m_isUpdating)
                    return;
                if (auto shape = sender.try_as<wux::Shapes::Shape>()) {
                    HandleFillChange(shape, shape.Fill());
                }
            });

        border.strokeToken = element.RegisterPropertyChangedCallback(
            wux::Shapes::Shape::StrokeProperty(),
            [this](wux::DependencyObject const& sender, wux::DependencyProperty const& dp) {
                if (m_isUpdating)
                    return;
                if (auto shape = sender.try_as<wux::Shapes::Shape>()) {
                    HandleStrokeChange(shape, shape.Stroke());
                }
            });
    }
}

void TaskbarAppearanceService::RegisterTaskbar(uintptr_t frameHandle, HWND window)
{
    std::lock_guard lock(m_mutex);
    m_Taskbars.insert_or_assign(frameHandle, TaskbarInfo{ {}, {}, window });
}

void TaskbarAppearanceService::UnregisterAllPropertyCallbacks()
{
    std::lock_guard lock(m_mutex);
    for (auto& [handle, info] : m_Taskbars) {
        if (info.background.control) {
            if (info.background.fillToken != 0) {
                info.background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), info.background.fillToken);
                info.background.fillToken = {};
            }
            if (info.background.strokeToken != 0) {
                info.background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), info.background.strokeToken);
                info.background.strokeToken = {};
            }
            if (info.background.opacityToken != 0) {
                info.background.control.UnregisterPropertyChangedCallback(
                    wux::UIElement::OpacityProperty(), info.background.opacityToken);
                info.background.opacityToken = {};
            }
        }
        if (info.border.control) {
            if (info.border.fillToken != 0) {
                info.border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), info.border.fillToken);
                info.border.fillToken = {};
            }
            if (info.border.strokeToken != 0) {
                info.border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), info.border.strokeToken);
                info.border.strokeToken = {};
            }
            if (info.border.opacityToken != 0) {
                info.border.control.UnregisterPropertyChangedCallback(
                    wux::UIElement::OpacityProperty(), info.border.opacityToken);
                info.border.opacityToken = {};
            }
        }
    }
}

void TaskbarAppearanceService::RegisterTaskbarBackground(uintptr_t frameHandle, wux::Shapes::Shape element)
{
    std::lock_guard lock(m_mutex);
    if (const auto it = m_Taskbars.find(frameHandle); it != m_Taskbars.end())
    {
        auto& background = it->second.background;

        // 注销旧回调
        if (background.control)
        {
            if (background.fillToken != 0)
            {
                background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), background.fillToken);
                background.fillToken = {};
            }
            if (background.strokeToken != 0)
            {
                background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), background.strokeToken);
                background.strokeToken = {};
            }
            if (background.opacityToken != 0)
            {
                background.control.UnregisterPropertyChangedCallback(
                    wux::UIElement::OpacityProperty(), background.opacityToken);
                background.opacityToken = {};
            }
        }

        background.control = element;
        background.originalFill = element.Fill();
        background.originalStroke = element.Stroke();
        background.originalOpacity = element.Opacity();

        background.fillToken = element.RegisterPropertyChangedCallback(
            wux::Shapes::Shape::FillProperty(),
            [this](wux::DependencyObject const& sender, wux::DependencyProperty const& dp) {
                if (m_isUpdating)
                    return;
                if (auto shape = sender.try_as<wux::Shapes::Shape>()) {
                    HandleFillChange(shape, shape.Fill());
                }
            });

        background.strokeToken = element.RegisterPropertyChangedCallback(
            wux::Shapes::Shape::StrokeProperty(),
            [this](wux::DependencyObject const& sender, wux::DependencyProperty const& dp) {
                if (m_isUpdating)
                    return;
                if (auto shape = sender.try_as<wux::Shapes::Shape>()) {
                    HandleStrokeChange(shape, shape.Stroke());
                }
            });

        background.opacityToken = element.RegisterPropertyChangedCallback(
            wux::UIElement::OpacityProperty(),
            [this](wux::DependencyObject const& sender, wux::DependencyProperty const& dp) {
                if (m_isUpdating) return;
                if (auto shape = sender.try_as<wux::Shapes::Shape>()) {
                    HandleOpacityChange(shape, shape.Opacity());
                }
            });
    }
}

void TaskbarAppearanceService::UnregisterTaskbar(uintptr_t frameHandle)
{
    std::lock_guard lock(m_mutex);
    if (auto it = m_Taskbars.find(frameHandle); it != m_Taskbars.end()) {
        // 取消注册回调
        auto& background = it->second.background;
        if (background.control) {
            if (background.fillToken != 0) {
                background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), background.fillToken);
                background.fillToken = {};
            }
            if (background.strokeToken != 0) {
                background.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), background.strokeToken);
                background.strokeToken = {};
            }
            if (background.opacityToken != 0) {
                background.control.UnregisterPropertyChangedCallback(
                    wux::UIElement::OpacityProperty(), background.opacityToken);
                background.opacityToken = {};
            }
        }

        auto& border = it->second.border;
        if (border.control) {
            if (border.fillToken != 0) {
                border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::FillProperty(), border.fillToken);
                border.fillToken = {};
            }
            if (border.strokeToken != 0) {
                border.control.UnregisterPropertyChangedCallback(
                    wux::Shapes::Shape::StrokeProperty(), border.strokeToken);
                border.strokeToken = {};
            }
            if (border.opacityToken != 0) {
                border.control.UnregisterPropertyChangedCallback(
                    wux::UIElement::OpacityProperty(), border.opacityToken);
                border.opacityToken = {};
            }
        }

        m_Taskbars.erase(it);
    }
}

std::optional<TaskbarInfo> TaskbarAppearanceService::GetTaskbarInfo(HWND taskbar)
{
    std::lock_guard lock(m_mutex);
    for (const auto& [handle, info] : m_Taskbars)
    {
        if (GetAncestor(info.window, GA_PARENT) == taskbar)
        {
            return info;
        }
    }
    return std::nullopt;
}

void TaskbarAppearanceService::HandleOpacityChange(wux::Shapes::Shape element, float newOpacity)
{
    if (m_isUpdating) return;

    std::lock_guard lock(m_mutex);
    m_isUpdating = true;

    // 查找包含此元素的 TaskbarInfo
    for (auto& [handle, info] : m_Taskbars) {
        if (info.background.control == element) {
            // 检测到外部修改，恢复自定义透明度
            if (info.background.customOpacity.has_value()) {
                element.Opacity(info.background.customOpacity.value());
            }
            break;
        }
        else if (info.border.control == element) {
            if (info.border.customOpacity.has_value()) {
                element.Opacity(info.border.customOpacity.value());
            }
            break;
        }
    }

    m_isUpdating = false;
}

void TaskbarAppearanceService::RestoreDefaultControlFill(const ControlInfo<wux::Shapes::Shape>& info)
{
    if (info.control) {
        m_isUpdating = true;

        // 恢复原始画刷
        if (info.originalFill) {
            info.control.Fill(info.originalFill);
        }
        if (info.originalStroke) {
            info.control.Stroke(info.originalStroke);
        }

        // 恢复原始透明度
        info.control.Opacity(info.originalOpacity);

        m_isUpdating = false;
    }
}

// 标志位控制，防止递归死循环
std::atomic<bool> TaskbarAppearanceService::m_isUpdating = false;

void TaskbarAppearanceService::HandleFillChange(wux::Shapes::Shape element, wuxm::Brush newFill)
{
    if (m_isUpdating)
        return;

    std::lock_guard lock(m_mutex);
    m_isUpdating = true;

    // 查找包含此元素的 TaskbarInfo
    for (auto& [handle, info] : m_Taskbars) {
        if (info.background.control == element) {
            // 检测到外部修改，恢复自定义外观
            if (info.background.customBrush) {
                element.Fill(info.background.customBrush);
            }
            break;
        }
        else if (info.border.control == element) {
            if (info.border.customBrush) {
                element.Fill(info.border.customBrush);
            }
            break;
        }
    }

    m_isUpdating = false;
}

void TaskbarAppearanceService::HandleStrokeChange(wux::Shapes::Shape element, wuxm::Brush newStroke)
{
    if (m_isUpdating)
        return;

    std::lock_guard lock(m_mutex);
    m_isUpdating = true;

    // 与 HandleFillChange 类似的处理逻辑
    for (auto& [handle, info] : m_Taskbars) {
        if (info.border.control == element && info.border.customStroke) {
            element.Stroke(info.border.customStroke);
            break;
        }
    }

    m_isUpdating = false;
}

