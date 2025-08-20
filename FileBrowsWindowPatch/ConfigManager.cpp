#include "pch.h"

#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include "Helper.h"

ConfigManager::Config ConfigManager::config;

void ConfigManager::LoadConfig() {
    // 或者使用构造函数初始化：
    config = Config{};
}

const ConfigManager::Config& ConfigManager::GetConfig() {
    return config;
}

std::wstring ConfigManager::GetConfigPath() {
    return GetCurDllDir() + L"\\ExplorerBlurMica.ini";
}

void ConfigManager::ParseColor(const std::wstring& value, Gdiplus::Color& color) {
    if (value.length() < 6) return;

    try {
        unsigned int rgba = std::stoul(value, nullptr, 16);
        color = Gdiplus::Color(
            (rgba >> 24) & 0xFF,  // R
            (rgba >> 16) & 0xFF,  // G
            (rgba >> 8) & 0xFF,   // B
            rgba & 0xFF           // A
        );
    }
    catch (...) {
        // 解析失败时使用默认值
        color = Gdiplus::Color(40, 40, 40, 200);
    }
}

float ConfigManager::ParseFloat(const std::wstring& value, float defaultValue) {
    try {
        return std::stof(value);
    }
    catch (...) {
        return defaultValue;
    }
}

int ConfigManager::ParseInt(const std::wstring& value, int defaultValue) {
    try {
        return std::stoi(value);
    }
    catch (...) {
        return defaultValue;
    }
}