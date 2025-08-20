#pragma once
#include <Windows.h>
#include <atlbase.h>
#include <atlcom.h>
#include <UIAutomation.h>
#include <vector>
#include <string>

/// 定义XAML元素扫描类
class XamlTreeScanner {
public:
    XamlTreeScanner();
    ~XamlTreeScanner();

    /// 扫描当前进程中的所有XAML元素，并按UI树层级顺序排序
    void ScanAllElements();

    /// 获取扫描到的元素集合 (自动化元素接口指针列表)
    std::vector<CComPtr<IUIAutomationElement>> GetElements() const;

    /// 获取元素的Name属性
    static std::wstring GetElementName(IUIAutomationElement* element);

    /// 设置元素文本 (适用于支持ValuePattern的文本控件)
     void SetText(IUIAutomationElement* element, const std::wstring& text) const;

    /// 设置元素可见性 (对于窗口使用WindowPattern最大化/最小化)
    void SetVisibility(IUIAutomationElement* element, bool visible) const;

    /// 设置元素颜色 (若XAML元素支持，可在此扩展)
    void SetColor(IUIAutomationElement* element, uint32_t color) const;

    IUIAutomation* GetAutomation() const {
        return m_automation;
    }

private:
    CComPtr<IUIAutomation> m_automation;
    std::vector<CComPtr<IUIAutomationElement>> m_elements;
};
