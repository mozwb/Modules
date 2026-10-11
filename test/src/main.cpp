#include "UI/Component/Label.h"
#include "UI/Container/tabhostcontainer.h"
#include "UI/DockLayout/toplayout.h"
#include "UI/Panel/HexViewer.h"
#include "UI/Panel/ImageViewer.h"
#include "UI/Panel/LogViewer.h"
#include "UI/Panel/MediaPlayerPanel.h"
#include "Widget/Application.h"
#include "Log/XYLog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    bool IsVideoPath(const X_Y::XPath &path)
    {
        std::string extension = path.Path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        return extension == ".mp4" || extension == ".mkv" ||
               extension == ".avi" || extension == ".mov" ||
               extension == ".webm" || extension == ".wmv" ||
               extension == ".m4v";
    }

    class ColorPanel final : public X_Y::Panel
    {
    public:
        ColorPanel(std::string title, uint32_t color)
            : m_Label(new X_Y::Label())
        {
            m_Label->SetText(title);
            m_Label->SetColor(0xFFFFFFFF);
            m_Label->SetBgColor(color);
            AddComponent(m_Label);
        }

    protected:
        void OnLayout() override
        {
            m_Label->SetRect(0, 0, m_W, m_H);
        }

    private:
        X_Y::Label *m_Label;
    };

}

int main(int argc, char *argv[])
{
    X_Y::Application app(argc, argv);
#ifdef XY_DEBUG
    XINFO("X_Y Visual Test started");
#endif
    const X_Y::XPath inputPath =
        argc > 1 ? X_Y::XPath(std::filesystem::path(argv[1])) : X_Y::XPath{};

    auto *layout = new X_Y::TopLayout();
    auto *window = new X_Y::TabHostContainer();
    window->setTitle("X_Y Visual Test");
    window->setSize(1200, 800);
    window->SetDockLayout(layout);

    auto logViewer = std::make_unique<X_Y::LogViewer>();
    logViewer->Start();
    layout->TopDock().AddPanel(std::move(logViewer), "Log");

    layout->BottomDock().AddPanel(
        std::make_unique<ColorPanel>("Bottom Dock", 0xFF5D4037), "Bottom");

    auto hexViewer = std::make_unique<X_Y::HexViewer>();
    if (!inputPath.Empty())
        hexViewer->OpenFile(inputPath);
    layout->LeftDock().AddPanel(std::move(hexViewer), "Hex");

    auto imageViewer = std::make_unique<X_Y::ImageViewer>();
    if (!inputPath.Empty())
        imageViewer->OpenFile(inputPath);
    layout->CenterDock().AddPanel(std::move(imageViewer), "Image");

    auto mediaPlayer = std::make_unique<X_Y::MediaPlayerPanel>();
    if (!inputPath.Empty() && IsVideoPath(inputPath))
        mediaPlayer->OpenFile(inputPath);
    layout->CenterDock().AddPanel(std::move(mediaPlayer), "Media Player");

    layout->RightDock().AddPanel(
        std::make_unique<ColorPanel>("Right Dock", 0xFF4E342E), "Right");

    app.SetMainWindow(window);
    app.Own(window);
    if (!window->show())
    {
        app.Shutdown();
#ifdef XY_DEBUG
        logger.clear();
#endif
        return 1;
    }

    while (app.isRunning())
    {
        app.pushEvents();
        app.ProcessEvents();
    }

    app.Shutdown();
#ifdef XY_DEBUG
    logger.clear();
#endif
    return 0;
}
