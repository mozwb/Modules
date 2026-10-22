#include "../../../Input/KeyMapper.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <windows.h>

namespace X_Y
{
    using KeyCode = Input::KeyCode;
    using MouseCode = Input::MouseCode;
    using EatMode = Input::EatMode;
    using Key = Input::Key;
    using Mouse = Input::Mouse;

    namespace
    {
        std::array<std::atomic<Input::EatMode>, 256> s_keyModes{};
        std::array<std::atomic<Input::EatMode>, 3> s_mouseModes{};
        struct EatEvent
        {
            uint32_t code;
            bool pressed;
        };
        std::mutex s_eatQueueMutex;
        std::deque<EatEvent> s_keyQueue;
        std::deque<EatEvent> s_mouseQueue;
        HHOOK s_keyboardHook = nullptr;
        HHOOK s_mouseHook = nullptr;
        std::thread s_hookThread;
        DWORD s_hookThreadId = 0;
        std::mutex s_hookMutex;
        std::condition_variable s_hookReady;
        bool s_hookInitialized = false;

        LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION)
            {
                const auto *event = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
                const bool isKeyEvent = wParam == WM_KEYDOWN || wParam == WM_KEYUP ||
                                        wParam == WM_SYSKEYDOWN || wParam == WM_SYSKEYUP;
                if (isKeyEvent && event->vkCode < s_keyModes.size() &&
                    (event->flags & LLKHF_INJECTED) == 0)
                {
                    const auto mode = s_keyModes[event->vkCode].load();
                    if (mode == Input::EatMode::Pass || mode == Input::EatMode::Capture)
                    {
                        std::lock_guard lock(s_eatQueueMutex);
                        s_keyQueue.push_back({event->vkCode,
                                              wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN});
                        if (mode == Input::EatMode::Capture)
                            return 1;
                    }
                    if (mode == Input::EatMode::Block)
                        return 1;
                }
            }
            return ::CallNextHookEx(nullptr, code, wParam, lParam);
        }

        LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION)
            {
                const auto *event = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
                if ((event->flags & LLMHF_INJECTED) == 0)
                {
                    int button = -1;
                    if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP)
                        button = 0;
                    else if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP)
                        button = 1;
                    else if (wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP)
                        button = 2;
                    if (button >= 0)
                    {
                        const auto mode = s_mouseModes[button].load();
                        if (mode == Input::EatMode::Pass || mode == Input::EatMode::Capture)
                        {
                            std::lock_guard lock(s_eatQueueMutex);
                            const uint32_t virtualButton = button == 0 ? VK_LBUTTON : button == 1 ? VK_RBUTTON
                                                                                                  : VK_MBUTTON;
                            s_mouseQueue.push_back({virtualButton,
                                                    wParam == WM_LBUTTONDOWN ||
                                                        wParam == WM_RBUTTONDOWN ||
                                                        wParam == WM_MBUTTONDOWN});
                            if (mode == Input::EatMode::Capture)
                                return 1;
                        }
                        if (mode == Input::EatMode::Block)
                            return 1;
                    }
                }
            }
            return ::CallNextHookEx(nullptr, code, wParam, lParam);
        }

        void HookThreadMain()
        {
            s_hookThreadId = ::GetCurrentThreadId();
            MSG initialMessage{};
            ::PeekMessageW(&initialMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            HINSTANCE module = ::GetModuleHandleW(nullptr);
            s_keyboardHook = ::SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, module, 0);
            s_mouseHook = ::SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, module, 0);

            {
                std::lock_guard lock(s_hookMutex);
                s_hookInitialized = true;
            }
            s_hookReady.notify_all();

            MSG message;
            while (::GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }

            if (s_keyboardHook)
                ::UnhookWindowsHookEx(s_keyboardHook);
            if (s_mouseHook)
                ::UnhookWindowsHookEx(s_mouseHook);
        }

        void StopHookThread();

        bool EnsureHookThread()
        {
            std::unique_lock lock(s_hookMutex);
            static std::once_flag cleanupFlag;
            std::call_once(cleanupFlag, []
                           { std::atexit(StopHookThread); });
            if (!s_hookThread.joinable())
            {
                s_hookInitialized = false;
                s_hookThread = std::thread(HookThreadMain);
            }
            s_hookReady.wait(lock, []
                             { return s_hookInitialized; });
            return s_keyboardHook != nullptr || s_mouseHook != nullptr;
        }

        void StopHookThread()
        {
            std::thread thread;
            {
                std::lock_guard lock(s_hookMutex);
                if (!s_hookThread.joinable())
                    return;
                ::PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
                thread = std::move(s_hookThread);
            }
            thread.join();
            std::lock_guard lock(s_hookMutex);
            s_hookThreadId = 0;
            s_hookInitialized = false;
        }
    }

    // // ── 全局 KeyMapper 实例 ───────────────────────────
    // static KeyMapper *s_Mapper = nullptr;

    // // 懒初始化（在首次 Translate 调用时创建）
    // static KeyMapper *GetMapper()
    // {
    //     if (!s_Mapper)
    //     {
    //         s_Mapper = KeyMapperFactory::Create();
    //     }
    //     return s_Mapper;
    // }

    // namespace InputMapping
    // {

    //     Input::KeyCode Translate(uint32_t platformKey)
    //     {
    //         auto *mapper = GetMapper();
    //         return static_cast<Input::KeyCode>(mapper->PlatformToKey(platformKey));
    //     }

    //     uint32_t TranslateKey(Input::KeyCode key)
    //     {
    //         auto *mapper = GetMapper();
    //         return mapper->KeyToPlatform(static_cast<uint32_t>(key));
    //     }

    //     Input::MouseCode TranslateMouse(uint32_t platformButton)
    //     {
    //         auto *mapper = GetMapper();
    //         return static_cast<Input::MouseCode>(mapper->PlatformToMouse(platformButton));
    //     }

    //     uint32_t TranslateMouseKey(Input::MouseCode button)
    //     {
    //         auto *mapper = GetMapper();
    //         return mapper->MouseToPlatform(static_cast<uint32_t>(button));
    //     }

    // } // namespace InputMapping

    // ── Win32 平台的 KeyMapper 实现 ─────────────────────

    class KeyMapperWin32 : public KeyMapper
    {
    public:
        uint32_t PlatformToKey(uint32_t platformKey) const override
        {
            // 字母 A-Z
            if (platformKey >= 'A' && platformKey <= 'Z')
                return platformKey;
            // 数字 0-9
            if (platformKey >= '0' && platformKey <= '9')
                return platformKey;

#define MAP(PLATFORM, INNER)     \
    if (platformKey == PLATFORM) \
        return INNER;

            MAP(VK_SPACE, Key::Space);
            MAP(VK_OEM_4, Key::LeftBracket);
            MAP(VK_OEM_6, Key::RightBracket);
            MAP(VK_OEM_1, Key::Semicolon);
            MAP(VK_OEM_7, Key::Apostrophe);
            MAP(VK_OEM_COMMA, Key::Comma);
            MAP(VK_OEM_PERIOD, Key::Period);
            MAP(VK_OEM_2, Key::Slash);
            MAP(VK_OEM_MINUS, Key::Minus);
            MAP(VK_OEM_PLUS, Key::Equal);
            MAP(VK_OEM_5, Key::Backslash);
            MAP(VK_OEM_3, Key::GraveAccent);
            MAP(VK_ESCAPE, Key::Escape);
            MAP(VK_TAB, Key::Tab);
            MAP(VK_RETURN, Key::Enter);
            MAP(VK_BACK, Key::Backspace);
            MAP(VK_INSERT, Key::Insert);
            MAP(VK_DELETE, Key::Delete);
            MAP(VK_HOME, Key::Home);
            MAP(VK_END, Key::End);
            MAP(VK_PRIOR, Key::PageUp);
            MAP(VK_NEXT, Key::PageDown);
            MAP(VK_UP, Key::Up);
            MAP(VK_DOWN, Key::Down);
            MAP(VK_LEFT, Key::Left);
            MAP(VK_RIGHT, Key::Right);
            MAP(VK_CAPITAL, Key::CapsLock);
            MAP(VK_SCROLL, Key::ScrollLock);
            MAP(VK_NUMLOCK, Key::NumLock);
            MAP(VK_LSHIFT, Key::LeftShift);
            MAP(VK_RSHIFT, Key::RightShift);
            MAP(VK_LCONTROL, Key::LeftControl);
            MAP(VK_RCONTROL, Key::RightControl);
            MAP(VK_LMENU, Key::LeftAlt);
            MAP(VK_RMENU, Key::RightAlt);
#undef MAP

            // F1-F24
            if (platformKey >= VK_F1 && platformKey <= VK_F24)
                return Key::F1 + (platformKey - VK_F1);

            return 0;
        }

        uint32_t KeyToPlatform(uint32_t keyCode) const override
        {
            if (keyCode >= 'A' && keyCode <= 'Z')
                return keyCode;
            if (keyCode >= '0' && keyCode <= '9')
                return keyCode;

#define MAP(INNER, PLATFORM) \
    if (keyCode == INNER)    \
        return PLATFORM;

            MAP(Key::Space, VK_SPACE);
            MAP(Key::LeftBracket, VK_OEM_4);
            MAP(Key::RightBracket, VK_OEM_6);
            MAP(Key::Semicolon, VK_OEM_1);
            MAP(Key::Apostrophe, VK_OEM_7);
            MAP(Key::Comma, VK_OEM_COMMA);
            MAP(Key::Period, VK_OEM_PERIOD);
            MAP(Key::Slash, VK_OEM_2);
            MAP(Key::Minus, VK_OEM_MINUS);
            MAP(Key::Equal, VK_OEM_PLUS);
            MAP(Key::Backslash, VK_OEM_5);
            MAP(Key::GraveAccent, VK_OEM_3);
            MAP(Key::Escape, VK_ESCAPE);
            MAP(Key::Tab, VK_TAB);
            MAP(Key::Enter, VK_RETURN);
            MAP(Key::Backspace, VK_BACK);
            MAP(Key::Insert, VK_INSERT);
            MAP(Key::Delete, VK_DELETE);
            MAP(Key::Home, VK_HOME);
            MAP(Key::End, VK_END);
            MAP(Key::PageUp, VK_PRIOR);
            MAP(Key::PageDown, VK_NEXT);
            MAP(Key::Up, VK_UP);
            MAP(Key::Down, VK_DOWN);
            MAP(Key::Left, VK_LEFT);
            MAP(Key::Right, VK_RIGHT);
            MAP(Key::CapsLock, VK_CAPITAL);
            MAP(Key::ScrollLock, VK_SCROLL);
            MAP(Key::NumLock, VK_NUMLOCK);
            MAP(Key::LeftShift, VK_LSHIFT);
            MAP(Key::RightShift, VK_RSHIFT);
            MAP(Key::LeftControl, VK_LCONTROL);
            MAP(Key::RightControl, VK_RCONTROL);
            MAP(Key::LeftAlt, VK_LMENU);
            MAP(Key::RightAlt, VK_RMENU);
#undef MAP

            if (keyCode >= Key::F1 && keyCode <= Key::F24)
                return VK_F1 + (keyCode - Key::F1);

            return 0;
        }

        uint32_t PlatformToMouse(uint32_t platformButton) const override
        {
            if (platformButton == VK_LBUTTON)
                return Mouse::ButtonLeft;
            if (platformButton == VK_RBUTTON)
                return Mouse::ButtonRight;
            if (platformButton == VK_MBUTTON)
                return Mouse::ButtonMiddle;
            return 0;
        }

        uint32_t MouseToPlatform(uint32_t mouseCode) const override
        {
            if (mouseCode == Mouse::ButtonLeft)
                return VK_LBUTTON;
            if (mouseCode == Mouse::ButtonRight)
                return VK_RBUTTON;
            if (mouseCode == Mouse::ButtonMiddle)
                return VK_MBUTTON;
            return 0;
        }

        bool IsKeyPressed(uint32_t keyCode) const override
        {
            uint32_t vk = KeyToPlatform(keyCode);
            if (vk == 0)
                return false;
            return (::GetKeyState((int)vk) & 0x8000) != 0;
        }

        bool IsMousePressed(uint32_t mouseCode) const override
        {
            uint32_t vk = MouseToPlatform(mouseCode);
            if (vk == 0)
                return false;
            return (::GetKeyState((int)vk) & 0x8000) != 0;
        }

        // 硬件直读轮询(GetAsyncKeyState，无焦点依赖)——游戏/高频/全局输入用
        bool IsKeyDown(uint32_t keyCode) const override
        {
            uint32_t vk = KeyToPlatform(keyCode);
            if (vk == 0)
                return false;
            return (::GetAsyncKeyState((int)vk) & 0x8000) != 0;
        }

        bool IsMouseDown(uint32_t mouseCode) const override
        {
            uint32_t vk = MouseToPlatform(mouseCode);
            if (vk == 0)
                return false;
            return (::GetAsyncKeyState((int)vk) & 0x8000) != 0;
        }

        uint32_t GetKeyPressed() const override
        {
            for (uint32_t vk = 0; vk <= 0xFF; ++vk)
            {
                if (::GetKeyState((int)vk) & 0x8000)
                {
                    return PlatformToKey(vk);
                }
            }
            return 0;
        }

        uint32_t GetMouseButtonPressed() const override
        {
            for (uint32_t vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON})
            {
                if (::GetKeyState((int)vk) & 0x8000)
                {
                    return PlatformToMouse(vk);
                }
            }
            return 0;
        }
        uint32_t GetKeyDown() const override
        {
            for (uint32_t vk = 0; vk <= 0xFF; ++vk)
            {
                if (::GetAsyncKeyState((int)vk) & 0x8000)
                {
                    return PlatformToKey(vk);
                }
            }
            return 0;
        }
        uint32_t GetMouseDown() const override
        {
            for (uint32_t vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON})
            {
                if (::GetAsyncKeyState((int)vk) & 0x8000)
                {
                    return PlatformToMouse(vk);
                }
            }
            return 0;
        }

        void GetMousePos(float &x, float &y) const override
        {
            POINT pt;
            ::GetCursorPos(&pt);
            x = (float)pt.x;
            y = (float)pt.y;
        }

        void SetMousePos(float x, float y) override
        {
            // 移动真实系统光标到全局屏幕坐标
            ::SetCursorPos((int)x, (int)y);
        }

        bool SimulateTypeText(const wchar_t *wstr, uint32_t charIntervalMs) override
        {
            if (!wstr)
                return false;
            while (*wstr)
            {
                wchar_t ch = *wstr++;
                INPUT inp[2]{};
                if (ch == L'\r' || ch == L'\n')
                {
                    if (ch == L'\r' && *wstr == L'\n')
                        ++wstr;
                    inp[0].type = INPUT_KEYBOARD;
                    inp[0].ki.wVk = VK_RETURN;
                    inp[0].ki.dwFlags = 0;
                    inp[1].type = INPUT_KEYBOARD;
                    inp[1].ki.wVk = VK_RETURN;
                    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
                }
                else
                {
                    inp[0].type = INPUT_KEYBOARD;
                    inp[0].ki.wVk = 0;
                    inp[0].ki.wScan = ch;
                    inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
                    inp[1].type = INPUT_KEYBOARD;
                    inp[1].ki.wVk = 0;
                    inp[1].ki.wScan = ch;
                    inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                }
                if (::SendInput(2, inp, sizeof(INPUT)) != 2)
                    return false;
                if (charIntervalMs > 0 && *wstr != L'\0')
                    ::Sleep(charIntervalMs);
            }
            return true;
        }
        bool SimulateKey(uint32_t keyCode, bool pressed) override
        {
            const uint32_t vk = KeyToPlatform(keyCode);
            if (vk == 0)
                return false;
            INPUT inp{};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = (WORD)vk;
            inp.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
            return ::SendInput(1, &inp, sizeof(INPUT)) == 1;
        }
        bool SimulateMouse(uint32_t mouseCode, bool pressed) override
        {
            uint32_t vk = MouseToPlatform(mouseCode);
            INPUT inp{};
            inp.type = INPUT_MOUSE;
            if (vk == VK_LBUTTON)
            {
                inp.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            }
            else if (vk == VK_RBUTTON)
            {
                inp.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            }
            else if (vk == VK_MBUTTON)
            {
                inp.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            }
            else
            {
                return false; // 未知鼠标键
            }
            return ::SendInput(1, &inp, sizeof(INPUT)) == 1;
        }
        bool SimulateMouseWheel(float delta) override
        {
            INPUT inp{};
            inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
            inp.mi.mouseData = (DWORD)(delta * WHEEL_DELTA); // Windows标准滚轮单位
            return ::SendInput(1, &inp, sizeof(INPUT)) == 1;
        }

        bool EatKey(uint32_t keyCode, Input::EatMode mode) override
        {
            const uint32_t vk = KeyToPlatform(keyCode);
            if (vk == 0 || vk >= s_keyModes.size())
                return false;
            if (mode != Input::EatMode::Pass && !EnsureHookThread())
                return false;
            s_keyModes[vk].store(mode);
            return true;
        }

        bool EatMouse(uint32_t mouseCode, Input::EatMode mode) override
        {
            if (mouseCode > Input::Mouse::ButtonMiddle)
                return false;
            if (mode != Input::EatMode::Pass && !EnsureHookThread())
                return false;
            s_mouseModes[mouseCode].store(mode);
            return true;
        }

        bool TryGetEatKey(uint32_t &keyCode, bool &pressed) override
        {
            std::lock_guard lock(s_eatQueueMutex);
            if (s_keyQueue.empty())
                return false;
            keyCode = s_keyQueue.front().code;
            pressed = s_keyQueue.front().pressed;
            s_keyQueue.pop_front();
            return true;
        }

        bool TryGetEatMouse(uint32_t &mouseCode, bool &pressed) override
        {
            std::lock_guard lock(s_eatQueueMutex);
            if (s_mouseQueue.empty())
                return false;
            mouseCode = s_mouseQueue.front().code;
            pressed = s_mouseQueue.front().pressed;
            s_mouseQueue.pop_front();
            return true;
        }

        void ClearEatKeyQueue() override
        {
            std::lock_guard lock(s_eatQueueMutex);
            s_keyQueue.clear();
        }

        void ClearEatMouseQueue() override
        {
            std::lock_guard lock(s_eatQueueMutex);
            s_mouseQueue.clear();
        }

        void ResetEatState() override
        {
            for (auto &mode : s_keyModes)
                mode.store(Input::EatMode::Pass);
            for (auto &mode : s_mouseModes)
                mode.store(Input::EatMode::Pass);

            std::lock_guard lock(s_eatQueueMutex);
            s_keyQueue.clear();
            s_mouseQueue.clear();
        }

        void StopHooks() override
        {
            StopHookThread();
        }
    };

    // ── 工厂实现 ──────────────────────────────────────
    KeyMapper *KeyMapperFactory::Create()
    {
        return new KeyMapperWin32();
    }

} // namespace X_Y
