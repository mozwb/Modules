#include "UI/Component/ListBox.h"
#include "Widget/FontLibrary.h"
#include <algorithm>

namespace X_Y
{

    // ── 工具：UTF-8 单字符字节数 ──
    // 折行按完整安字符切开，中文不会被劈半。按首字节前导判断：
    //   0xxxxxxx → 1 字节；110xxxxx → 2；1110xxxx → 3；11110xxx → 4；其余按 1。

    int ListBox::Utf8CharLen(unsigned char lead)
    {
        if (lead < 0x80)
            return 1;
        if ((lead & 0xE0) == 0xC0)
            return 2;
        if ((lead & 0xF0) == 0xE0)
            return 3;
        if ((lead & 0xF8) == 0xF0)
            return 4;
        return 1;
    }

    void ListBox::AddItem(const char *text, uint32_t textColor, uint32_t bgColor)
    {
        ListBoxItem item;
        item.text = text ? text : "";
        item.textColor = textColor;
        item.bgColor = bgColor;
        m_Items.push_back(item);
        // 折行占位：段数待绘制时按实际宽补算（折叠是惰性的，只在 OnPaint 里做）
        m_Fold.emplace_back();
        // 高度在下次绘制补算 fold 后更新；先请求重绘
        RequestRepaint();
    }

    void ListBox::Clear()
    {
        m_Items.clear();
        m_Fold.clear();
        m_SelectedIndex = -1;
        m_TotalLines = 0;
        m_LineStartIndex.clear();
        m_LastFoldWidth = -1;
        m_FoldedCount = 0;
        UpdateHeight();
        RequestRepaint();
    }

    void ListBox::SetLineHeight(int h)
    {
        if (h <= 0)
            h = 1;
        m_LineHeight = h;
        UpdateHeight();
    }

    // 淘汰最旧的 n 条（从头部删）。
    //
    // ⚠️ 关键：必须重置折行游标。m_FoldedCount 是"已惰性折叠到第几条"的游标，
    //    EnsureFold 只从它往后补算 —— 这个设计【只适用于只增不减】。
    //    头部一删，所有 item 下标前移，m_Fold[i] 与 m_Items[i] 的对应关系整体错位，
    //    旧的 m_FoldedCount 就指向了错误的条目。
    //    ⇒ 统一重置成"全部待重折"（m_FoldedCount=0 + m_LastFoldWidth=-1），
    //      下一次 OnPaint 的 EnsureFold 会全量重折一次。
    //
    // ⚠️ 选中项：删的是头部，选中项下标整体前移 n。
    //    若选中项本身被删掉（下标 < n），收敛到新的第一项（列表非空时）。
    void ListBox::RemoveFirst(int n)
    {
        if (n <= 0)
            return;
        const int count = (int)m_Items.size();
        if (count == 0)
            return;
        if (n > count)
            n = count;

        m_Items.erase(m_Items.begin(), m_Items.begin() + n);
        m_Fold.erase(m_Fold.begin(), m_Fold.begin() + n);

        // 选中项跟着平移；被删掉则收敛
        if (m_SelectedIndex >= 0)
        {
            m_SelectedIndex -= n;
            if (m_SelectedIndex < 0)
                m_SelectedIndex = m_Items.empty() ? -1 : 0;
        }

        // 折行缓存整体作废 → 下次 OnPaint 全量重折
        m_FoldedCount = 0;
        m_LastFoldWidth = -1;
        m_Fold.resize(m_Items.size()); // 与新条数对齐（尾部补空占位）

        BuildLineIndex();
        UpdateHeight();
        RequestRepaint();
    }

    void ListBox::SetWrapMode(WrapMode mode)
    {
        if (m_WrapMode == mode)
            return;
        m_WrapMode = mode;
        m_LastFoldWidth = -1; // 段结构不同，强制全部重折
        m_FoldedCount = 0;
        RequestRepaint();
    }

    int ListBox::GetItemLineCount(int index) const
    {
        if (index < 0 || index >= (int)m_Fold.size())
            return 1;
        return (int)m_Fold[index].size();
    }

    // ── 折行核心 ──
    // 折一行 item 的文本，按当前可用宽逐字符累加切段。
    // 可用宽 = 内容宽 - 左缩进(indent=4)。
    // 规则：
    //   - 可用宽不足以容纳最宽单个字符时，整行丢弃（不显示，符合"窗口过窄不显示"）。
    //   - 能容纳至少一个字符 → 逐字符贪婪装箱，满宽切段，直至文字全部画完。

    void ListBox::FoldItem(size_t idx, Canvas &canvas)
    {
        const std::string &src = m_Items[idx].text;
        const Font &font = FontLibrary::Instance().GetDefault();
        std::vector<std::string> segs;
        segs.reserve(src.size() / 8 + 1);

        if (m_WrapMode == WrapMode::NoWrap)
        {
            // 不折行：整条一段（横向滚动交给 ScrollArea 处理）
            if (!src.empty())
                segs.push_back(src);
        }
        else if (src.empty())
        {
            // 空行：至少保留一个空物理行，保证行号/选中可感知
            segs.push_back("");
        }
        else
        {
            int usable = m_LastFoldWidth - 4; // 减左缩进
            if (usable <= 0)
            {
                // 窗口过窄连一个字符都放不下 → 整行丢弃，不显示
            }
            else
            {
                // 若能塞下整行则单段；否则逐字符切（按 UTF-8 完整字符）
                if (font.MeasureText(src.c_str()) <= usable)
                {
                    segs.push_back(src);
                }
                else
                {
                    std::string cur;
                    cur.reserve(src.size());
                    int curW = 0;
                    const size_t n = src.size();
                    size_t p = 0;
                    while (p < n)
                    {
                        int clen = Utf8CharLen((unsigned char)src[p]);
                        if (p + clen > n)
                            clen = 1;
                        std::string one = src.substr(p, clen);
                        int w = font.MeasureText(one.c_str());
                        // 首字符宽度已超可用宽 → 整行一个字符都放不下，丢弃
                        if (cur.empty() && w > usable)
                        {
                            segs.clear();
                            cur.clear();
                            curW = 0;
                            break;
                        }
                        if (!cur.empty() && curW + w > usable)
                        {
                            segs.push_back(cur);
                            cur.clear();
                            curW = 0;
                        }
                        cur += one;
                        curW += w;
                        p += clen;
                    }
                    if (!cur.empty())
                        segs.push_back(cur);
                }
            }
        }

        m_Fold[idx] = std::move(segs);
    }

    // 惰性折叠：在 OnPaint 里驱动（此时 Canvas 可用、宽度已知）。
    //  - 宽度变了 → 全量重折；数量多了 → 只补算新增条目。
    // 避免每帧重算全部 5000 条，增量喂入只折新条目。

    void ListBox::EnsureFold(Canvas &canvas)
    {
        const size_t n = m_Items.size();
        int curW = GetWidth();

        // 宽度变化 → 全部重折（旧折叠结果基于旧宽，作废）
        if (curW != m_LastFoldWidth)
        {
            m_LastFoldWidth = curW;
            m_FoldedCount = 0;
        }

        if ((int)n == m_FoldedCount)
            return; // 全部已折，无新增

        m_Fold.resize(n);
        // 只补算新增的 [m_FoldedCount, n)

        for (size_t i = m_FoldedCount; i < n; i++)
            FoldItem(i, canvas);

        m_FoldedCount = (int)n;
        BuildLineIndex();
        UpdateHeight();
    }

    // 重建物理行索引：m_LineStartIndex[i] = item i 的起始物理行（前缀和）。

    void ListBox::BuildLineIndex()
    {
        m_LineStartIndex.clear();
        m_LineStartIndex.reserve(m_Items.size() + 1);
        m_LineStartIndex.push_back(0);
        int acc = 0;
        for (size_t i = 0; i < m_Items.size(); i++)
        {
            acc += (int)m_Fold[i].size();
            m_LineStartIndex.push_back(acc);
        }
        m_TotalLines = acc;
    }

    void ListBox::UpdateHeight()
    {
        SetRect(GetX(), GetY(), GetWidth(), m_TotalLines * m_LineHeight);
    }

    // ── 物理行 → 逻辑 item ──
    // localY → 物理行 → 二分定位到所属 item（返回 item 索引，-1 = 未命中）。

    int ListBox::GetRowFromMouseY(int localY) const
    {
        int row = localY / m_LineHeight;
        if (row < 0 || row >= m_TotalLines || m_Items.empty())
            return -1;
        // 找到第一个起始物理行 > row 的 item，其前一个即所属
        auto it = std::upper_bound(m_LineStartIndex.begin(),
                                   m_LineStartIndex.end(), row);
        int idx = (int)(it - m_LineStartIndex.begin()) - 1;
        if (idx < 0 || idx >= (int)m_Items.size())
            return -1;
        return idx;
    }

    void ListBox::OnInput(UIInputEvent &e)
    {
        auto *mouse = dynamic_cast<UIMouseEvent *>(&e);
        if (!mouse || mouse->action != MouseAction::Press)
            return;

        const int index = GetRowFromMouseY(e.y);
        if (index < 0)
            return;

        SetSelectedIndex(index);
        e.Handled = true;
    }

    void ListBox::OnPaint(Canvas &canvas)
    {
        // 惰性折叠：宽度变化 / 有新增时重算（需 Canvas 测宽）
        EnsureFold(canvas);

        // 组件坐标一律【相对所属容器】(0,0 起画)；位置偏移由宿主压 canvas origin 处理。
        const int x = 0, y = 0, w = GetWidth();
        const int total = m_TotalLines;
        if (total <= 0)
            return;

        // 可视裁剪：只画屏上可视物理行 [rowFirst, rowLast)
        int rowFirst = 0;
        int rowLast = total;
        if (m_ViewHeight > 0 && m_LineHeight > 0)
        {
            rowFirst = m_ViewOffset / m_LineHeight;
            rowLast = (m_ViewOffset + m_ViewHeight) / m_LineHeight + 1;
            if (rowFirst < 0)
                rowFirst = 0;
            if (rowLast > total)
                rowLast = total;
            if (rowFirst > rowLast)
                rowFirst = rowLast;
        }

        if (m_Items.empty())
            return;

        // 可视物理行范围 → 涉及的 item 范围（折行后一个 item 跨多行，需整 item 画）
        auto itemOf = [&](int row)
        {
            auto it = std::upper_bound(m_LineStartIndex.begin(),
                                       m_LineStartIndex.end(), row);
            int idx = (int)(it - m_LineStartIndex.begin()) - 1;
            if (idx < 0)
                idx = 0;
            if (idx >= (int)m_Items.size())
                idx = (int)m_Items.size() - 1;
            return idx;
        };
        int itemFirst = itemOf(rowFirst);
        int itemLast = (rowLast > 0) ? itemOf(rowLast - 1) : itemFirst;
        if (itemLast < itemFirst)
            itemLast = itemFirst;

        // 逐 item 画其所有物理行段
        for (int i = itemFirst; i <= itemLast; i++)
        {
            const std::vector<std::string> &segs = m_Fold[i];
            if (segs.empty())
                continue; // 该 item 被丢弃（窗口过窄）

            int segCount = (int)segs.size();
            int itemStart = m_LineStartIndex[i];
            uint32_t bgColor = m_Items[i].bgColor; // 默认窗口深底
            if (i == m_SelectedIndex)
                bgColor = m_SelectedColor;

            for (int s = 0; s < segCount; s++)
            {
                int rowY = y + (itemStart + s) * m_LineHeight;

                // 组合拳：铺行背景 + 同背景色写字。
                // 背景盖整行高；文字起点带缩进 (x+4, rowY+2)。
                auto &font = FontLibrary::Instance().GetDefault();
                canvas.FillText(font, x, rowY, w, m_LineHeight, x + 4,
                                rowY + 2, segs[s].c_str(),
                                m_Items[i].textColor, bgColor);
            }
        }
    }

}
