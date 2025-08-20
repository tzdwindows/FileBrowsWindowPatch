#include "pch.h"

#include "Helper.h"

std::wstring GetCurDllDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);
    std::wstring wpath = path;
    size_t pos = wpath.find_last_of(L"\\");
    if (pos != std::wstring::npos) {
        return wpath.substr(0, pos);
    }
    return L"";
}

// https://stackoverflow.com/a/54364173
std::wstring_view TrimStringView(std::wstring_view s) {
    s.remove_prefix(std::min(s.find_first_not_of(L" \t\r\v\n"), s.size()));
    s.remove_suffix(
        std::min(s.size() - s.find_last_not_of(L" \t\r\v\n") - 1, s.size()));
    return s;
}

// https://stackoverflow.com/a/46931770
std::vector<std::wstring_view> SplitStringView(std::wstring_view s,
    std::wstring_view delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::wstring_view token;
    std::vector<std::wstring_view> res;

    while ((pos_end = s.find(delimiter, pos_start)) !=
        std::wstring_view::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }

    res.push_back(s.substr(pos_start));
    return res;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return L"";

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wideLength == 0) return L"";

    std::wstring wideStr;
    wideStr.resize(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wideStr[0], wideLength);

    // ÒÆ³ýÄ©Î²µÄ null ×Ö·û
    if (!wideStr.empty() && wideStr.back() == L'\0') {
        wideStr.pop_back();
    }

    return wideStr;
}