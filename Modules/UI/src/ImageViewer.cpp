#include "UI/Panel/ImageViewer.h"

#include "UI/Component/ImageView.h"
#include "UI/Component/Label.h"
#include "Decoder/Image.h"

#include <string>
#include <utility>

namespace X_Y
{
    ImageViewer::ImageViewer()
        : m_ImageView(new ImageView()),
          m_Status(new Label())
    {
        m_Status->SetColor(0xFFFFFFFF);
        m_Status->SetBgColor(0xFF30343B);
        m_Status->SetText("Drop an image file here.");

        AddComponent(m_ImageView);
        AddComponent(m_Status);
    }

    bool ImageViewer::OpenFile(const XPath &path)
    {
        return FileDropPanel::OpenFile(path);
    }

    void ImageViewer::OnLayout()
    {
        constexpr int statusHeight = 26;
        const int imageHeight = m_H > statusHeight ? m_H - statusHeight : 0;
        m_ImageView->SetRect(0, 0, m_W, imageHeight);
        m_Status->SetRect(0, imageHeight, m_W, m_H - imageHeight);
    }

    void ImageViewer::OnPaint(Canvas &canvas)
    {
        canvas.FillRect(0, 0, m_W, m_H, 0xFF202124);
        FileDropPanel::OnPaint(canvas);
    }

    bool ImageViewer::OnOpenFiles(const std::vector<XPath> &files)
    {
        Decode::ImageError error = Decode::ImageError::InvalidData;
        for (const XPath &file : files)
        {
            Decode::Image image(file.Path());
            if (!image.IsLoaded())
            {
                error = image.GetError();
                continue;
            }

            m_Status->SetText(
                file.getName() + " | " +
                std::to_string(image.GetWidth()) + " x " +
                std::to_string(image.GetHeight()) + " | " +
                std::to_string(image.GetChannels()) + " channels");
            m_ImageView->SetImage(std::move(image));
            return true;
        }

        m_Status->SetText(
            "Image decode failed (error " +
            std::to_string(static_cast<int>(error)) + ")");
        return false;
    }
}
