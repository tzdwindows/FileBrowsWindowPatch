#include "pch.h"

#include "WindowEffect.h"
#include <dwmapi.h>


typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

WindowEffect::WindowEffect(HWND hWnd) : m_hWnd(hWnd) {
    const auto& config = ConfigManager::GetConfig();

    m_effectType = config.effectType;
    m_blendColor = config.blendColor.ToCOLORREF();

    if (!config.imagePath.empty()) {
        SetImageBackground(config.imagePath, config.imageOpacity, config.imageBlurRadius);
    }

    ApplyEffect();
}

void WindowEffect::ApplyEffect() {}
void WindowEffect::ApplyCompositionEffect() {}
void WindowEffect::ApplyMicaEffect() {}
void WindowEffect::ApplyBlurEffect() {}

void WindowEffect::SetEffectType(ConfigManager::EffectType type) {
    m_effectType = type;
    ApplyEffect();
}

void WindowEffect::SetBlendColor(COLORREF color) {
    m_blendColor = color;
    ApplyEffect();
}

void WindowEffect::SetImageBackground(const std::wstring& path, float opacity, int blurRadius) {
    m_imagePath = path;
    m_imageOpacity = opacity;
    m_imageBlurRadius = blurRadius;

    if (!path.empty()) {
        if (!m_imageRenderer) {
            m_imageRenderer = std::make_unique<ImageRenderer>();
        }
        m_imageRenderer->LoadImage(path, blurRadius);
    }
    else {
        m_imageRenderer.reset();
    }

    // 强制重绘窗口
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

bool WindowEffect::HasImageBackground() const {
    return m_imageRenderer != nullptr;
}

void WindowEffect::RenderImageBackground(HDC hdc, const RECT& rect) {
    if (m_imageRenderer) {
        m_imageRenderer->Render(hdc, rect, m_imageOpacity);
    }
}

void WindowEffect::HandleSizeChanged() {
    if (m_imageRenderer) {
        RECT clientRect;
        GetClientRect(m_hWnd, &clientRect);
        m_imageRenderer->Resize(clientRect.right, clientRect.bottom);
    }
}

void WindowEffect::HandleDpiChanged() {
    if (m_imageRenderer) {
        m_imageRenderer->ClearCache();
    }
}