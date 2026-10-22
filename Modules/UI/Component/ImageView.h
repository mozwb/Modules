#pragma once

#include "Component.h"
#include "Decoder/Image.h"

namespace X_Y
{
    enum class ImageScaleMode
    {
        Contain,
        Stretch
    };

    class ImageView final : public Component
    {
    public:
        void SetImage(const Decode::Image &image);
        void SetImage(Decode::Image &&image);
        void ClearImage();

        const Decode::Image &GetImage() const { return m_Image; }

        void SetScaleMode(ImageScaleMode mode);
        ImageScaleMode GetScaleMode() const { return m_ScaleMode; }

        void OnPaint(Canvas &canvas) override;

    private:
        Decode::Image m_Image;
        ImageScaleMode m_ScaleMode = ImageScaleMode::Contain;
    };
}
