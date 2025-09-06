#include "pch.h"
#include "VisualTreeWatcher.h"
#include <sstream>
#include "TaskbarAppearanceService.h"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>  
#include <comutil.h>
#include <inspectable.h>
#include <windows.ui.xaml.h>               // Windows.UI.Xaml 命名空间
#include <windows.ui.xaml.controls.h>      // 控件相关接口
#include <windows.ui.xaml.shapes.h>        // Shape 类型


#include <fstream>
#include <string>
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <sstream>
#include <vector>
#include <combaseapi.h>
#include <stack>

#include "Logger.h"
#include <windows.ui.xaml.h>
#include "winstring.h"
#include "XamlTreeScanner.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <xamlom.winui.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <wrl/client.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.xaml.shapes.h>

#include <winrt/Windows.UI.Xaml.h>            

#pragma comment(lib, "comsuppw.lib") 
#pragma comment(lib, "runtimeobject.lib")

using namespace Microsoft::WRL;

VisualTreeWatcher::VisualTreeWatcher(winrt::com_ptr<IUnknown> site, wil::unique_event_nothrow&& readyEvent) :
    m_XamlDiagnostics(site.as<IXamlDiagnostics>()),
    m_AppearanceService(winrt::make_self<TaskbarAppearanceService>()),
    m_ReadyEvent(std::move(readyEvent))
{
    HANDLE thread = CreateThread(
        nullptr, 0,
        [](LPVOID lpParam) -> DWORD {
            auto watcher = reinterpret_cast<VisualTreeWatcher*>(lpParam);
            HRESULT hr = watcher->m_XamlDiagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(watcher);
            if (FAILED(hr))
            {
                //LOG_ERROR("[VisualTreeWatcher.cpp][VisualTreeWatcher]", "Loading AdviseVisualTreeChange error, error code", hr);
            }
            return 0;
        },
        this, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }

    m_ReadyEvent.SetEvent();
}

HRESULT VisualTreeWatcher::OnVisualTreeChange(
    ParentChildRelation relation,
    VisualElement element,
    VisualMutationType mutationType)
{
    try {
        struct ElementInfo {
            std::wstring type;
            std::wstring name;
            unsigned long long parent;
        };
        static std::unordered_map<unsigned long long, ElementInfo> g_elements;

        // 日志（基础）
        {
            wchar_t buf[512];
            swprintf_s(buf, L"[OnVisualTreeChange] mutation=%d type=%s handle=%llu parent=%llu name=%s \n",
                static_cast<int>(mutationType),
                element.Type ? element.Type : L"(null)",
                static_cast<unsigned long long>(element.Handle),
                static_cast<unsigned long long>(relation.Parent),
                element.Name ? element.Name : L"(null)");
            OutputDebugStringW(buf);
            LOG_DEBUG("[VisualTreeWatcher.cpp][OnVisualTreeChange]", buf);
        }

        auto toKey = [](unsigned long long h) -> unsigned long long {
            return static_cast<unsigned long long>(h);
            };

        auto matches_candidate = [](const ElementInfo& e) -> bool {
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"CommandBarControlRootGrid") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"NavigationBarControlGrid") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"DetailsViewControlRootGrid") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"GalleryRootGrid") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"HomeViewRootGrid") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"LayoutRoot") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Grid" && e.name == L"PART_LayoutRoot") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.Border" && e.name == L"BorderElement") return true;
            if (e.name == L"FileExplorerCommandBar") return true;
            if (e.name == L"FileExplorerSearchBox") return true;
            if (e.type == L"Microsoft.UI.Xaml.Shapes.Path" && e.name == L"SelectedBackgroundPath") return true;
            if (e.type == L"Microsoft.UI.Xaml.Controls.TextBox") return true;
            if (e.type == L"Windows.UI.Xaml.Controls.Grid" && e.name == L"CommandBarControlRootGrid") return true;
            if (e.type == L"Windows.UI.Xaml.Shapes.Path" && e.name == L"SelectedBackgroundPath") return true;
            if (e.type == L"Windows.UI.Xaml.Controls.Grid" && e.name == L"NavigationBarControlGrid") return true;
            if (e.type == L"Windows.UI.Xaml.Controls.Grid" && e.name == L"PART_LayoutRoot") return true;
            if (e.type == L"Windows.UI.Xaml.Controls.Grid" && e.name == L"LayoutRoot") return true;
            return false;
            };

        if (mutationType == Add) {
            ElementInfo info;
            info.type = element.Type ? element.Type : L"";
            info.name = element.Name ? element.Name : L"";
            info.parent = static_cast<unsigned long long>(relation.Parent);

            unsigned long long key = toKey(static_cast<unsigned long long>(element.Handle));
            g_elements[key] = info;

            if (matches_candidate(info)) {
                wchar_t foundBuf[256];
                swprintf_s(foundBuf, L"[OnVisualTreeChange] Candidate matched handle=%llu -> attempt set background transparent\n", key);
                OutputDebugStringW(foundBuf);

                auto diag = m_XamlDiagnostics.try_as<IXamlDiagnostics>();
                if (!diag) {
                    OutputDebugStringW(L"[OnVisualTreeChange] IXamlDiagnostics not available\n");
                }
                else {
                    IVisualTreeService* pVisualService = nullptr;
                    HRESULT hrService = diag->QueryInterface(__uuidof(IVisualTreeService), (void**)&pVisualService);
                    if (FAILED(hrService) || !pVisualService) {
                        wchar_t err2[128];
                        swprintf_s(err2, L"[OnVisualTreeChange] QueryInterface(IVisualTreeService) failed hr=0x%08X handle=%llu\n",
                            hrService, key);
                        OutputDebugStringW(err2);
                    }
                    else {
                        IVisualTreeService3* pVisualService3 = nullptr;
                        HRESULT hrService3 = pVisualService->QueryInterface(__uuidof(IVisualTreeService3), (void**)&pVisualService3);
                        if (FAILED(hrService3) || !pVisualService3) {
                            wchar_t err2[128];
                            swprintf_s(err2, L"[OnVisualTreeChange] QueryInterface(IVisualTreeService3) failed hr=0x%08X handle=%llu\n",
                                hrService3, key);
                            OutputDebugStringW(err2);
                            pVisualService->Release();
                        }
                        else {
                            // --- 决策：先改 local，再尝试在 ControlTemplate 中寻找可修改的子元素并修改其 Background/Fill ---
                            const wchar_t* propName = nullptr;
                            if (info.type == L"Microsoft.UI.Xaml.Controls.CommandBar") {
                                propName = L"Microsoft.UI.Xaml.Controls.CommandBar.Background";
                            }
                            else if (info.type == L"Microsoft.UI.Xaml.Controls.AutoSuggestBox") {
                                propName = L"Microsoft.UI.Xaml.Controls.AutoSuggestBox.Background";
                            }
                            else if (info.type == L"Microsoft.UI.Xaml.Controls.TextBox") {
                                propName = L"Microsoft.UI.Xaml.Controls.TextBox.Background";
                            }
                            
                            else if (info.type == L"Microsoft.UI.Xaml.Shapes.Path") {
                                propName = L"Microsoft.UI.Xaml.Shapes.Path.RasterizationScale";
                                wchar_t buf2[1024];
                                unsigned int propIndex = 0;
                                HRESULT hrIndex = pVisualService3->GetPropertyIndex(key, propName, &propIndex);
                                if (SUCCEEDED(hrIndex)) {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) hr=0x%08X index=%u handle=%llu\n",
                                        propName, hrIndex, propIndex, static_cast<unsigned long long>(key));
                                    OutputDebugStringW(buf2);

                                    const struct { const wchar_t* type; const wchar_t* val; } candidates[] = {
                                        {L"System.Double", L"0.0"},
                                        {L"System.Double", L"0"},
                                        {L"System.Double, mscorlib", L"0"},
                                        {L"System.Double, System.Private.CoreLib", L"0"},
                                        {L"Windows.Foundation.IReference`1[System.Double]", L"0"},
                                        {L"Windows.Foundation.IReference`1[System.Double], Windows.Foundation", L"0"},
                                        {L"double", L"0"},
                                        {L"Double", L"0"},
                                        {L"System.Double", L"0.000000"},
                                        {L"System.Double", L"0E0"}
                                    };

                                    InstanceHandle valueHandle = 0;
                                    HRESULT hrCreate = E_FAIL;
                                    for (const auto& c : candidates) {
                                        // 清理上一次的 handle（如果 CreateInstance 在失败时返回非零句柄，确保不泄漏）
                                        if (valueHandle) {
                                            // 如果有释放接口应在此释放；无法确定接口时仅置0以避免误用
                                            valueHandle = 0;
                                        }

                                        BSTR typeB = SysAllocString(c.type);
                                        BSTR valB = SysAllocString(c.val);
                                        hrCreate = pVisualService3->CreateInstance(typeB, valB, &valueHandle);
                                        swprintf_s(buf2, L"[OnVisualTreeChange] Try CreateInstance(%s, %s) hr=0x%08X handle=%llu\n",
                                            c.type, c.val, hrCreate, static_cast<unsigned long long>(valueHandle));
                                        OutputDebugStringW(buf2);
                                        SysFreeString(typeB);
                                        SysFreeString(valB);

                                        if (SUCCEEDED(hrCreate) && valueHandle) break;
                                    }

                                    // 如果简单的 CreateInstance 失败，尝试使用字符串封箱（有些实现接受 "System.String" + 数字字符串）
                                    if (!(SUCCEEDED(hrCreate) && valueHandle)) {
                                        BSTR typeB = SysAllocString(L"System.String");
                                        BSTR valB = SysAllocString(L"0");
                                        InstanceHandle strHandle = 0;
                                        HRESULT hrStr = pVisualService3->CreateInstance(typeB, valB, &strHandle);
                                        swprintf_s(buf2, L"[OnVisualTreeChange] Try CreateInstance(System.String, \"0\") hr=0x%08X handle=%llu\n",
                                            hrStr, static_cast<unsigned long long>(strHandle));
                                        OutputDebugStringW(buf2);
                                        SysFreeString(typeB);
                                        SysFreeString(valB);

                                        if (SUCCEEDED(hrStr) && strHandle) {
                                            // 有些运行时会自动将 string -> 相应类型转换（尝试）
                                            valueHandle = strHandle;
                                            hrCreate = hrStr;
                                        }
                                    }

                                    if (SUCCEEDED(hrCreate) && valueHandle) {
                                        // 成功创建实例，设置本地值（注意：避免使用 0 作为 instance handle）
                                        HRESULT hrSetLocal = pVisualService3->SetProperty(key, valueHandle, propIndex);
                                        swprintf_s(buf2, L"[OnVisualTreeChange] SetProperty(LOCAL %s) hr=0x%08X handle=%llu\n",
                                            propName, hrSetLocal, static_cast<unsigned long long>(key));
                                        OutputDebugStringW(buf2);

                                        // 如果存在释放接口，这里应释放 valueHandle（接口未知时跳过）
                                    }
                                    else {
                                        swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(all attempts) failed hr=0x%08X handle=%llu - cannot set RasterizationScale\n",
                                            hrCreate, static_cast<unsigned long long>(valueHandle));
                                        OutputDebugStringW(buf2);
                                    }
                                }
                                else {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) failed hr=0x%08X\n",
                                        propName, hrIndex);
                                    OutputDebugStringW(buf2);
                                }
                                propName = L"Microsoft.UI.Xaml.Shapes.Path.Fill";
                            } else if(info.type == L"Microsoft.UI.Xaml.Controls.Border")
                            {
                                propName = L"Microsoft.UI.Xaml.Controls.Border.BorderBrush";
                            }

                            else {
                                propName = L"Microsoft.UI.Xaml.Controls.Grid.BorderBrush";
                                wchar_t buf2[1024];
                                unsigned int propIndex = 0;
                                HRESULT hrIndex = pVisualService3->GetPropertyIndex(key, propName, &propIndex);
                                if (SUCCEEDED(hrIndex)) {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) hr=0x%08X index=%u handle=%llu\n",
                                        propName, hrIndex, propIndex, key);
                                    OutputDebugStringW(buf2);

                                    // 创建透明 Brush
                                    const wchar_t* brushType =
                                        (info.type.rfind(L"Microsoft.UI.Xaml", 0) == 0) ?
                                        L"Microsoft.UI.Xaml.Media.SolidColorBrush" :
                                        L"Windows.UI.Xaml.Media.SolidColorBrush";
                                    BSTR brushTypeBstr = SysAllocString(brushType);
                                    BSTR transparentColorBstr = SysAllocString(L"Transparent");
                                    InstanceHandle brushHandle = 0;
                                    HRESULT hrCreate = pVisualService3->CreateInstance(brushTypeBstr, transparentColorBstr, &brushHandle);
                                    SysFreeString(brushTypeBstr);

                                    swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(%s, Transparent) hr=0x%08X handle=%llu\n",
                                        brushType, hrCreate, static_cast<unsigned long long>(brushHandle));
                                    OutputDebugStringW(buf2);

                                    if (SUCCEEDED(hrCreate) && brushHandle) {
                                        // 修改元素本地值
                                        HRESULT hrSetLocal = pVisualService3->SetProperty(key, brushHandle, propIndex);
                                        swprintf_s(buf2, L"[OnVisualTreeChange] SetProperty(LOCAL %s) hr=0x%08X handle=%llu\n",
                                            propName, hrSetLocal, key);
                                        OutputDebugStringW(buf2);
                                    }
                                    else {
                                        swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(%s) failed hr=0x%08X\n",
                                            propName, hrIndex);
                                        OutputDebugStringW(buf2);
                                    }
                                }
                                else {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) failed hr=0x%08X\n",
                                        propName, hrIndex);
                                    OutputDebugStringW(buf2);
                                }

                                propName = L"Microsoft.UI.Xaml.Controls.Grid.Background";
                            }

                            wchar_t buf2[1024];
                            unsigned int propIndex = 0;
                            HRESULT hrIndex = pVisualService3->GetPropertyIndex(key, propName, &propIndex);
                            if (SUCCEEDED(hrIndex)) {
                                swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) hr=0x%08X index=%u handle=%llu\n",
                                    propName, hrIndex, propIndex, key);
                                OutputDebugStringW(buf2);

                                // 创建透明 Brush
                                const wchar_t* brushType =
                                    (info.type.rfind(L"Microsoft.UI.Xaml", 0) == 0) ?
                                    L"Microsoft.UI.Xaml.Media.SolidColorBrush" :
                                    L"Windows.UI.Xaml.Media.SolidColorBrush";
                                BSTR brushTypeBstr = SysAllocString(brushType);
                                BSTR transparentColorBstr = SysAllocString(L"Transparent");
                                InstanceHandle brushHandle = 0;
                                HRESULT hrCreate = pVisualService3->CreateInstance(brushTypeBstr, transparentColorBstr, &brushHandle);
                                SysFreeString(brushTypeBstr);

                                swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(%s, Transparent) hr=0x%08X handle=%llu\n",
                                    brushType, hrCreate, static_cast<unsigned long long>(brushHandle));
                                OutputDebugStringW(buf2);

                                if (SUCCEEDED(hrCreate) && brushHandle) {
                                    // 修改元素本地值
                                    HRESULT hrSetLocal = pVisualService3->SetProperty(key, brushHandle, propIndex);
                                    swprintf_s(buf2, L"[OnVisualTreeChange] SetProperty(LOCAL %s) hr=0x%08X handle=%llu\n",
                                        propName, hrSetLocal, key);
                                    OutputDebugStringW(buf2);
                                } else {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(%s) failed hr=0x%08X\n",
                                        propName, hrIndex);
                                    OutputDebugStringW(buf2);
                                }
                            }
                            else {
                                swprintf_s(buf2, L"[OnVisualTreeChange] GetPropertyIndex(%s) failed hr=0x%08X\n",
                                    propName, hrIndex);
                                OutputDebugStringW(buf2);
                            }

                            pVisualService3->Release();
                            pVisualService->Release();
                        } // end if pVisualService3 ok
                    } // end else pVisualService ok
                } // end else diag ok
            } // end if matches_candidate
        } // end if Add
        else if (mutationType == Remove) {
            unsigned long long key = toKey(static_cast<unsigned long long>(element.Handle));
            auto it = g_elements.find(key);
            if (it != g_elements.end()) {
                g_elements.erase(it);
                wchar_t buf[128];
                swprintf_s(buf, L"[OnVisualTreeChange] Removed handle=%llu from snapshot\n", key);
                OutputDebugStringW(buf);
            }
        }
    }
    catch (...) {
        OutputDebugStringW(L"[OnVisualTreeChange] exception caught\n");
    }
    return S_OK;
}

HRESULT VisualTreeWatcher::OnElementStateChanged(InstanceHandle element, VisualElementState elementState, LPCWSTR context) noexcept
{
    return S_OK;
}

winrt::Windows::UI::Xaml::FrameworkElement VisualTreeWatcher::FindParentByName(
    std::wstring_view name,
    winrt::Windows::UI::Xaml::DependencyObject startElement
)
{
    winrt::Windows::UI::Xaml::DependencyObject current = startElement;
    while (current) {
        auto parent = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
        if (!parent) break;

        if (auto fe = parent.try_as<winrt::Windows::UI::Xaml::FrameworkElement>()) {
            if (fe.Name() == name) {
                return fe;
            }
        }

        current = parent;
    }
    return nullptr;
}