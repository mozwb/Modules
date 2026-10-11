#pragma once
#include "Component.h"
#include <string>
#include <vector>
#include <cstdint>

namespace X_Y
{

    struct ListBoxItem
    {
        std::string text;
        uint32_t textColor = 0xFF000000;
        uint32_t bgColor = 0xFFFFFFFF;
        void *userData = nullptr;
    };

    // 折行模式（扫码 A/B 行为，由使用方挑选）
    //   Wrap   ：自动折行（不定高，长行按可用宽切多物理行）—— 终端式
    //   NoWrap ：不折行（每行固定一物理行，超出部分横向滚动，需 ScrollArea 补横向能力）
    enum class WrapMode
    {
        Wrap,
        NoWrap,
    };

    class ListBox : public Component
    {
    public:
        ListBox() = default;

        void AddItem(const char *text, uint32_t textColor = 0xFF000000, uint32_t bgColor = 0xFFFFFFFF);
        void Clear();

        // 淘汰最旧的 n 条（从头部删）。用于"只增列表"封顶：
        // LogViewer 这类滚动日志只往尾部追加，不封顶会无限涨内存。
        // n <= 0 或 n >= 条数时按实际条数删（删空了等价于 Clear 的内容部分）。
        // ⚠️ 会重置折行游标（下标全部前移，旧折行缓存作废）→ 下次 OnPaint 全量重折。
        void RemoveFirst(int n);

        int GetItemCount() const { return (int)m_Items.size(); }
        const ListBoxItem &GetItem(int index) const { return m_Items[index]; }

        // 某逻辑 item 占几个物理行（折行结果）
        int GetItemLineCount(int index) const;

        // 总物理行数（滚动高度 = 总物理行 × 行高）
        int GetTotalLines() const { return m_TotalLines; }

        int GetSelectedIndex() const { return m_SelectedIndex; }
        void SetSelectedIndex(int idx)
        {
            if (m_SelectedIndex == idx)
                return;
            m_SelectedIndex = idx;
            RequestRepaint();
        }
        void SetSelectedColor(uint32_t color)
        {
            if (m_SelectedColor == color)
                return;
            m_SelectedColor = color;
            RequestRepaint();
        }

        int GetLineHeight() const { return m_LineHeight; }
        void SetLineHeight(int h);

        // 折行模式（默认 Wrap）。NoWrap 需 ScrollArea 支持横向滚动。
        void SetWrapMode(WrapMode mode);
        WrapMode GetWrapMode() const { return m_WrapMode; }

        int GetScrollStep() const override { return m_LineHeight; }

        // 记录滚动视口，OnPaint 只画可视区行
        void SetViewport(int scrollOffset, int viewHeight) override
        {
            m_ViewOffset = scrollOffset;
            m_ViewHeight = viewHeight;
        }

        // 鼠标 y → 命中的逻辑 item 索引（-1 = 未命中）
        int GetRowFromMouseY(int localY) const;

        void OnInput(UIInputEvent &e) override;
        void OnPaint(Canvas &canvas) override;

    private:
        // UTF-8 单字符字节数（按首字节前导判断；非法字节按 1）
        static int Utf8CharLen(unsigned char lead);

        // 折一行（按当前 m_LastFoldWidth 切段），把段文本写入 m_Fold[idx]
        void FoldItem(size_t idx, Canvas &canvas);

        // 惰性折叠：OnPaint 里驱动。宽度变化全量重折；否则只补新增条目。
        void EnsureFold(Canvas &canvas);

        // 重建 m_LineStartIndex（每 item 的起始物理行，前缀和）与 m_TotalLines
        void BuildLineIndex();

        void UpdateHeight();

        std::vector<ListBoxItem> m_Items;
        std::vector<std::vector<std::string>> m_Fold; // 每 item 的折行段（物理行文本，UTF-8）
        std::vector<int> m_LineStartIndex;            // item 的起始物理行（前缀和）
        int m_TotalLines = 0;                         // 全部物理行数

        WrapMode m_WrapMode = WrapMode::Wrap;
        int m_LastFoldWidth = -1; // 上次折行时的内容宽（用于检测宽度变化）
        int m_FoldedCount = 0;    // 已惰性折叠的 item 数（增量折行游标）

        int m_SelectedIndex = -1;
        uint32_t m_SelectedColor = 0xFFFEFEFE;
        int m_LineHeight = 20; // 物理行高（文字带 + 行距）
        int m_LineSpacing = 4; // 行距（文字带之间留白，防背景吞 descender）
        int m_ViewOffset = 0;  // 滚动偏移（像素）
        int m_ViewHeight = 0;  // 可视区高（像素）
    };

}
