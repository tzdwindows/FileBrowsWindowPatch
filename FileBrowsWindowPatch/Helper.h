#pragma once
#include <string>
#include <Windows.h>

std::wstring GetCurDllDir();

// https://stackoverflow.com/a/54364173
std::wstring_view TrimStringView(std::wstring_view s);

// https://stackoverflow.com/a/46931770
std::vector<std::wstring_view> SplitStringView(std::wstring_view s,
    std::wstring_view delimiter);

std::wstring Utf8ToWide(const std::string& utf8);
