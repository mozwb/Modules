#include "UI/Component/ImageView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace X_Y
{
    void ImageView::SetImage(const Decode::Image &image)
    {
        m_Image = image;
        RequestRepaint();
    }

    void ImageView::SetImage(Decode::Image &&image)
    {
        m_Image = std::move(image);
        RequestRepaint();
    }

    void ImageView::ClearImage()
    {
        m_Image = Decode::Image{};
        RequestRepaint();
    }

    void ImageView::SetScaleMode(ImageScaleMode mode)
    {
        if (m_ScaleMode == mode)
            return;
        m_ScaleMode = mode;
        RequestRepaint();
    }

    void ImageView::OnPaint(Canvas &canvas)
    {
        if (!m_Image.IsLoaded() || GetWidth() <= 0 || GetHeight() <= 0)
            return;

        int drawX = 0;
        int drawY = 0;
        int drawWidth = GetWidth();
        int drawHeight = GetHeight();

        if (m_ScaleMode == ImageScaleMode::Contain)
        {
            const double scale = std::min(
                static_cast<double>(drawWidth) / m_Image.GetWidth(),
                static_cast<double>(drawHeight) / m_Image.GetHeight());
            drawWidth = std::max(1, static_cast<int>(std::round(m_Image.GetWidth() * scale)));
            drawHeight = std::max(1, static_cast<int>(std::round(m_Image.GetHeight() * scale)));
            drawX = (GetWidth() - drawWidth) / 2;
            drawY = (GetHeight() - drawHeight) / 2;
        }

        const Buffer &pixels = m_Image.GetPixelBuffer();
        canvas.DrawImage(
            pixels.Data,
            pixels.Size,
            m_Image.GetWidth(),
            m_Image.GetHeight(),
            m_Image.GetChannels(),
            drawX,
            drawY,
            drawWidth,
            drawHeight);
    }
}
