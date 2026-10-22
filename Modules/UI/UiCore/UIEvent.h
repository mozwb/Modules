#pragma once
#include "Input/Input.h"

namespace X_Y
{

// ============================================================
// UIInputEvent — 命中路由下传的事件对象。
// 设计要点：
//   - 命中路由时，事件对象沿 Dock→Panel→Component 下传；
//   - 每层处理完设 Handled=true 即"吞掉"，分发停止，父级不再收到（冒泡控制）；
//   - x/y 是【当前路由层的局部坐标】（命中下钻时逐层减偏移）。
// ============================================================
struct UIInputEvent
{
    int x = 0, y = 0;
    bool Handled = false;   // 处理者设 true = 吞掉，停止继续传
    virtual ~UIInputEvent() = default;
};

// 鼠标动作类型（区分按下/移动/抬起/滚动）
enum class MouseAction { Press, Move, Release, Scroll };

struct UIMouseEvent : public UIInputEvent
{
    MouseAction action = MouseAction::Move;
    Input::MouseCode button = Input::Mouse::ButtonLeft;  // 有效（Press/Release/Scroll 用）
    int scrollDelta = 0;   // 滚轮增量（单击=1格，>0 向上，<0 向下）
};

// 按键事件
struct UIKeyEvent : public UIInputEvent
{
    Input::KeyCode key = 0;   // 0 = 无（仅字符）
    wchar_t ch = 0;
    bool isChar = false;        // true=字符输入(WM_CHAR)，false=按键(WM_KEYDOWN)
};

} // namespace X_Y
