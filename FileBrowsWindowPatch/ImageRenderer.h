#pragma once
#include <gdiplus.h>
#include <memory>
#include <string>

class ImageRenderer {
public:
    ImageRenderer();
    ~ImageRenderer();

    bool LoadImage(const std::wstring& path, int blurRadius = 0);
    void Render(HDC hdc, const RECT& rect, float opacity = 1.0f);
    void Resize(int width, int height);
    void ClearCache();

private:
    std::unique_ptr<Gdiplus::Bitmap> sourceBitmap;
    std::unique_ptr<Gdiplus::Bitmap> renderedBitmap;
    int cachedWidth = 0;
    int cachedHeight = 0;
    int blurRadius = 0;
    bool needsUpdate = true;

    void ApplyBlurEffect();
    void UpdateRenderedBitmap();
};