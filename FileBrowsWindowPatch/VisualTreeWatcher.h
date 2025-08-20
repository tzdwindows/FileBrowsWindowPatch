#pragma once  
#include <string_view>  
#include <unordered_set>  
#include <variant>
#include <xamlOM.h>  
#include <winrt/base.h>  
#include <winrt/Windows.UI.Xaml.h>  
#include <winrt/Windows.UI.Xaml.Hosting.h>  
#include <winrt/Windows.UI.Xaml.Shapes.h>  
#include <wil/resource.h>
#include <windows.ui.xaml.media.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include "Helper.h"

using namespace std::literals;

template <typename T>
winrt::com_ptr<::IInspectable> ToComInspectable(T const& winrtObject) {
    winrt::com_ptr<::IInspectable> result;
    if (auto inspectable = winrtObject.try_as<winrt::Windows::Foundation::IInspectable>()) {
        inspectable.as<::IInspectable>().copy_to(result.put());
    }
    return result;
}

struct XamlBlurBrushParams {
    float blurAmount;
    winrt::Windows::Foundation::Numerics::float4 tint;
};

using PropertyOverrideValue =
std::variant<winrt::Windows::Foundation::IInspectable, XamlBlurBrushParams>;

using PropertyOverrides =
std::unordered_map<winrt::Windows::UI::Xaml::DependencyProperty,
    std::unordered_map<std::wstring, PropertyOverrideValue>>;

using PropertyKeyValue =
std::pair<winrt::Windows::UI::Xaml::DependencyProperty, winrt::Windows::Foundation::IInspectable>;
using PropertyValuesUnresolved =
std::vector<std::pair<std::wstring, std::wstring>>;
using PropertyValues = std::vector<PropertyKeyValue>;
using PropertyValuesMaybeUnresolved =
std::variant<PropertyValuesUnresolved, PropertyValues>;

struct StyleRule {
    std::wstring name;
    std::wstring visualState;
    std::wstring value;
    bool isXamlValue = false;
};
using PropertyOverridesUnresolved = std::vector<StyleRule>;
using PropertyOverridesMaybeUnresolved =
std::variant<PropertyOverridesUnresolved, PropertyOverrides>;

// 前向声明  
class TaskbarAppearanceService;
using InstanceHandle = UINT64;

// 使用与 TranslucentTB 相同的继承方式  
struct VisualTreeWatcher : winrt::implements<VisualTreeWatcher, IVisualTreeServiceCallback2, winrt::non_agile>
{
public:
    VisualTreeWatcher(winrt::com_ptr<IUnknown> site, wil::unique_event_nothrow&& readyEvent);
    void SnapshotFindCommandBarInXamlRoot(InstanceHandle xamlRootHandle);

    // 禁用拷贝和移动  
    VisualTreeWatcher(const VisualTreeWatcher&) = delete;
    VisualTreeWatcher& operator=(const VisualTreeWatcher&) = delete;
    VisualTreeWatcher(VisualTreeWatcher&&) = delete;
    VisualTreeWatcher& operator=(VisualTreeWatcher&&) = delete;

private:
    std::wstring GetElementDetailedName(InstanceHandle handle, const wchar_t* fallbackType = L"<unknown>");
    void ScanVisualTreeRecursive(InstanceHandle elementHandle, int depth);
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType mutationType) override;
    void ModifyBrushProperties(IVisualTreeService2* visualTreeService, InstanceHandle brushHandle);
    void CreateAndSetTransparentBrush(IVisualTreeService2* visualTreeService, InstanceHandle elementHandle,
                                      unsigned int backgroundPropertyIndex);
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle element, VisualElementState elementState, LPCWSTR context) noexcept override;
    winrt::Windows::UI::Xaml::FrameworkElement FindParentByName(std::wstring_view name,
                                                                winrt::Windows::UI::Xaml::DependencyObject
                                                                startElement);

    std::unordered_map<winrt::Windows::UI::Xaml::VisualStateGroup, PropertyOverrides>
        FindElementPropertyOverrides(winrt::Windows::UI::Xaml::FrameworkElement element, std::wstring type, std::wstring name,
            std::optional<std::wstring> visualStateGroupName, PCWSTR fallbackClassName, PropertyValuesMaybeUnresolved propertyValues,
            PropertyOverridesMaybeUnresolved* propertyOverridesMaybeUnresolved);

    template <typename T = ::IInspectable>
    auto FromHandle(InstanceHandle handle)
    {
        winrt::com_ptr<::IInspectable> obj = nullptr;
        m_XamlDiagnostics->GetIInspectableFromHandle(
            handle, obj.put()
        );
        return obj.as<T>();
    }
    template <typename T = ::IInspectable>
    auto FromIInspectable(winrt::com_ptr<T> obj)
    {
        InstanceHandle handle = 0;
        winrt::check_hresult(
            m_XamlDiagnostics->GetHandleFromIInspectable(obj.get(), &handle)
        );

        return handle;
    }
    auto FromIInspectable(::IInspectable* obj)
    {
        InstanceHandle handle = 0;
        winrt::check_hresult(
            m_XamlDiagnostics->GetHandleFromIInspectable(obj, &handle)
        );

        return handle;
    }

    winrt::com_ptr<IXamlDiagnostics> m_XamlDiagnostics;
    winrt::com_ptr<TaskbarAppearanceService> m_AppearanceService;
    std::unordered_set<InstanceHandle> m_NonMatchingXamlSources;
    winrt::com_ptr<IVisualTreeService3> m_visualTreeService;
    wil::unique_event_nothrow m_ReadyEvent;
};