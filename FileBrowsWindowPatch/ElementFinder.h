#pragma once


#include <string>
#include <vector>
#include <optional>

#include "VisualTreeWatcher.h"

namespace winrt {
    using namespace winrt;
    using namespace Windows::UI::Xaml;
    using namespace Media;
}

class ElementFinder {
public:
    struct ElementMatcher {
        std::wstring type;
        std::wstring name;
        std::optional<std::wstring> visualStateGroupName;
        int oneBasedIndex = 0;
    };

    struct ParentChildRelation {
        InstanceHandle ParentHandle;
        InstanceHandle ChildHandle;
    };

    ElementFinder();
    ~ElementFinder();

    winrt::FrameworkElement FindMatchingElement(
        winrt::FrameworkElement root,
        const std::wstring& target,
        PCWSTR fallbackClassName = nullptr
    );

private:
    bool TestElementMatcher(
        winrt::FrameworkElement element,
        const ElementMatcher& matcher,
        PCWSTR fallbackClassName
    );

    ElementMatcher ParseTargetString(const std::wstring& target);
    std::wstring AdjustTypeName(const std::wstring& type);
};