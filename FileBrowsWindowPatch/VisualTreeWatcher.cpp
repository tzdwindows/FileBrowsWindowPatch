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
                                propName = L"Microsoft.UI.Xaml.Shapes.Path.Fill^";
                            }
                            else {
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
                                    // 1) 修改元素本地值
                                    HRESULT hrSetLocal = pVisualService3->SetProperty(key, brushHandle, propIndex);
                                    swprintf_s(buf2, L"[OnVisualTreeChange] SetProperty(LOCAL %s) hr=0x%08X handle=%llu\n",
                                        propName, hrSetLocal, key);
                                    OutputDebugStringW(buf2);

                                    // 2) 尝试直接修改 ControlTemplate 内部实际渲染背景的元素（绕过 Style.Setter）
                                    // 常见 TextBox Template 中用于绘制背景的类型/Name（尝试列表）
                                    const wchar_t* targetElementTypes[] = {
                                        L"Microsoft.UI.Xaml.Controls.Border",
                                        L"Windows.UI.Xaml.Controls.Border",
                                        L"Border",
                                        L"Microsoft.UI.Xaml.Shapes.Rectangle",
                                        L"Windows.UI.Xaml.Shapes.Rectangle",
                                        L"Rectangle",
                                        L"Microsoft.UI.Xaml.Shapes.Path",
                                        L"Path"
                                    };
                                    // 这些元素上可能使用的属性名（优先 Background/Fill）
                                    const wchar_t* bgPropCandidates[] = {
                                        L"Microsoft.UI.Xaml.Controls.Border.Background",
                                        L"Windows.UI.Xaml.Controls.Border.Background",
                                        L"Border.Background",
                                        L"Microsoft.UI.Xaml.Shapes.Path.Fill",
                                        L"Windows.UI.Xaml.Shapes.Path.Fill",
                                        L"Path.Fill",
                                        L"Microsoft.UI.Xaml.Shapes.Rectangle.Fill",
                                        L"Windows.UI.Xaml.Shapes.Rectangle.Fill",
                                        L"Rectangle.Fill",
                                        L"Background",
                                        L"Fill"
                                    };

                                    // 查找 ControlTemplate/Template，并在 Template.VisualTree 中递归查找可设置 Background/Fill 的元素
                                    const wchar_t* templateCandidates[] = {
                                        L"Microsoft.UI.Xaml.Controls.Control.Template",
                                        L"Windows.UI.Xaml.Controls.Control.Template",
                                        L"Control.Template",
                                        L"Template"
                                    };

                                    auto trySetBgOnHandle = [&](InstanceHandle h) -> bool {
                                        if (!h) return false;
                                        // 遍历 bgPropCandidates，若有可用属性则尝试设置
                                        for (const wchar_t* bgProp : bgPropCandidates) {
                                            unsigned int idx = 0;
                                            HRESULT hrIdx = pVisualService3->GetPropertyIndex(h, bgProp, &idx);
                                            if (SUCCEEDED(hrIdx)) {
                                                HRESULT hrSet = pVisualService3->SetProperty(h, brushHandle, idx);
                                                swprintf_s(buf2, L"[OnVisualTreeChange] Try set %s on handle=%llu hr=0x%08X\n", bgProp, static_cast<unsigned long long>(h), hrSet);
                                                OutputDebugStringW(buf2);
                                                if (SUCCEEDED(hrSet)) return true;
                                            }
                                        }
                                        return false;
                                        };

                                    // 递归搜索函数：对 rootHandle 尝试 set，若未成功则尝试获取其 "children"-like 属性并对每个子项递归
                                    std::function<bool(InstanceHandle, int)> recurseSet;
                                    recurseSet = [&](InstanceHandle rootHandle, int depth) -> bool {
                                        if (!rootHandle || depth > 12) return false;
                                        // 1) 直接尝试在该节点上设置 Background/Fill
                                        if (trySetBgOnHandle(rootHandle)) {
                                            swprintf_s(buf2, L"[OnVisualTreeChange] Successfully set bg on template element handle=%llu\n", static_cast<unsigned long long>(rootHandle));
                                            OutputDebugStringW(buf2);
                                            return true;
                                        }

                                        // 2) 尝试从该节点获取直接可能包含子元素的属性（尝试多种常见属性名）
                                        const wchar_t* childrenCandidates[] = {
                                            L"Microsoft.UI.Xaml.FrameworkElement.Content",
                                            L"Content",
                                            L"Child",
                                            L"Children",
                                            L"Microsoft.UI.Xaml.Controls.Panel.Children",
                                            L"VisualTree",
                                            L"TemplateRoot",
                                            L"Root",
                                            L"Target",
                                            L"Element",
                                            L"Collection",
                                            L"Items"
                                        };

                                        for (const wchar_t* cc : childrenCandidates) {
                                            unsigned int childPropIdx = 0;
                                            HRESULT hrChildIdx = pVisualService3->GetPropertyIndex(rootHandle, cc, &childPropIdx);
                                            if (SUCCEEDED(hrChildIdx)) {
                                                InstanceHandle childHandle = 0;
                                                HRESULT hrGetChild = pVisualService3->GetProperty(rootHandle, childPropIdx, &childHandle);
                                                swprintf_s(buf2, L"[OnVisualTreeChange] Try children candidate (%s) on handle=%llu hr=0x%08X childHandle=%llu\n",
                                                    cc, static_cast<unsigned long long>(rootHandle), hrGetChild, static_cast<unsigned long long>(childHandle));
                                                OutputDebugStringW(buf2);

                                                if (SUCCEEDED(hrGetChild) && childHandle) {
                                                    // 有时 childHandle 是集合/工厂而非单个元素；尝试直接设置/递归（若是集合则尝试直接递归）
                                                    if (recurseSet(childHandle, depth + 1)) return true;
                                                }
                                            }
                                        }

                                        // 3) 若都没有，尝试通过已保存快照中的父链向上查找 resources（之前的实现）
                                        return false;
                                        }; // end recurseSet

                                    // 获取 Control.Template（若存在）
                                    bool modifiedInTemplate = false;
                                    for (const wchar_t* tc : templateCandidates) {
                                        unsigned int tmpIdx = 0;
                                        HRESULT hrTmpIdx = pVisualService3->GetPropertyIndex(key, tc, &tmpIdx);
                                        if (SUCCEEDED(hrTmpIdx)) {
                                            InstanceHandle templateHandle = 0;
                                            HRESULT hrGetTmp = pVisualService3->GetProperty(key, tmpIdx, &templateHandle);
                                            swprintf_s(buf2, L"[OnVisualTreeChange] Try Template candidate (%s) hr=0x%08X templateHandle=%llu\n",
                                                tc, hrGetTmp, static_cast<unsigned long long>(templateHandle));
                                            OutputDebugStringW(buf2);
                                            if (SUCCEEDED(hrGetTmp) && templateHandle) {
                                                // 有些 template 直接包含 VisualTree/Root，尝试递归在其中查找可设置节点
                                                if (recurseSet(templateHandle, 0)) {
                                                    modifiedInTemplate = true;
                                                    break;
                                                }
                                                // 若 templateHandle 本身不是可操作的 visual tree 节点，尝试获取其 VisualTree 属性
                                                unsigned int vtIdx = 0;
                                                const wchar_t* visualTreeCandidates[] = {
                                                    L"Microsoft.UI.Xaml.Controls.ControlTemplate.VisualTree",
                                                    L"VisualTree",
                                                    L"Template.VisualTree",
                                                    L"ControlTemplate.VisualTree"
                                                };
                                                for (const wchar_t* vtc : visualTreeCandidates) {
                                                    HRESULT hrVtIdx = pVisualService3->GetPropertyIndex(templateHandle, vtc, &vtIdx);
                                                    if (SUCCEEDED(hrVtIdx)) {
                                                        InstanceHandle vtHandle = 0;
                                                        HRESULT hrGetVt = pVisualService3->GetProperty(templateHandle, vtIdx, &vtHandle);
                                                        swprintf_s(buf2, L"[OnVisualTreeChange] Try Template.VisualTree (%s) hr=0x%08X vtHandle=%llu\n",
                                                            vtc, hrGetVt, static_cast<unsigned long long>(vtHandle));
                                                        OutputDebugStringW(buf2);
                                                        if (SUCCEEDED(hrGetVt) && vtHandle) {
                                                            if (recurseSet(vtHandle, 0)) {
                                                                modifiedInTemplate = true;
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                                if (modifiedInTemplate) break;
                                            }
                                        }
                                    } // end for templateCandidates

                                    if (modifiedInTemplate) {
                                        swprintf_s(buf2, L"[OnVisualTreeChange] Modified background via ControlTemplate traversal for handle=%llu\n", key);
                                        OutputDebugStringW(buf2);
                                    }
                                    else {
                                        swprintf_s(buf2, L"[OnVisualTreeChange] 未能在 ControlTemplate 中找到可修改的元素（许多运行时不允许通过 diagnostics 修改模板/默认样式）。将继续尝试 Resources/祖先查找。\n");
                                        OutputDebugStringW(buf2);

                                        // 如果上面都失败，继续尝试祖先 Resources（和之前实现类似）
                                        unsigned long long parentKey = static_cast<unsigned long long>(relation.Parent);
                                        unsigned long long current = parentKey;
                                        bool modifiedFromResources = false;
                                        for (int depth = 0; depth < 20 && current != 0; ++depth) {
                                            unsigned int resPropIdx = 0;
                                            const wchar_t* resourcesCandidates[] = {
                                                L"Microsoft.UI.Xaml.FrameworkElement.Resources",
                                                L"Windows.UI.Xaml.FrameworkElement.Resources",
                                                L"FrameworkElement.Resources",
                                                L"Resources",
                                                L"Application.Resources",
                                                L"Microsoft.UI.Xaml.Application.Resources",
                                                L"Windows.UI.Xaml.Application.Resources"
                                            };
                                            InstanceHandle resourcesHandle = 0;
                                            bool foundResources = false;
                                            for (const wchar_t* rc : resourcesCandidates) {
                                                HRESULT hrResIdx = pVisualService3->GetPropertyIndex(current, rc, &resPropIdx);
                                                if (SUCCEEDED(hrResIdx)) {
                                                    HRESULT hrGetRes = pVisualService3->GetProperty(current, resPropIdx, &resourcesHandle);
                                                    swprintf_s(buf2, L"[OnVisualTreeChange] ancestor handle=%llu try Resources candidate (%s) hr=0x%08X resourcesHandle=%llu\n",
                                                        current, rc, hrGetRes, static_cast<unsigned long long>(resourcesHandle));
                                                    OutputDebugStringW(buf2);
                                                    if (SUCCEEDED(hrGetRes) && resourcesHandle) {
                                                        foundResources = true;
                                                        break;
                                                    }
                                                }
                                            }
                                            if (foundResources && resourcesHandle) {
                                                const wchar_t* candidateStyleKeys[] = {
                                                    L"TextBoxStyle",
                                                    L"TextBoxDefaultStyle",
                                                    L"Microsoft.UI.Xaml.Controls.TextBoxStyle",
                                                    L"TextBoxThemeStyle",
                                                    L"DefaultTextBoxStyle",
                                                    L"TextBox"
                                                };
                                                for (const wchar_t* keyCandidate : candidateStyleKeys) {
                                                    unsigned int resIndex = 0;
                                                    HRESULT hrResIndex = pVisualService3->GetPropertyIndex(resourcesHandle, keyCandidate, &resIndex);
                                                    if (SUCCEEDED(hrResIndex)) {
                                                        InstanceHandle foundStyleHandle = 0;
                                                        HRESULT hrGetFound = pVisualService3->GetProperty(resourcesHandle, resIndex, &foundStyleHandle);
                                                        swprintf_s(buf2, L"[OnVisualTreeChange] Try resource key (%s) hr=0x%08X foundStyleHandle=%llu\n",
                                                            keyCandidate, hrGetFound, static_cast<unsigned long long>(foundStyleHandle));
                                                        OutputDebugStringW(buf2);
                                                        if (SUCCEEDED(hrGetFound) && foundStyleHandle) {
                                                            // 在该 style 上尝试直接设置 Background（如果可用）
                                                            unsigned int foundStylePropIdx = 0;
                                                            HRESULT hrFspi = pVisualService3->GetPropertyIndex(foundStyleHandle, propName, &foundStylePropIdx);
                                                            if (SUCCEEDED(hrFspi)) {
                                                                HRESULT hrSetFound = pVisualService3->SetProperty(foundStyleHandle, brushHandle, foundStylePropIdx);
                                                                swprintf_s(buf2, L"[OnVisualTreeChange] SetProperty(RESOURCE STYLE %s) hr=0x%08X styleHandle=%llu\n",
                                                                    propName, hrSetFound, static_cast<unsigned long long>(foundStyleHandle));
                                                                OutputDebugStringW(buf2);
                                                                modifiedFromResources = SUCCEEDED(hrSetFound);
                                                                if (modifiedFromResources) break;
                                                            }
                                                            else {
                                                                // 若 style 未直接暴露属性，尝试在 style 的 Template 上执行模板遍历
                                                                unsigned int tplIdx = 0;
                                                                const wchar_t* foundTemplateCandidates[] = {
                                                                    L"Microsoft.UI.Xaml.Controls.Control.Template",
                                                                    L"Windows.UI.Xaml.Controls.Control.Template",
                                                                    L"Control.Template",
                                                                    L"Template"
                                                                };
                                                                for (const wchar_t* ftc : foundTemplateCandidates) {
                                                                    HRESULT hrFtIdx = pVisualService3->GetPropertyIndex(foundStyleHandle, ftc, &tplIdx);
                                                                    if (SUCCEEDED(hrFtIdx)) {
                                                                        InstanceHandle foundTpl = 0;
                                                                        HRESULT hrGetTpl = pVisualService3->GetProperty(foundStyleHandle, tplIdx, &foundTpl);
                                                                        swprintf_s(buf2, L"[OnVisualTreeChange] Found style.Template candidate (%s) hr=0x%08X foundTpl=%llu\n",
                                                                            ftc, hrGetTpl, static_cast<unsigned long long>(foundTpl));
                                                                        OutputDebugStringW(buf2);
                                                                        if (SUCCEEDED(hrGetTpl) && foundTpl) {
                                                                            if (recurseSet(foundTpl, 0)) {
                                                                                modifiedFromResources = true;
                                                                                break;
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                                if (modifiedFromResources) break;
                                                            }
                                                        }
                                                    }
                                                }
                                                if (modifiedFromResources) break;
                                            }

                                            auto it = g_elements.find(current);
                                            if (it == g_elements.end()) break;
                                            current = it->second.parent;
                                        } // end ancestor loop

                                        if (!modifiedFromResources) {
                                            swprintf_s(buf2, L"[OnVisualTreeChange] 在祖先 Resources 或 Template 中未能修改 TextBox 的样式（可能由主题/默认样式或不可变运行时控制）。\n");
                                            OutputDebugStringW(buf2);
                                        }
                                    } // end modifiedInTemplate else
                                } // end if brush created
                                else {
                                    swprintf_s(buf2, L"[OnVisualTreeChange] CreateInstance(Brush) 失败 hr=0x%08X\n", hrCreate);
                                    OutputDebugStringW(buf2);
                                }

                                SysFreeString(transparentColorBstr);
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