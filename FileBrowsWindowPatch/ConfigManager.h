#pragma once
#include <string>
#include <Windows.h>
#include <gdiplus.h>

class ConfigManager {
public:
    enum EffectType {
        Acrylic = 0,
        Mica = 1,
        Blur = 2
    };

    struct Config {
        EffectType effectType = Acrylic;
        Gdiplus::Color blendColor = 0;

        std::wstring imagePath = L"C:\\Users\\Administrator\\Desktop\\db1d26c115.jpg";
        float imageOpacity = 0.5f;
        int imageBlurRadius = 5;

        bool automatic_acquisition_color = false;
        int automatic_acquisition_color_transparency = -1;
    };

    static void LoadConfig();
    static const Config& GetConfig();

private:
    static Config config;

    static std::wstring GetConfigPath();
    static void ParseColor(const std::wstring& value, Gdiplus::Color& color);
    static float ParseFloat(const std::wstring& value, float defaultValue);
    static int ParseInt(const std::wstring& value, int defaultValue);
};