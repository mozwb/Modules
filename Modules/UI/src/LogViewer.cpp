#include "../panel/LogViewer.h"
#include "DataStore/DataStore.h"
#include "Timer/Timer.h"
#include "Widget/BaseWin.h"
#include "Widget/FontLibrary.h"
#include <algorithm>

namespace X_Y
{

    void LogStripe::SetEntries(const std::vector<LogEntry> &entries)
    {
        Clear();
        for (const auto &entry : entries)
        {
            AddItem(entry.text.c_str(), entry.color, entry.bgColor);
        }
    }

    // ════════════════════════════════════════════════════════════
    // 工具：解析单行日志
    // ════════════════════════════════════════════════════════════

    // 解析 Log 写入的 ANSI 颜色序列，剥离控制字符，提取前景色
    // 支持的序列：\x1B[38;2;R;G;Bm(前景RGB) / \x1B[...m(其他，仅剥离)
    // 注意：\x1B[0m(重置) 不覆盖行色——Log 每行结构为 <颜色>正文<重置>，
    //       行色按该行设置的第一个前景色为准，结尾重置只是收尾，不把整行抹黑。
    static LogEntry ParseLine(const std::string &raw)
    {
        LogEntry entry;
        entry.color = 0xFF000000;
        bool hasColor = false;

        std::string text;
        text.reserve(raw.size());

        size_t i = 0;
        const size_t n = raw.size();
        while (i < n)
        {
            if (raw[i] == '\x1B' && i + 1 < n && raw[i + 1] == '[')
            {
                size_t j = i + 2;
                while (j < n && raw[j] != 'm')
                    j++; // 找到序列结束符 'm'
                if (j >= n)
                    break; // 未闭合，丢弃剩余

                std::string esc = raw.substr(i + 2, j - i - 2); // 不含 ESC[ 和 m 的参数字符串
                i = j + 1;

                // \x1B[0m 重置：只作为收尾标记，不覆盖已定的行色
                if (esc == "0" || esc == "0;0")
                    continue;

                // 前景色 RGB：38;2;R;G;B
                if (esc.rfind("38;2;", 0) == 0)
                {
                    int r = 0, g = 0, b = 0;
                    if (sscanf_s(esc.c_str() + 5, "%d;%d;%d", &r, &g, &b) == 3)
                    {
                        entry.color = 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
                        hasColor = true;
                    }
                }
                // 其他序列(背景/粗体等)仅剥离，不改变颜色
                continue;
            }

            text += raw[i];
            i++;
        }

        entry.text = std::move(text);
        return entry;
    }

    class LogViewer::TagStrip final : public Component
    {
    public:
        explicit TagStrip(LogViewer *owner) : m_Owner(owner) {}

        void OnPaint(Canvas &canvas) override { m_Owner->PaintTags(canvas); }

        void OnInput(UIInputEvent &e) override
        {
            auto *mouse = dynamic_cast<UIMouseEvent *>(&e);
            if (!mouse)
                return;

            const int index = m_Owner->HitTestTag(e.x, e.y);
            if (mouse->action == MouseAction::Move && index != m_Owner->m_TagHoverIndex)
            {
                m_Owner->m_TagHoverIndex = index;
                m_Owner->RequestRepaint();
            }
            else if (mouse->action == MouseAction::Press &&
                     index >= 0 && m_Owner->IsInTagClose(index, e.x, e.y))
            {
                m_Owner->OnTagRemoved(m_Owner->m_Keywords[static_cast<std::size_t>(index)]);
                m_Owner->m_TagHoverIndex = -1;
                m_Owner->RequestRepaint();
                e.Handled = true;
            }
        }

    private:
        LogViewer *m_Owner;
    };

    // ════════════════════════════════════════════════════════════
    // 构造 / 析构
    // ════════════════════════════════════════════════════════════

    LogViewer::LogViewer()
    {
        m_Key = SysClock::NowFormat("YY-MM-DD") + ".log";

        m_TagStrip = new TagStrip(this);
        m_KeywordInput = new TextInput();
        m_KeywordInput->SetPlaceholder("输入关键词，回车添加到筛选");
        // 输入框回车 → 添加为筛选规则
        m_KeywordInput->OnEnter = [this]()
        {
            OnEnterKeyword();
        };

        m_LogStripe = std::make_unique<LogStripe>();

        m_ScrollArea = new ScrollArea();
        m_ScrollArea->SetContent(m_LogStripe.get());

        // ★ 所有权交给 Panel（AddComponent 接管，析构时由 Panel 释放）；
        //   本类成员只留裸借用指针。
        AddComponent(m_TagStrip);
        AddComponent(m_KeywordInput);
        AddComponent(m_ScrollArea);
    }

    LogViewer::~LogViewer()
    {
        Stop();
    }

    // ════════════════════════════════════════════════════════════
    // 配置
    // ════════════════════════════════════════════════════════════

    void LogViewer::SetDataStoreKey(const std::string &key)
    {
        {
            std::lock_guard<std::shared_mutex> lock(m_EntriesMutex);
            m_Key = key;
            m_LastSize = 0;
            m_AllEntries.Clear();
            m_LogStripe->Clear(); // 换数据源 → stripe 同步清空
            m_RenderedSeq = 0;    // 序号归零
        }
        RequestRepaint();
    }

    // ════════════════════════════════════════════════════════════
    // 生命周期
    // ════════════════════════════════════════════════════════════

    void LogViewer::Start()
    {
        if (m_Timer)
            return;

        m_Timer = std::make_unique<Ticker>();
        m_Timer->Start(500, [this]()
                       {
        Buffer buf = DataStore::Instance().ReadCopy(m_Key);
        if (!buf || !buf.Data || buf.Size == 0) return;

        uint64_t currentSize = buf.Size;

        std::lock_guard<std::shared_mutex> lock(m_EntriesMutex);

        if (currentSize < m_LastSize) {
            m_LastSize = 0;
        }

        if (currentSize == m_LastSize) return;

        auto tailLen = currentSize - m_LastSize;
        auto* bytes = reinterpret_cast<const char*>(buf.Data) + m_LastSize;
        m_LastSize = currentSize;

        // 入队前的全局序号，用于增量喂入范围
        uint64_t before = m_AllEntries.TotalPushed();
        bool queueWasFull = m_AllEntries.Full();

        std::string current;
        for (uint64_t i = 0; i < tailLen; i++) {
            char c = bytes[i];
            if (c == '\n') {
                if (!current.empty()) {
                    m_AllEntries.Push(ParseLine(current));
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            m_AllEntries.Push(ParseLine(current));
        }

        uint64_t after = m_AllEntries.TotalPushed();
        uint64_t newEntries = after - before;
        if ((queueWasFull || newEntries >= MAX_ENTRIES) && newEntries > 0)
            RebuildAll();
        else
            IncrementalAppend(before, after); });
    }

    void LogViewer::Stop()
    {
        if (!m_Timer)
            return;
        m_Timer->Stop();
        m_Timer->Join();
        m_Timer.reset();
    }

    // ════════════════════════════════════════════════════════════
    // 筛选（持有共享锁时调用）
    // ════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════
    // 筛选（必须在 m_EntriesMutex 持有锁时调用）
    // ════════════════════════════════════════════════════════════

    // 拆一个筛选 tag 的 AND 子项：按 "&&" 分割，去除空白。
    // 示例："error && info" → {"error","info"}；"error" → {"error"}
    static std::vector<std::string> SplitAndParts(const std::string &tag)
    {
        std::vector<std::string> parts;
        std::string cur;
        const size_t n = tag.size();
        size_t i = 0;
        // 逐段找 "&&"
        while (i < n)
        {
            size_t found = tag.find("&&", i);
            if (found == std::string::npos)
            {
                cur = tag.substr(i);
                i = n;
            }
            else
            {
                cur = tag.substr(i, found - i);
                i = found + 2;
            }
            // 去首尾空白（空格/Tab），空段忽略
            size_t b = cur.find_first_not_of(" 	");
            size_t e2 = cur.find_last_not_of(" 	");
            if (b != std::string::npos && e2 >= b)
                parts.push_back(cur.substr(b, e2 - b + 1));
            cur.clear();
        }
        return parts;
    }

    // 判断单条是否命中当前筛选：
    //   tag 之间 OR（命中任一 tag 即通过）；单个 tag 内用 "&&" 表达 AND（需同时命中所有子项）
    //   （区分大小写，严格子串匹配）
    bool LogViewer::MatchesKeyword(const LogEntry &e) const
    {
        if (m_Keywords.empty())
            return true;
        for (const auto &kw : m_Keywords)
        {
            std::vector<std::string> parts = SplitAndParts(kw);
            if (parts.empty())
                continue;
            bool all = true;
            for (const auto &part : parts)
            {
                if (part.empty() || e.text.find(part) == std::string::npos)
                {
                    all = false;
                    break;
                }
            }
            if (all)
                return true;
        }
        return false;
    }

    // 全量重建：清空 stripe，从队头重筛全部。仅关键词变化 / 数据重置时调用。
    void LogViewer::RebuildAll()
    {
        m_LogStripe->Clear();
        const size_t cnt = m_AllEntries.Size();
        for (size_t i = 0; i < cnt; i++)
        {
            const LogEntry &e = m_AllEntries[i]; // 相对索引，0 = 最旧
            if (MatchesKeyword(e))
                m_LogStripe->AddItem(e.text.c_str(), e.color, e.bgColor);
        }
        m_RenderedSeq = m_AllEntries.TotalPushed();
        // 全量重建同样按 MAX_ENTRIES 封顶：队列本身最多 5000 条，
        // 单关键词去掉筛选后最多也就是 5000 —— 这里主要是语义上的对称保险。
        TrimStripe();
        RequestRepaint();
    }

    // 增量喂入：把全局序号 [fromSeq, toSeq) 的新条目追加到 stripe（含 filter）。
    // 锁内调用（Ticker 排他锁）。
    void LogViewer::IncrementalAppend(uint64_t fromSeq, uint64_t toSeq)
    {
        if (toSeq <= fromSeq)
            return;
        for (uint64_t i = fromSeq; i < toSeq; i++)
        {
            const LogEntry &e = m_AllEntries.At(i); // 按全局序号
            if (MatchesKeyword(e))
                m_LogStripe->AddItem(e.text.c_str(), e.color, e.bgColor);
        }
        m_RenderedSeq = toSeq;
        TrimStripe();
        RequestRepaint();
    }

    // 淘汰 stripe 头部多余条目，保持它不超过 MAX_ENTRIES。
    //
    // ⚠️ 为什么必须有这个：m_AllEntries（LoopQueue<LogEntry,5000>）是环形的、
    //    满了自动覆盖最旧 —— 它自己内存恒定。但 m_LogStripe 是 std::vector，
    //    只加不删：上游封顶、下游不封顶。
    //    ⇒ 程序开几小时，stripe 累积到几十万条，内存全在这儿；
    //      且 RebuildAll 全量重建时峰值翻倍 + 明显卡顿。
    //    MAX_ENTRIES 过去只管住了 m_AllEntries，管不到 stripe —— 这里补上。
    //
    // 淘汰放在【关键词过滤之后】：stripe 里存的是"已通过筛选的那部分"，
    // 所以按 stripe 自己的条数封顶，与 m_AllEntries 的 5000 是两个独立上限。
    void LogViewer::TrimStripe()
    {
        const int overflow = m_LogStripe->GetItemCount() - static_cast<int>(MAX_ENTRIES);
        if (overflow > 0)
            m_LogStripe->RemoveFirst(overflow);
    }

    // ════════════════════════════════════════════════════════════
    // 交互回调
    // ════════════════════════════════════════════════════════════

    // 输入框回车：把当前输入作为一条筛选规则添加，并清空输入框
    void LogViewer::OnEnterKeyword()
    {
        std::string kw = m_KeywordInput->GetText();
        // 空关键词忽略
        if (kw.empty())
            return;

        std::lock_guard<std::shared_mutex> lock(m_EntriesMutex);
        // 去重：已存在同样的关键词则不重复添加
        for (const auto &k : m_Keywords)
            if (k == kw)
                return;

        m_Keywords.push_back(kw);
        RebuildAll();

        m_KeywordInput->SetText("");
    }

    // 删除某条筛选规则并重筛
    void LogViewer::OnTagRemoved(const std::string &tag)
    {
        std::lock_guard<std::shared_mutex> lock(m_EntriesMutex);
        auto it = std::find(m_Keywords.begin(), m_Keywords.end(), tag);
        if (it != m_Keywords.end())
            m_Keywords.erase(it);
        RebuildAll();
    }

    int LogViewer::MeasureTags()
    {
        m_TagRects.clear();
        m_TagCloseRects.clear();
        if (m_Keywords.empty())
            return 0;

        const Font &font = FontLibrary::Instance().GetDefault();
        const int contentWidth = std::max(0, GetWidth());
        const int closeSize = 12;
        const int closePad = 3;
        const int textPadLeft = 8;
        const int textPadRight = closeSize + closePad * 3;
        const int rowHeight = m_TagHeight;

        int x = 0;
        int y = 0;
        int rows = 1;
        for (const std::string &keyword : m_Keywords)
        {
            const int textWidth = font.MeasureText(keyword.c_str());
            const int tagWidth = std::max(rowHeight, textWidth + textPadLeft + textPadRight);
            if (x > 0 && x + tagWidth > contentWidth)
            {
                x = 0;
                y += rowHeight + m_TagGap;
                ++rows;
            }

            m_TagRects.push_back({x, y + 2, tagWidth, rowHeight});
            m_TagCloseRects.push_back({x + tagWidth - closeSize - closePad,
                                       y + 2 + closePad, closeSize, closeSize});
            x += tagWidth + m_TagGap;
        }

        return rows * rowHeight + (rows - 1) * m_TagGap + 4;
    }

    int LogViewer::HitTestTag(int x, int y) const
    {
        for (std::size_t i = 0; i < m_TagRects.size(); ++i)
        {
            const TagRect &rect = m_TagRects[i];
            if (x >= rect.x && x < rect.x + rect.w &&
                y >= rect.y && y < rect.y + rect.h)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool LogViewer::IsInTagClose(int index, int x, int y) const
    {
        if (index < 0 || index >= static_cast<int>(m_TagCloseRects.size()))
            return false;

        const TagRect &rect = m_TagCloseRects[index];
        return x >= rect.x && x < rect.x + rect.w &&
               y >= rect.y && y < rect.y + rect.h;
    }

    void LogViewer::PaintTags(Canvas &canvas)
    {
        const Font &font = FontLibrary::Instance().GetDefault();
        for (std::size_t i = 0; i < m_Keywords.size(); ++i)
        {
            const TagRect &tag = m_TagRects[i];
            const bool hovered = static_cast<int>(i) == m_TagHoverIndex;
            canvas.FillRoundRect(tag.x, tag.y, tag.w, tag.h, 6,
                                 hovered ? 0xFF2F6F9F : 0xFF3C3C46);
            canvas.DrawText(font, tag.x + 8, tag.y + (tag.h - 14) / 2,
                            m_Keywords[i].c_str(), 0xFFE8E8E8);

            const TagRect &close = m_TagCloseRects[i];
            canvas.FillRoundRect(close.x, close.y, close.w, close.h, 3,
                                 hovered ? 0xFF6A7280 : 0xFF555A63);
            canvas.DrawText(font, close.x + 2, close.y - 1, "x", 0xFFEEEEEE);
        }
    }

    // ════════════════════════════════════════════════════════════
    // 绘制（主线程）
    // ════════════════════════════════════════════════════════════

    void LogViewer::OnPaint(Canvas &canvas)
    {
        MeasureTags();
        OnLayout();

        // LogViewer 是 Panel：坐标一律【Panel 局部】(0,0 起画)。
        // 宿主（Dock）已经把 canvas origin 压到本 Panel 的位置了，
        // 所以这里不再需要 GetX()/GetY() 那层手工偏移。
        canvas.FillRect(0, 0, GetWidth(), GetHeight(), 0xFF1E1E1E);
        canvas.FillRect(m_ScrollArea->GetX(),
                        m_ScrollArea->GetY(),
                        m_ScrollArea->GetWidth(),
                        m_ScrollArea->GetHeight(),
                        0xFF000000);

        // 共享锁：保护 m_LogStripe（Ticker 线程可能正增量写）以及绘制
        {
            std::shared_lock<std::shared_mutex> lock(m_EntriesMutex);
            Panel::OnPaint(canvas);
        }
    }

    // ════════════════════════════════════════════════════════════
    // 布局
    // ════════════════════════════════════════════════════════════

    void LogViewer::OnLayout()
    {
        int w = GetWidth();
        int h = GetHeight();
        if (w <= 0 || h <= 0)
            return;

        MeasureTags();

        // 输入框：筛选标签条之下
        // 这里的w应该预留出来m_ScrollArea滑块的宽度
        const int tagHeight = m_TagRects.empty() ? 0 : m_TagRects.back().y + m_TagRects.back().h + 2;
        m_TagStrip->SetRect(0, 0, w, tagHeight);
        const int inputH = 22;
        const int gap = 2;
        int inputY = tagHeight + gap;

        // 日志区：占满剩余
        int panelY = inputY + inputH + gap;
        int panelH = std::max(0, h - panelY);
        m_ScrollArea->SetRect(0, panelY, w, panelH);

        w = m_ScrollArea->GetViewWidth();
        m_KeywordInput->SetRect(0, inputY, w, inputH);
    }

}
// namespace X_Y
