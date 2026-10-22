#include "UI/Component/ScrollArea.h"

namespace X_Y
{

    void ScrollArea::ClampOffset()
    {
        if (m_ScrollOffset < 0)
            m_ScrollOffset = 0;
        if (m_Content)
        {
            int maxOffset = m_Content->GetHeight() - GetHeight();
            if (maxOffset < 0)
                maxOffset = 0;
            if (m_ScrollOffset > maxOffset)
                m_ScrollOffset = maxOffset;
        }
    }

    void ScrollArea::SetScrollOffset(int offset)
    {
        m_ScrollOffset = offset;
        ClampOffset();
        // 状态变了，主动请求所属窗口重绘（不依赖外部 Ticker 顺带刷新）
        RequestRepaint();
    }

    bool ScrollArea::NeedsScrollbar() const
    {
        if (!m_Content)
            return false;
        return m_Content->GetHeight() > GetHeight();
    }

    // 滑块矩形（ScrollArea 局部坐标）：右侧 kScrollbarWidth 宽，y/h 为局部值

    void ScrollArea::GetThumbRect(int &outY, int &outH) const
    {

        int viewH = GetHeight();
        int contentH = m_Content ? m_Content->GetHeight() : 0;

        if (contentH <= viewH)
        {
            outY = 0;
            outH = viewH;
            return;
        }

        outH = (int)((float)viewH * viewH / contentH);
        if (outH < kMinThumbH)
            outH = kMinThumbH;

        int scrollRange = contentH - viewH;
        int thumbRange = viewH - outH;
        outY = (int)((float)m_ScrollOffset / scrollRange * thumbRange);
    }

    void ScrollArea::DrawScrollbar(Canvas &canvas) const
    {
        if (!NeedsScrollbar())
            return;

        // ScrollArea 是 Component：坐标一律【相对所属 Panel】(0,0 起画)
        const int w = GetWidth(), h = GetHeight();
        const int sbX = w - kScrollbarWidth;

        // 轨道
        canvas.FillRect(sbX, 0, kScrollbarWidth, h, 0xFF2A2A2A);

        int thumbY, thumbH;
        GetThumbRect(thumbY, thumbH);

        canvas.FillRect(sbX, thumbY, kScrollbarWidth, thumbH, 0xFF5A5A5A);
    }

    void ScrollArea::OnPaint(Canvas &canvas)
    {
        if (!m_Content)
            return;

        const int w = GetWidth(), h = GetHeight();
        const int viewW = GetViewWidth();

        // 内容裁到可视区（本组件局部坐标）
        canvas.SetClip(0, 0, w, h);

        // 内容在"本组件本地"的位置：x=0，y=负的滚动偏移。
        // 压入 origin 后，内容也按自己的 (0,0) 起画 —— 嵌套原点自动处理滚动位移。
        canvas.PushOrigin(0, -m_ScrollOffset);

        // 通知内容本次可视范围（供支持可视裁剪的内容如 ListBox 只画可视行）
        m_Content->SetRect(0, 0, viewW, m_Content->GetHeight());
        m_Content->SetViewport(m_ScrollOffset, h);
        m_Content->OnPaint(canvas);

        canvas.PopOrigin();
        canvas.ResetClip();

        DrawScrollbar(canvas);
    }

    // ── 输入（统一入口）：滚轮 / 滑块拖动 / 内容滚动转发 ──
    void ScrollArea::OnInput(UIInputEvent &e)
    {
        // 键盘：下传给内容（若内容聚焦）
        if (auto *ke = dynamic_cast<UIKeyEvent *>(&e))
        {
            if (m_Content && m_Content->IsVisible() && m_Content->IsFocused())
                m_Content->OnInput(e);
            return;
        }

        auto *me = dynamic_cast<UIMouseEvent *>(&e);
        if (!me)
            return;

        const int localX = e.x;
        const int localY = e.y;

        // 滚轮：内容滚动
        if (me->action == MouseAction::Scroll)
        {
            if (m_Content && m_Content->IsVisible() && localX < GetWidth() - kScrollbarHitWidth)
            {
                // 给内容自己先处理（如列表按行滚）；未处理再自己卷
                m_Content->OnInput(e);
                if (!e.Handled)
                    Scroll((float)me->scrollDelta);
            }
            else
            {
                Scroll((float)me->scrollDelta);
            }
            return;
        }

        if (me->action == MouseAction::Press)
        {
            // 命中滑块条竖带 → 滑块/翻页；否则下传内容
            // 命中区比视觉滚动条更宽，避免 8px 滚动条在缩放或取整后难以按中。
            if (NeedsScrollbar() && localX >= GetWidth() - kScrollbarHitWidth)
            {
                int thumbY, thumbH;
                GetThumbRect(thumbY, thumbH);
                if (localY >= thumbY && localY < thumbY + thumbH)
                {
                    m_DraggingThumb = true;
                    m_DragOffsetY = localY - thumbY;
                }
                else
                {
                    int viewH = GetHeight();
                    int pageStep = viewH - GetScrollStep();
                    if (pageStep < GetScrollStep())
                        pageStep = GetScrollStep();
                    if (localY < thumbY)
                        SetScrollOffset(m_ScrollOffset - pageStep);
                    else
                        SetScrollOffset(m_ScrollOffset + pageStep);
                }
                return;
            }
            // 未命中滑块 → 下传内容
            if (m_Content && m_Content->IsVisible())
            {
                m_Content->SetFocused(true);
                e.y = localY + m_ScrollOffset;
                m_Content->OnInput(e);
                e.y = localY; // 还原
            }
            return;
        }

        if (me->action == MouseAction::Move)
        {
            if (m_DraggingThumb)
            {
                int viewH = GetHeight();
                int contentH = m_Content ? m_Content->GetHeight() : 0;
                if (contentH <= viewH)
                    return;
                int thumbH;
                {
                    int ty;
                    GetThumbRect(ty, thumbH);
                }
                int thumbRange = viewH - thumbH;
                int newThumbY = localY - m_DragOffsetY;
                if (newThumbY < 0)
                    newThumbY = 0;
                if (newThumbY > thumbRange)
                    newThumbY = thumbRange;
                int scrollRange = contentH - viewH;
                SetScrollOffset((int)((float)newThumbY / thumbRange * scrollRange));
                return;
            }
            if (m_Content && m_Content->IsVisible())
            {
                e.y = localY + m_ScrollOffset;
                m_Content->OnInput(e);
                e.y = localY;
            }
            return;
        }

        if (me->action == MouseAction::Release)
        {
            if (m_DraggingThumb)
            {
                m_DraggingThumb = false;
                return;
            }
            if (m_Content && m_Content->IsVisible())
            {
                e.y = localY + m_ScrollOffset;
                m_Content->OnInput(e);
                e.y = localY;
            }
            return;
        }
    }

}
