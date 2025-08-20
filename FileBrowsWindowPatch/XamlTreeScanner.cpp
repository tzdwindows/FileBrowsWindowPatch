#include  "pch.h"

#include "XamlTreeScanner.h"
#include <atlbase.h>
#include <comdef.h>

XamlTreeScanner::XamlTreeScanner() {
    // 创建 UI Automation 实例 (使用 UIAutomation8)
    m_automation.CoCreateInstance(CLSID_CUIAutomation8);
}

XamlTreeScanner::~XamlTreeScanner() {
    // 清理
    m_elements.clear();
    m_automation.Release();
    CoUninitialize();
}

void XamlTreeScanner::ScanAllElements() {
    m_elements.clear();
    if (!m_automation) return;

    CComPtr<IUIAutomationElement> root;
    // 获取桌面根元素
    m_automation->GetRootElement(&root);
    if (!root) return;

    // 构造进程 ID 条件
    DWORD pid = GetCurrentProcessId();
    CComVariant varPid((int)pid);
    CComPtr<IUIAutomationCondition> cond;
    m_automation->CreatePropertyCondition(UIA_ProcessIdPropertyId, varPid, &cond);

    // 查找当前进程的所有元素 (递归子树)
    CComPtr<IUIAutomationElementArray> arr;
    root->FindAll(TreeScope_Descendants, cond, &arr);
    if (!arr) return;

    int length = 0;
    arr->get_Length(&length);
    for (int i = 0; i < length; i++) {
        CComPtr<IUIAutomationElement> elem;
        arr->GetElement(i, &elem);
        if (elem) {
            m_elements.push_back(elem);
        }
    }
}

std::vector<CComPtr<IUIAutomationElement>> XamlTreeScanner::GetElements() const {
    return m_elements;
}

std::wstring XamlTreeScanner::GetElementName(IUIAutomationElement* element) {
    if (!element) return L"";
    CComBSTR bstrName;
    // 获取 Name 属性
    element->get_CurrentName(&bstrName);
    if (bstrName) {
        return std::wstring(bstrName, SysStringLen(bstrName));
    }
    return L"";
}

void XamlTreeScanner::SetText(IUIAutomationElement* element, const std::wstring& text) const {
    if (!element) return;
    CComPtr<IUIAutomationValuePattern> valPattern;
    // 获取 ValuePattern 接口
    element->GetCurrentPattern(UIA_ValuePatternId, (IUnknown**)&valPattern);
    if (valPattern) {
        // 设置文本值
        CComBSTR bstrText(text.c_str());
        valPattern->SetValue(bstrText);
    }
}

void XamlTreeScanner::SetVisibility(IUIAutomationElement* element, bool visible) const {
    if (!element) return;
    CComPtr<IUIAutomationWindowPattern> winPattern;
    // 获取 WindowPattern 接口 (仅适用于窗口元素)
    element->GetCurrentPattern(UIA_WindowPatternId, (IUnknown**)&winPattern);
    if (winPattern) {
        // 切换窗口可见状态 (正常/最小化)
        if (visible) {
            winPattern->SetWindowVisualState(WindowVisualState_Normal);
        }
        else {
            winPattern->SetWindowVisualState(WindowVisualState_Minimized);
        }
    }
}

void XamlTreeScanner::SetColor(IUIAutomationElement* /*element*/, uint32_t /*color*/) const {
    // UI Automation 不直接支持颜色设置，
    // 如果需要，可使用 WinRT XAML 接口（如 XamlDirect）对元素进行自定义操作。
    
}
