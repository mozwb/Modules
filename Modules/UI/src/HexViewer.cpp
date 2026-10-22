#include "../panel/HexViewer.h"
#include "UI/Component/Button.h"
#include "UI/Component/ScrollArea.h"
#include "UI/Component/horizontal.h"
#include "Widget/FontLibrary.h"
#include "Memory/Memory.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

namespace X_Y
{
    class HexViewer::BinaryContent final : public Component
    {
    public:
        bool OpenFile(const XPath &path)
        {
            if (path.Empty() || !path.IsFile())
                return false;

            Buffer data = path.ReadBinary();
            if (data.Size > 0 && !data.Data)
                return false;

            m_File = path;
            m_Data = std::move(data);
            m_ViewportOffset = 0;
            const uint64_t rowCount = (m_Data.Size + kBytesPerRow - 1) / kBytesPerRow;
            const int contentHeight = kHeaderHeight + static_cast<int>(rowCount) * kRowHeight;
            SetRect(GetX(), GetY(), GetWidth(), std::max(kHeaderHeight, contentHeight));
            return true;
        }

        void ClearFile()
        {
            m_File = XPath();
            m_Data.Release();
            m_ViewportOffset = 0;
            SetRect(GetX(), GetY(), GetWidth(), kHeaderHeight);
        }

        int GetScrollStep() const override { return kRowHeight; }

        void SetViewport(int scrollOffset, int viewHeight) override
        {
            m_ViewportOffset = std::max(0, scrollOffset);
            m_ViewportHeight = std::max(0, viewHeight);
        }

        void OnPaint(Canvas &canvas) override
        {
            // 本组件由 ScrollArea 持有；ScrollArea 已压过 origin（含滚动位移），
            // 所以这里一律按【自身局部坐标】(0,0 起画)，不再加 GetX()/GetY()。
            constexpr int x = 0;
            constexpr int y = 0;
            const int width = GetWidth();
            const int firstRow = std::max(0, m_ViewportOffset / kRowHeight);
            const uint64_t rowCount = (m_Data.Size + kBytesPerRow - 1) / kBytesPerRow;
            const int lastRow = std::min<uint64_t>(rowCount,
                                                   firstRow + m_ViewportHeight / kRowHeight + 2);

            const Font &font = FontLibrary::Instance().GetDefault();
            const int addressWidth = font.MeasureText("0000000000000000");
            const int hexX = x + kLeftPadding + addressWidth + kColumnGap;
            canvas.FillRect(x, y, width, kHeaderHeight, 0xFF202832);
            canvas.DrawText(font, x + kLeftPadding, y + 4, "Offset", 0xFF9DAAB8);
            canvas.DrawText(font, hexX, y + 4, "Hexadecimal", 0xFF9DAAB8);

            for (int row = firstRow; row < lastRow; ++row)
            {
                const uint64_t offset = static_cast<uint64_t>(row) * kBytesPerRow;
                if (offset >= m_Data.Size)
                    break;

                char address[32] = {};
                char hex[3 * kBytesPerRow + 1] = {};
                FormatOffset(address, offset);
                for (int column = 0; column < kBytesPerRow; ++column)
                {
                    const uint64_t index = offset + column;
                    if (index >= m_Data.Size)
                    {
                        hex[column * 3] = ' ';
                        hex[column * 3 + 1] = ' ';
                        hex[column * 3 + 2] = ' ';
                        continue;
                    }

                    const unsigned char value = m_Data[index];
                    const char *digits = "0123456789ABCDEF";
                    hex[column * 3] = digits[value >> 4];
                    hex[column * 3 + 1] = digits[value & 0x0F];
                    hex[column * 3 + 2] = ' ';
                }
                hex[3 * kBytesPerRow] = '\0';

                const int rowY = y + kHeaderHeight + row * kRowHeight;
                if ((row & 1) == 0)
                    canvas.FillRect(x, rowY, width, kRowHeight, 0xFF171D24);
                char line[sizeof(address) + 2 + sizeof(hex)] = {};
                std::memcpy(line, address, 16);
                line[16] = ' ';
                line[17] = ' ';
                std::memcpy(line + 18, hex, sizeof(hex));
                canvas.DrawText(font, x + kLeftPadding, rowY + 2,
                                line, 0xFFD7DEE5);
            }
        }

    private:
        static void FormatOffset(char (&buffer)[32], uint64_t offset)
        {
            static constexpr char digits[] = "0123456789ABCDEF";
            for (int i = 0; i < 16; ++i)
                buffer[i] = digits[(offset >> ((15 - i) * 4)) & 0x0F];
            buffer[16] = '\0';
        }

        static constexpr int kBytesPerRow = 16;
        static constexpr int kRowHeight = 20;
        static constexpr int kHeaderHeight = 24;
        static constexpr int kLeftPadding = 8;
        static constexpr int kColumnGap = 16;

        XPath m_File;
        Buffer m_Data;
        int m_ViewportOffset = 0;
        int m_ViewportHeight = 0;
    };

    HexViewer::HexViewer()
        : FileDropPanel(),
          m_Content(std::make_unique<BinaryContent>())
    {
        m_FileBar = new Horizontal();
        m_ScrollArea = new ScrollArea();
        m_ScrollArea->SetContent(m_Content.get());
        m_FileBar->OnSelected = [this](std::size_t index, Component *)
        {
            if (!m_UpdatingSelection)
                ActivateFile(index);
        };
        // ★ 所有权交给 Panel（AddComponent 接管，析构时由 Panel 释放）；
        //   本类成员只留裸借用指针。
        Panel::AddComponent(m_FileBar);
        Panel::AddComponent(m_ScrollArea);
    }

    HexViewer::~HexViewer() = default;

    bool HexViewer::OpenFile(const XPath &path)
    {
        if (path.Empty() || !path.IsFile())
            return false;

        auto it = std::find_if(m_Files.begin(), m_Files.end(),
                               [&path](const XPath &file)
                               { return file.IsPathEqual(path); });
        if (it == m_Files.end())
        {
            AddFile(path);
            it = std::prev(m_Files.end());
        }

        ActivateFile(static_cast<std::size_t>(it - m_Files.begin()));
        return !m_ActiveFile.Empty();
    }

    void HexViewer::AddFile(const XPath &path)
    {
        if (path.Empty() || !path.IsFile())
            return;

        m_Files.push_back(path);
        const std::size_t index = m_Files.size() - 1;
        auto tab = std::make_unique<Button>();
        tab->SetText(path.getName().c_str());
        tab->SetRect(0, 0, 180, kFileBarHeight);
        tab->SetRounded(true);
        tab->SetCloseable(true);
        tab->SetBgColor(0xFF29313A);
        tab->SetHoverColor(0xFF3B4D5E);
        tab->SetTextColor(0xFFE6EDF3);
        tab->SetOnClose([this, self = static_cast<Button *>(tab.get())]()
                        { m_PendingClose.push_back(self); });
        m_FileBar->AddComponent(tab.get());
        m_FileTabs.push_back(std::move(tab));

        if (m_Files.size() == 1)
            ActivateFile(0);
        RequestRepaint();
    }

    void HexViewer::ClearFiles()
    {
        // ⚠️ 先清待关闭队列：下面 m_FileTabs.clear() 会把那些 Button 全析构，
        //    队列里存的裸指针立刻变悬空 → 下次 ProcessPendingClose 就是 UAF。
        m_PendingClose.clear();

        m_Files.clear();
        m_FileTabs.clear();
        m_FileBar->Clear();
        m_Content->ClearFile();
        m_ActiveFile = XPath();
        RequestRepaint();
    }

    void HexViewer::ActivateFile(std::size_t index)
    {
        if (index >= m_Files.size() || !m_Content->OpenFile(m_Files[index]))
            return;

        m_UpdatingSelection = true;
        m_FileBar->Select(index);
        m_UpdatingSelection = false;
        m_ActiveFile = m_Files[index];
        for (std::size_t i = 0; i < m_FileTabs.size(); ++i)
            m_FileTabs[i]->SetBgColor(i == index ? 0xFF36506A : 0xFF29313A);
        RequestRepaint();
    }

    void HexViewer::CloseFile(std::size_t index)
    {
        if (index >= m_Files.size())
            return;

        m_FileBar->RemoveComponent(m_FileTabs[index].get());
        m_FileTabs.erase(m_FileTabs.begin() + index);
        m_Files.erase(m_Files.begin() + index);

        // 重挂剩余 tab 的关闭回调。
        // ⚠️ 与 AddFile 统一用【指针】捕获（不再捕获下标）：下标在"登记 → 结算"
        //    这段延迟窗口里会过期。指针稳定，结算时反查下标 —— 见 ProcessPendingClose。
        for (auto &tab : m_FileTabs)
        {
            Button *self = static_cast<Button *>(tab.get());
            tab->SetOnClose([this, self]()
                            { m_PendingClose.push_back(self); });
        }

        if (m_Files.empty())
        {
            m_Content->ClearFile();
            m_ActiveFile = XPath();
        }
        else
        {
            const std::size_t nextIndex = std::min(index, m_Files.size() - 1);
            ActivateFile(nextIndex);
        }
        RequestRepaint();
    }

    void HexViewer::ProcessPendingClose()
    {
        if (m_PendingClose.empty())
            return;

        // 取走队列（结算过程中可能又有人登记，用 swap 避免自己吃自己）
        std::vector<Button *> pending;
        pending.swap(m_PendingClose);

        for (Button *tab : pending)
        {
            if (!tab)
                continue;

            // ★ 按【指针】反查当前下标：登记到现在可能已经增删过文件，
            //   下标会过期，指针不会。找不到（已被关掉）就跳过。
            std::size_t index = m_FileTabs.size();
            for (std::size_t i = 0; i < m_FileTabs.size(); ++i)
            {
                if (m_FileTabs[i].get() == tab)
                {
                    index = i;
                    break;
                }
            }
            if (index < m_FileTabs.size())
                CloseFile(index);
        }
    }

    void HexViewer::OnLayout()
    {
        const int width = GetWidth();
        const int height = GetHeight();
        const int barHeight = std::min(kFileBarHeight, std::max(0, height));
        m_FileBar->SetRect(0, 0, width, barHeight);
        m_ScrollArea->SetRect(0, barHeight, width, std::max(0, height - barHeight));
    }

    void HexViewer::OnPaint(Canvas &canvas)
    {
        ProcessPendingClose();
        // HexViewer 是 Panel：宿主（Dock）已压过 origin，一律按 Panel 局部 (0,0) 起画。
        canvas.FillRect(0, 0, GetWidth(), GetHeight(), 0xFF11151A);
        FileDropPanel::OnPaint(canvas);
    }

    bool HexViewer::OnOpenFiles(const std::vector<XPath> &files)
    {
        bool opened = false;
        for (const XPath &file : files)
            opened = OpenFile(file) || opened;
        RequestRepaint();
        return opened;
    }
}