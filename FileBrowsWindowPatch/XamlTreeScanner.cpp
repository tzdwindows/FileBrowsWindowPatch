#include  "pch.h"

#include "XamlTreeScanner.h"
#include <atlbase.h>
#include <comdef.h>

XamlTreeScanner::XamlTreeScanner() {
    // ���� UI Automation ʵ�� (ʹ�� UIAutomation8)
    m_automation.CoCreateInstance(CLSID_CUIAutomation8);
}

XamlTreeScanner::~XamlTreeScanner() {
    // ����
    m_elements.clear();
    m_automation.Release();
    CoUninitialize();
}

void XamlTreeScanner::ScanAllElements() {
    m_elements.clear();
    if (!m_automation) return;

    CComPtr<IUIAutomationElement> root;
    // ��ȡ�����Ԫ��
    m_automation->GetRootElement(&root);
    if (!root) return;

    // ������� ID ����
    DWORD pid = GetCurrentProcessId();
    CComVariant varPid((int)pid);
    CComPtr<IUIAutomationCondition> cond;
    m_automation->CreatePropertyCondition(UIA_ProcessIdPropertyId, varPid, &cond);

    // ���ҵ�ǰ���̵�����Ԫ�� (�ݹ�����)
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
    // ��ȡ Name ����
    element->get_CurrentName(&bstrName);
    if (bstrName) {
        return std::wstring(bstrName, SysStringLen(bstrName));
    }
    return L"";
}

void XamlTreeScanner::SetText(IUIAutomationElement* element, const std::wstring& text) const {
    if (!element) return;
    CComPtr<IUIAutomationValuePattern> valPattern;
    // ��ȡ ValuePattern �ӿ�
    element->GetCurrentPattern(UIA_ValuePatternId, (IUnknown**)&valPattern);
    if (valPattern) {
        // �����ı�ֵ
        CComBSTR bstrText(text.c_str());
        valPattern->SetValue(bstrText);
    }
}

void XamlTreeScanner::SetVisibility(IUIAutomationElement* element, bool visible) const {
    if (!element) return;
    CComPtr<IUIAutomationWindowPattern> winPattern;
    // ��ȡ WindowPattern �ӿ� (�������ڴ���Ԫ��)
    element->GetCurrentPattern(UIA_WindowPatternId, (IUnknown**)&winPattern);
    if (winPattern) {
        // �л����ڿɼ�״̬ (����/��С��)
        if (visible) {
            winPattern->SetWindowVisualState(WindowVisualState_Normal);
        }
        else {
            winPattern->SetWindowVisualState(WindowVisualState_Minimized);
        }
    }
}

void XamlTreeScanner::SetColor(IUIAutomationElement* /*element*/, uint32_t /*color*/) const {
    // UI Automation ��ֱ��֧����ɫ���ã�
    // �����Ҫ����ʹ�� WinRT XAML �ӿڣ��� XamlDirect����Ԫ�ؽ����Զ��������
    
}
