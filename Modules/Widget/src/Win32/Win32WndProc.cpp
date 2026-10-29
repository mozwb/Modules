#include "Win32/Win32WndProc.h"
#include "Win32/Win32Globals.h"
#include "Widget/BaseWin.h"
#include "APP/Application.h"
#include "Widget/Movement/Movements.h"
#include "Widget/Movement/AppMovement.h"
#include "Widget/Movement/KeyMovement.h"
#include "Widget/Movement/MouseMovement.h"
#include "Widget/Canvas.h"
#include "Widget/Dpi.h"

namespace X_Y::Win32
{
    using InputMapping = Input;
    LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        using BaseWin = X_Y::BaseWin;

        // 1. WndProc hook（如 ImGui 输入优先处理）

        if (g_WndProcHook && g_WndProcHook(hwnd, msg, wParam, lParam))
            return true;

        BaseWin *pThis = nullptr;
        auto *app = App::Application::instance();

        // 2. 窗口创建时：绑定 C++ 对象与 HWND，记录初始尺寸

        if (msg == WM_NCCREATE)
        {
            pThis = (BaseWin *)((CREATESTRUCT *)lParam)->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

            RECT rect;
            if (GetClientRect(hwnd, &rect))
            {
                // 物理客户区 → 逻辑尺寸（DPI 缩放），存进 XWidget 供上层逻辑布局
                float s = Dpi::GetScale();
                pThis->SetActualSize((int)(rect.right / s), (int)(rect.bottom / s));
            }

            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        else
        {
            pThis = (BaseWin *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        // 3. WM_PAINT：创建 Canvas 后通过虚函数 OnPaint 回调

        if (msg == WM_PAINT && pThis)
        {
            // 如果窗口标记为独立线程自绘，主线程跳过
            if (pThis->m_SkipMainThreadPaint)
            {
                ValidateRect(hwnd, nullptr);
                return 0;
            }

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);
            int w = rect.right - rect.left;
            int h = rect.bottom - rect.top;

            // 关键修正：CanvasImplSoftware 接受的是 HWND，而不是 HDC。
            // 若把 HDC 传给它，Flush() 里会拿到错误的窗口句柄，导致上屏失败，窗口停留白屏。
            Canvas canvas(w, h, (void *)hwnd);
            pThis->OnPaint(&canvas);
            // 双缓冲：把内存位图一次性上屏
            canvas.Flush();

            EndPaint(hwnd, &ps);
            return 0;
        }

        // 双缓冲：我们自己每帧全覆盖重绘，不需要系统用白刷擦背景
        // 拦截 WM_ERASEBKGND，返回非 0 告知系统“背景已处理”，消除滚动/刷新时的白屏闪烁
        if (msg == WM_ERASEBKGND && pThis)
        {
            return 1;
        }

        Movement *movement = nullptr;
        KeyCode key = NULL;
        MouseCode mbutton = NULL;

        // 4. 如果对象存在，把 Win32 消息转换成 MovementType 事件
        if (pThis)
        {
            switch (msg)
            {
            case WM_CLOSE:
            {
                movement = new WindowClose(pThis);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_DESTROY:
            {
                movement = new WindowDestroy(pThis);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_SIZE:
            {
                int width = LOWORD(lParam); // 物理像素
                int height = HIWORD(lParam);
                // 物理 → 逻辑（DPI 缩放），存进 XWidget + 上层布局用逻辑
                float s = Dpi::GetScale();
                width = (int)(width / s);
                height = (int)(height / s);
                pThis->SetActualSize(width, height);
                movement = new WindowResize(pThis, width, height);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_MOVE:
            {
                movement = new WindowMoved(pThis);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_SETFOCUS:
            {
                movement = new WindowFouces(pThis);
                app->GetEventQueue().Push(movement);
                return 0;
            }

            // ── 自定义拖拽：不走系统拖拽循环 ──────────────
            case WM_NCLBUTTONDOWN:
            {
                if (wParam == HTCAPTION)
                {
                    // 只拦截标题栏拖拽，走自定义逻辑
                    POINT pt;
                    GetCursorPos(&pt);
                    RECT rc;
                    GetWindowRect(hwnd, &rc);
                    pThis->m_DragOffsetX = pt.x - rc.left;
                    pThis->m_DragOffsetY = pt.y - rc.top;
                    pThis->m_IsDragging = true;

                    SetCapture(hwnd);

                    // 告知下层：拖拽已开始
                    movement = new WindowDragBegin(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                // 非 HTCAPTION（关闭、最小化、resize 等）交给系统处理
                break;
            }

            // WM_EXITSIZEMOVE 不再需要，系统不会进入拖拽循环了
            case WM_EXITSIZEMOVE:
            {
                return 0;
            }

            // ── 鼠标移动：既是窗口拖动也是正常事件 ──────────
            case WM_MOUSEMOVE:
            {
                float x = (short)LOWORD(lParam);
                float y = (short)HIWORD(lParam);

                // 如果正在自定义拖拽，移动窗口跟随鼠标
                if (pThis->m_IsDragging)
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    SetWindowPos(hwnd, NULL,
                                 pt.x - pThis->m_DragOffsetX,
                                 pt.y - pThis->m_DragOffsetY,
                                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                }

                // 无论如何都派发 MouseMoved，让 Docker 等能更新预览
                movement = new MouseMoved(pThis, x, y);
                app->GetEventQueue().Push(movement);
                return 0;
            }

            // ── 鼠标抬起：结束拖拽或正常事件 ──────────────
            case WM_LBUTTONUP:
            {
                if (pThis->m_IsDragging)
                {
                    pThis->m_IsDragging = false;
                    ReleaseCapture();

                    movement = new WindowDragEnd(pThis);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }

                // 非拖拽时走正常事件 + DefWindowProc
                mbutton = InputMapping::TranslateMouse(VK_LBUTTON);
                movement = new MouseButtonReleased(pThis, mbutton,
                                                   (float)(short)LOWORD(lParam),
                                                   (float)(short)HIWORD(lParam));
                app->GetEventQueue().Push(movement);
                break;
            }

            // ── 常规按键 / 鼠标事件 ──────────────────────────
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            {
                bool isRepeat = (lParam & (1 << 30)) != 0;
                key = InputMapping::Translate(wParam);
                movement = new KeyPressed(pThis, key, isRepeat);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP:
            {
                key = InputMapping::Translate(wParam);
                movement = new KeyReleased(pThis, key);
                app->GetEventQueue().Push(movement);
                return 0;
            }

            // 字符输入：WM_CHAR 的 wParam 是 Unicode 字符码（非 VK 键码），
            // 已由系统完成按键组合/输入法/键盘布局的翻译，直接作为字符传入。
            // 不做按键映射（Translate 只适用于"哪个键被按"，这里是"输入了什么字"）。
            case WM_CHAR:
            {
                // 过滤控制字符（回车/退格等已由 WM_KEYDOWN → OnKeyDown 处理，
                // 这里只放行可见字符：>=32 且非 DEL(127)）
                WPARAM wc = wParam;
                if (wc >= 32 && wc != 127)
                {
                    movement = new KeyTyped(pThis, (KeyCode)wc);
                    app->GetEventQueue().Push(movement);
                    return 0;
                }
                break;
            }
            case WM_LBUTTONDOWN:
            {
                mbutton = InputMapping::TranslateMouse(VK_LBUTTON);
                movement = new MouseButtonPressed(pThis, mbutton,
                                                  (float)(short)LOWORD(lParam),
                                                  (float)(short)HIWORD(lParam));
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_RBUTTONDOWN:
            {
                mbutton = InputMapping::TranslateMouse(VK_RBUTTON);
                movement = new MouseButtonPressed(pThis, mbutton,
                                                  (float)(short)LOWORD(lParam),
                                                  (float)(short)HIWORD(lParam));
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_RBUTTONUP:
            {
                mbutton = InputMapping::TranslateMouse(VK_RBUTTON);
                movement = new MouseButtonReleased(pThis, mbutton,
                                                   (float)(short)LOWORD(lParam),
                                                   (float)(short)HIWORD(lParam));
                app->GetEventQueue().Push(movement);
                return 0;
            }
            case WM_MOUSEWHEEL:
            {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                float yOffset = static_cast<float>(delta) / WHEEL_DELTA;
                // ⚠️ WM_MOUSEWHEEL 的 lParam 是【屏幕坐标】（与 WM_MOUSEMOVE 的
                //    客户区坐标不同），必须先转客户区。
                //    但只能用 ScreenToClientPhysical（物理→物理），不能用 ScreenToClient
                //    —— 后者会顺带 ÷scale 转成逻辑坐标，而上层 Container 统一会对
                //    鼠标事件做 ClientPhysicalToLogical，那就会【重复缩放】。
                //    在 150% DPI 下坐标会被多除一次 1.5，表现为滚轮位置偏左上，
                //    于是"鼠标在底部/右侧 Dock 上滚轮没反应"（坐标被算到了别的 Dock）。
                int mouseX = (short)LOWORD(lParam);
                int mouseY = (short)HIWORD(lParam);
                pThis->ScreenToClientPhysical(mouseX, mouseY);
                movement = new MouseScrolled(pThis, 0.0, yOffset,
                                             (float)mouseX, (float)mouseY);
                app->GetEventQueue().Push(movement);
                return 0;
            }
            }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

}
// namespace X_Y::Win32
