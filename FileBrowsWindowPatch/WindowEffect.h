#pragma once
#include "ImageRenderer.h"

#include "ConfigManager.h"

class WindowEffect {
public:
    WindowEffect() = default;

    WindowEffect(HWND hWnd);

    void ApplyEffect();
    void SetEffectType(ConfigManager::EffectType type);
    void SetBlendColor(COLORREF color);
    void SetImageBackground(const std::wstring& path, float opacity, int blurRadius);
    bool HasImageBackground() const;
    void RenderImageBackground(HDC hdc, const RECT& rect);

    void HandleSizeChanged();
    void HandleDpiChanged();

private:
    HWND m_hWnd;
    ConfigManager::EffectType m_effectType = ConfigManager::EffectType::Acrylic;
    COLORREF m_blendColor = RGB(0, 0, 0);

    // Õº∆¨±≥æ∞œ‡πÿ
    std::unique_ptr<ImageRenderer> m_imageRenderer;
    std::wstring m_imagePath;
    float m_imageOpacity = 1.0f;
    int m_imageBlurRadius = 0;

    void ApplyCompositionEffect();
    void ApplyMicaEffect();
    void ApplyBlurEffect();
};