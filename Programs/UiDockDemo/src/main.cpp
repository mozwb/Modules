// UiDockDemo —— 用 UI 模块复现五块 DockLayout：
//
//   ┌──────────────────────────────┐
//   │            Top / Log         │
//   ├──────┬───────────────┬───────┤
//   │ Left │   Center      │ Right │
//   │ Hex  │ Player / Image│ Info  │
//   ├──────┴───────────────┴───────┤
//   │          Bottom / Info       │
//   └──────────────────────────────┘
//
// 可把图片 / 视频路径作为 argv[1] 传进来，Hex 和 Player 会一起打开。
// 不传也能跑：Log / Hex / Player 面板和五块 Dock 都会显示。

#include "APP/Application.h"
#include "Log/XYLog.h"
#include "UI/Component/Label.h"
#include "UI/Container/tabhostcontainer.h"
#include "UI/DockLayout/toplayout.h"
#include "UI/Panel/HexViewer.h"
#include "UI/Panel/ImageViewer.h"
#include "UI/Panel/LogViewer.h"
#include "UI/Panel/MediaPlayerPanel.h"
#include "Widget/WindowHost.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    bool IsVideoPath(const X_Y::XPath &path)
    {
        std::string extension = path.Path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
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
    using namespace X_Y;

    App::Application app;

    XINFO("UiDockDemo: 启动");
    XINFO("UiDockDemo: 五区域 = Top / Bottom / Left / Center / Right");
    XINFO("UiDockDemo: 面板 = LogViewer / HexViewer / MediaPlayer / ImageViewer");

    const XPath inputPath =
        argc > 1 ? XPath(std::filesystem::path(argv[1])) : XPath{};

    auto *layout = new TopLayout();
    auto *window = new TabHostContainer();
    window->setTitle("X_Y UI Dock Demo");
    window->setSize(1200, 800);
    window->SetDockLayout(layout);

    // ── Top：日志 ────────────────────────────────────────────────────────────
    auto logViewer = std::make_unique<LogViewer>();
    logViewer->Start();
    layout->TopDock().AddPanel(std::move(logViewer), "Log");

    // ── Bottom：说明 ────────────────────────────────────────────────────────
    layout->BottomDock().AddPanel(
        std::make_unique<ColorPanel>("Bottom Dock · 拖分割线改变五块区域", 0xFF37474F),
        "Bottom");

    // ── Left：十六进制 ──────────────────────────────────────────────────────
    auto hexViewer = std::make_unique<HexViewer>();
    if (!inputPath.Empty())
    {
        hexViewer->OpenFile(inputPath);
    }
    layout->LeftDock().AddPanel(std::move(hexViewer), "Hex");

    // ── Center：播放器 + 图片（同一 Dock 的两个 tab）────────────────────────
    auto mediaPlayer = std::make_unique<MediaPlayerPanel>();
    if (!inputPath.Empty() && IsVideoPath(inputPath))
    {
        mediaPlayer->OpenFile(inputPath);
    }
    X_Y::Panel *mediaPanel =
        layout->CenterDock().AddPanel(std::move(mediaPlayer), "Media Player");

    auto imageViewer = std::make_unique<ImageViewer>();
    if (!inputPath.Empty() && !IsVideoPath(inputPath))
    {
        imageViewer->OpenFile(inputPath);
    }
    X_Y::Panel *imagePanel =
        layout->CenterDock().AddPanel(std::move(imageViewer), "Image");

    // 有图片路径就落图片 tab；否则默认播放器 tab。
    if (imagePanel && !inputPath.Empty() && !IsVideoPath(inputPath))
    {
        layout->CenterDock().ActivatePanel(imagePanel);
    }
    else if (mediaPanel)
    {
        layout->CenterDock().ActivatePanel(mediaPanel);
    }

    // ── Right：状态 ─────────────────────────────────────────────────────────
    layout->RightDock().AddPanel(
        std::make_unique<ColorPanel>("Right Dock · Log / Hex / Player 都在这", 0xFF4E342E),
        "Right");

    WindowHost::Instance().SetMainWindow(window);
    WindowHost::Instance().Own(window);
    if (!window->show())
    {
        std::fprintf(stderr, "UiDockDemo: 窗口创建失败\n");
        WindowHost::Instance().Shutdown();
        return 1;
    }

    XINFO("UiDockDemo: 窗口已显示，进入主循环");
    app.Run();

    WindowHost::Instance().Shutdown();
    XINFO("UiDockDemo: 退出");
    return 0;
}
