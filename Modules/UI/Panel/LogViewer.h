#pragma once
#include "UI/Panel/Panel.h"
#include "UI/Component/ListBox.h"
#include "UI/Component/ScrollArea.h"
#include "UI/Component/TextInput.h"
#include "Memory/Memory.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <shared_mutex>

namespace X_Y
{

    class Ticker;

    // ── LogEntry ──
    struct LogEntry
    {
        std::string text;
        uint32_t color = 0xFF000000;   // 默认黑色 ARGB
        uint32_t bgColor = 0xFF000000; // 默认黑色 ARGB
    };

    // ── LogStripe ──
    class LogStripe : public ListBox
    {
    public:
        LogStripe() = default;
        void SetEntries(const std::vector<LogEntry> &entries);
    };

    class LogViewer : public Panel
    {
    public:
        LogViewer();
        ~LogViewer() override;

        void SetDataStoreKey(const std::string &key);
        const std::string &GetDataStoreKey() const { return m_Key; }

        void Start();
        void Stop();

    protected:
        void OnLayout() override;
        void OnPaint(Canvas &canvas) override;

    private:
        class TagStrip;

        // 输入框回车 → 把当前输入文本添加为一条筛选规则（tag）
        void OnEnterKeyword();
        // 某个 tag 被点击 × 删除
        void OnTagRemoved(const std::string &tag);
        int MeasureTags();
        void PaintTags(Canvas &canvas);
        int HitTestTag(int x, int y) const;
        bool IsInTagClose(int index, int x, int y) const;
        bool MatchesKeyword(const LogEntry &e) const;             // 命中任一关键词（OR）即通过
        void RebuildAll();                                        // 全量重建（关键词变 / 数据重置时）
        void IncrementalAppend(uint64_t fromSeq, uint64_t toSeq); // 增量喂新条目
        void TrimStripe();                                        // 淘汰 stripe 头部，封顶 MAX_ENTRIES
        // ⚠️ 借用指针（裸）：这三个组件由【基类 Panel 拥有】——
        //    构造时 AddComponent 接管所有权，Panel 析构时统一释放。
        //    本类只借用访问，绝不 delete（否则与 Panel 双重释放 → 崩溃）。
        TextInput *m_KeywordInput = nullptr;
        TagStrip *m_TagStrip = nullptr;
        ScrollArea *m_ScrollArea = nullptr;
        // m_LogStripe 不进 Panel 树，仅被 m_ScrollArea 借用 → 由本类 unique_ptr 拥有。
        std::unique_ptr<LogStripe> m_LogStripe;

        static constexpr uint64_t MAX_ENTRIES = 5000;

        LoopQueue<LogEntry, MAX_ENTRIES> m_AllEntries;
        mutable std::shared_mutex m_EntriesMutex;

        std::string m_Key;
        std::vector<std::string> m_Keywords; // 已确认的筛选关键词（OR 语义）
        struct TagRect
        {
            int x, y, w, h;
        };
        std::vector<TagRect> m_TagRects;
        std::vector<TagRect> m_TagCloseRects;
        int m_TagHeight = 24;
        int m_TagGap = 6;
        int m_TagHoverIndex = -1;
        uint64_t m_LastSize = 0;
        uint64_t m_RenderedSeq = 0; // 已喂入 stripe 的全局序号

        std::unique_ptr<Ticker> m_Timer;
    };

}
// namespace X_Y
