#include "pch.h"

#include "ImageRenderer.h"
#include <algorithm>

#include "HookManager.h"

#pragma comment(lib, "gdiplus.lib")

ImageRenderer::ImageRenderer() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
}

ImageRenderer::~ImageRenderer() {
    ClearCache();
}

bool ImageRenderer::LoadImage(const std::wstring& path, int radius) {
    sourceBitmap.reset(Gdiplus::Bitmap::FromFile(path.c_str()));
    blurRadius = radius;
    needsUpdate = true;
    return sourceBitmap != nullptr && sourceBitmap->GetLastStatus() == Gdiplus::Ok;
}

void ImageRenderer::Render(HDC hdc, const RECT& rect, float opacity) {
    if (!sourceBitmap) return;

    RECT clientRect;
    GetClientRect(WindowFromDC(hdc), &clientRect);

    if (cachedWidth != clientRect.right || cachedHeight != clientRect.bottom) {
        Resize(clientRect.right, clientRect.bottom);
    }

    if (needsUpdate) {
        UpdateRenderedBitmap();
    }

    if (renderedBitmap) {
        Gdiplus::Graphics graphics(hdc);

        // 设置透明度
        Gdiplus::ColorMatrix matrix = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, opacity, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f
        };

        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault,
            Gdiplus::ColorAdjustTypeBitmap);

        graphics.DrawImage(
            renderedBitmap.get(),
            Gdiplus::Rect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top),
            0, 0, renderedBitmap->GetWidth(), renderedBitmap->GetHeight(),
            Gdiplus::UnitPixel, &attributes);
    }
}

void ImageRenderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;

    cachedWidth = width;
    cachedHeight = height;
    needsUpdate = true;
}

void ImageRenderer::ClearCache() {
    renderedBitmap.reset();
    needsUpdate = true;
}

void ImageRenderer::UpdateRenderedBitmap() {
    if (!sourceBitmap) return;

    renderedBitmap.reset(new Gdiplus::Bitmap(cachedWidth, cachedHeight));
    Gdiplus::Graphics graphics(renderedBitmap.get());

    // 绘制缩放后的图像
    graphics.DrawImage(
        sourceBitmap.get(),
        Gdiplus::Rect(0, 0, cachedWidth, cachedHeight),
        0, 0, sourceBitmap->GetWidth(), sourceBitmap->GetHeight(),
        Gdiplus::UnitPixel);

    if (blurRadius > 0) {
        ApplyBlurEffect();
    }

    needsUpdate = false;
}

// 简化的模糊算法 (实际项目应使用更高效的方法)
void ImageRenderer::ApplyBlurEffect() {
    const int radius = std::min(blurRadius, 10);
    const int size = 2 * radius + 1;
    const float weight = 1.0f / (size * size);

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, renderedBitmap->GetWidth(), renderedBitmap->GetHeight());

    if (renderedBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
        PixelFormat32bppARGB, &bmpData) == Gdiplus::Ok)
    {
        BYTE* pixels = static_cast<BYTE*>(bmpData.Scan0);
        const int stride = bmpData.Stride;
        const int width = bmpData.Width;
        const int height = bmpData.Height;

        // 创建临时缓冲区
        std::vector<BYTE> tempBuffer(stride * height);
        memcpy(tempBuffer.data(), pixels, stride * height);

        // 水平模糊
        for (int y = 0; y < height; y++) {
            for (int x = radius; x < width - radius; x++) {
                int sumB = 0, sumG = 0, sumR = 0, sumA = 0;

                for (int k = -radius; k <= radius; k++) {
                    const int offset = (y * stride) + ((x + k) * 4);
                    sumB += tempBuffer[offset];
                    sumG += tempBuffer[offset + 1];
                    sumR += tempBuffer[offset + 2];
                    sumA += tempBuffer[offset + 3];
                }

                const int offset = (y * stride) + (x * 4);
                pixels[offset] = static_cast<BYTE>(sumB * weight);
                pixels[offset + 1] = static_cast<BYTE>(sumG * weight);
                pixels[offset + 2] = static_cast<BYTE>(sumR * weight);
                pixels[offset + 3] = static_cast<BYTE>(sumA * weight);
            }
        }

        // 垂直模糊
        memcpy(tempBuffer.data(), pixels, stride * height);

        for (int y = radius; y < height - radius; y++) {
            for (int x = 0; x < width; x++) {
                int sumB = 0, sumG = 0, sumR = 0, sumA = 0;

                for (int k = -radius; k <= radius; k++) {
                    const int offset = ((y + k) * stride) + (x * 4);
                    sumB += tempBuffer[offset];
                    sumG += tempBuffer[offset + 1];
                    sumR += tempBuffer[offset + 2];
                    sumA += tempBuffer[offset + 3];
                }

                const int offset = (y * stride) + (x * 4);
                pixels[offset] = static_cast<BYTE>(sumB * weight);
                pixels[offset + 1] = static_cast<BYTE>(sumG * weight);
                pixels[offset + 2] = static_cast<BYTE>(sumR * weight);
                pixels[offset + 3] = static_cast<BYTE>(sumA * weight);
            }
        }

        renderedBitmap->UnlockBits(&bmpData);
    }
}