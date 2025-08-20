#include "pch.h"
#include "ElementFinder.h"


#include <winrt/impl/Windows.UI.Xaml.2.h>
#include <winrt/impl/windows.ui.xaml.media.2.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <algorithm>
#include <stack>

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Media;

FrameworkElement ElementFinder::FindMatchingElement(
    FrameworkElement root,
    const std::wstring& target,
    PCWSTR fallbackClassName
) {
    if (!root) {
        return nullptr;
    }

    auto matcher = ParseTargetString(target);

    // ʹ������������������Ӿ���
    std::stack<FrameworkElement> stack;
    stack.push(root);

    while (!stack.empty()) {
        auto element = stack.top();
        stack.pop();

        // ��鵱ǰԪ���Ƿ�ƥ��
        if (TestElementMatcher(element, matcher, fallbackClassName)) {
            return element;
        }#include <winrt/Microsoft.UI.Xaml.Media.h>

        // �����Ԫ�ص�ջ��
        int childCount = VisualTreeHelper::GetChildrenCount(element);
        for (int i = 0; i < childCount; i++) {
            auto child = VisualTreeHelper::GetChild(element, i);
            auto frameworkElement = child.try_as<FrameworkElement>();
            if (frameworkElement) {
                stack.push(frameworkElement);
            }
        }
    }

    return nullptr;
}

bool ElementFinder::TestElementMatcher(
    FrameworkElement element,
    const ElementMatcher& matcher,
    PCWSTR fallbackClassName
) {
    // �������ƥ��
    auto className = winrt::get_class_name(element);
    if (!matcher.type.empty() &&
        matcher.type != className &&
        (!fallbackClassName || matcher.type != fallbackClassName)) {
        return false;
    }

    // �������ƥ��
    if (!matcher.name.empty() && matcher.name != element.Name()) {
        return false;
    }

    // �������λ��
    if (matcher.oneBasedIndex > 0) {
        auto parent = VisualTreeHelper::GetParent(element);
        if (!parent) {
            return false;
        }

        int index = matcher.oneBasedIndex - 1;
        if (index < 0 ||
            index >= VisualTreeHelper::GetChildrenCount(parent) ||
            VisualTreeHelper::GetChild(parent, index) != element) {
            return false;
        }
    }

    // ע�⣺�����ݲ�����visualStateGroupName����Ϊ����Ӱ��Ԫ�صĴ�����
    // ����������ʽӦ�ã��ڲ���Ԫ��ʱ���Ժ���

    return true;
}

ElementFinder::ElementMatcher ElementFinder::ParseTargetString(const std::wstring& target) {
    ElementMatcher matcher;
    size_t pos = 0;

    // ��������
    size_t typeEnd = target.find_first_of(L"#@[", pos);
    if (typeEnd == std::wstring::npos) {
        matcher.type = target;
        matcher.type = AdjustTypeName(matcher.type);
        return matcher;
    }

    matcher.type = target.substr(pos, typeEnd - pos);
    matcher.type = AdjustTypeName(matcher.type);
    pos = typeEnd;

    while (pos < target.size()) {
        wchar_t delimiter = target[pos];
        pos++;

        if (delimiter == L'#') {
            // ��������
            size_t nameEnd = target.find_first_of(L"@[", pos);
            if (nameEnd == std::wstring::npos) {
                matcher.name = target.substr(pos);
                break;
            }
            matcher.name = target.substr(pos, nameEnd - pos);
            pos = nameEnd;
        }
        else if (delimiter == L'@') {
            // �����Ӿ�״̬��
            size_t vsgEnd = target.find_first_of(L"[", pos);
            if (vsgEnd == std::wstring::npos) {
                matcher.visualStateGroupName = target.substr(pos);
                break;
            }
            matcher.visualStateGroupName = target.substr(pos, vsgEnd - pos);
            pos = vsgEnd;
        }
        else if (delimiter == L'[') {
            // ��������������
            size_t endBracket = target.find(L']', pos);
            if (endBracket == std::wstring::npos) {
                // û�бպ����ţ�����
                break;
            }

            std::wstring content = target.substr(pos, endBracket - pos);
            pos = endBracket + 1;

            // ����Ƿ������֣�������
            if (std::all_of(content.begin(), content.end(), ::isdigit)) {
                matcher.oneBasedIndex = std::stoi(content);
            }
            // ���������ԣ���ʵ�����ݲ��������ԣ�
        }
    }

    return matcher;
}

std::wstring ElementFinder::AdjustTypeName(const std::wstring& type) {
    if (type.find(L'.') == std::wstring::npos) {
        if (type == L"Rectangle") {
            return L"Microsoft.UI.Xaml.Shapes.Rectangle";
        }
        return L"Microsoft.UI.Xaml.Controls." + type;
    }
    return type;
}