#pragma once
#include <Windows.h>
#include <atlbase.h>
#include <atlcom.h>
#include <UIAutomation.h>
#include <vector>
#include <string>

/// ����XAMLԪ��ɨ����
class XamlTreeScanner {
public:
    XamlTreeScanner();
    ~XamlTreeScanner();

    /// ɨ�赱ǰ�����е�����XAMLԪ�أ�����UI���㼶˳������
    void ScanAllElements();

    /// ��ȡɨ�赽��Ԫ�ؼ��� (�Զ���Ԫ�ؽӿ�ָ���б�)
    std::vector<CComPtr<IUIAutomationElement>> GetElements() const;

    /// ��ȡԪ�ص�Name����
    static std::wstring GetElementName(IUIAutomationElement* element);

    /// ����Ԫ���ı� (������֧��ValuePattern���ı��ؼ�)
     void SetText(IUIAutomationElement* element, const std::wstring& text) const;

    /// ����Ԫ�ؿɼ��� (���ڴ���ʹ��WindowPattern���/��С��)
    void SetVisibility(IUIAutomationElement* element, bool visible) const;

    /// ����Ԫ����ɫ (��XAMLԪ��֧�֣����ڴ���չ)
    void SetColor(IUIAutomationElement* element, uint32_t color) const;

    IUIAutomation* GetAutomation() const {
        return m_automation;
    }

private:
    CComPtr<IUIAutomation> m_automation;
    std::vector<CComPtr<IUIAutomationElement>> m_elements;
};
