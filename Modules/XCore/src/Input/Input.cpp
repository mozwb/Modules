// #include "../../Input/Input.h"
#include "../../Input/KeyMapper.h"

#ifdef XY_PLATFORM_WINDOWS
// OEMRESOURCE 让 winuser.h 暴露 OCR_* 系统光标 id（SetSystemCursor 用）
#define OEMRESOURCE
#include <windows.h>
#endif
#include "XCore/Decoder/Text.h"

namespace X_Y
{
    // extern KeyMapper *GetMapper();
    // 全局 mapper 实例
    static KeyMapper *s_Mapper = nullptr;

    static KeyMapper *GetMapper()
    {
        if (!s_Mapper)
        {
            s_Mapper = KeyMapperFactory::Create();
        }
        return s_Mapper;
    }

        using KeyCode = Input::KeyCode;
        using MouseCode = Input::MouseCode;
        using EatMode = Input::EatMode;
        using MousePosition = Input::MousePosition;

        bool Input::IsKeyPressed(KeyCode key)
        {
            auto *mapper = GetMapper();
            return mapper->IsKeyPressed(static_cast<uint32_t>(key));
        }

        bool Input::IsMouseButtonPressed(MouseCode button)
        {
            auto *mapper = GetMapper();
            return mapper->IsMousePressed(static_cast<uint32_t>(button));
        }
        KeyCode Input::GetKeyPressed()
        {
            auto *mapper = GetMapper();
            return static_cast<KeyCode>(mapper->GetKeyPressed());
        }
        MouseCode Input::GetMouseButtonPressed()
        {
            auto *mapper = GetMapper();
            return static_cast<MouseCode>(mapper->GetMouseButtonPressed());
        }

        MousePosition Input::GetMousePosition()
        {
            float x, y;
            GetMapper()->GetMousePos(x, y);
            return {x, y};
        }

        float Input::GetMouseX()
        {
            return GetMousePosition().x;
        }

        float Input::GetMouseY()
        {
            return GetMousePosition().y;
        }

        void Input::SetMousePosition(float x, float y)
        {
            GetMapper()->SetMousePos(x, y);
        }

        // ── 设备层(全局)能力 ──────────────────────

        void Input::SetCursorVisible(bool visible)
        {
#ifdef XY_PLATFORM_WINDOWS
            if (visible)
            {
                // 恢复系统默认光标
                ::SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
            }
            else
            {
                // 全局彻底隐藏：用 1x1 全透明光标替换系统光标（SetSystemCursor）。
                // 注意：ShowCursor 只对“本线程激活窗口”生效，无焦点/全屏透明层无效，
                //       故用 SetSystemCursor 全局替换，任何窗口都看不到光标。
                static HCURSOR s_hidden = nullptr;
                if (!s_hidden)
                {
                    // 1x1 透明位图掩码：AND 全 1 + XOR 全 0 = 全透明
                    static const BYTE andMask[1] = {0xFF};
                    static const BYTE xorMask[1] = {0x00};
                    s_hidden = ::CreateCursor(::GetModuleHandleW(nullptr),
                                              0, 0, 1, 1, andMask, xorMask);
                }
                if (s_hidden)
                {
                    // 替换主要系统光标（SetSystemCursor 会销毁传入句柄，故 CopyIcon）
                    static const int ids[] = {
                        OCR_NORMAL,
                        OCR_IBEAM,
                        OCR_WAIT,
                        OCR_CROSS,
                        OCR_SIZEALL,
                        OCR_SIZENWSE,
                        OCR_SIZENESW,
                        OCR_SIZEWE,
                        OCR_SIZENS,
                        OCR_NO,
                        OCR_HAND,
                        OCR_APPSTARTING,
                    };
                    for (int id : ids)
                        ::SetSystemCursor(::CopyIcon(s_hidden), id);
                }
            }
#endif
        }

        int Input::GetScreenWidth()
        {
#ifdef XY_PLATFORM_WINDOWS
            return ::GetSystemMetrics(SM_CXSCREEN);
#else
            return 0;
#endif
        }

        int Input::GetScreenHeight()
        {
#ifdef XY_PLATFORM_WINDOWS
            return ::GetSystemMetrics(SM_CYSCREEN);
#else
            return 0;
#endif
        }

        // ── 设备层：硬件直读轮询(转发到 KeyMapper 的 IsKeyDown/IsMouseDown) ──

        bool Input::IsKeyDown(KeyCode key)
        {
            auto *mapper = GetMapper();
            return mapper->IsKeyDown(static_cast<uint32_t>(key));
        }

        bool Input::IsMouseDown(MouseCode button)
        {
            auto *mapper = GetMapper();
            return mapper->IsMouseDown(static_cast<uint32_t>(button));
        }

        KeyCode Input::GetKeyDown()
        {
            auto *mapper = GetMapper();
            return static_cast<KeyCode>(mapper->GetKeyDown());
        }
        MouseCode Input::GetMouseDown()
        {
            auto *mapper = GetMapper();
            return static_cast<MouseCode>(mapper->GetMouseDown());
        }

        Input::KeyCode Input::Translate(uint32_t platformKey)
        {
            auto *mapper = GetMapper();
            return static_cast<Input::KeyCode>(mapper->PlatformToKey(platformKey));
        }

        uint32_t Input::TranslateKey(Input::KeyCode key)
        {
            auto *mapper = GetMapper();
            return mapper->KeyToPlatform(static_cast<uint32_t>(key));
        }

        Input::MouseCode Input::TranslateMouse(uint32_t platformButton)
        {
            auto *mapper = GetMapper();
            return static_cast<Input::MouseCode>(mapper->PlatformToMouse(platformButton));
        }

        uint32_t Input::TranslateMouseKey(Input::MouseCode button)
        {
            auto *mapper = GetMapper();
            return mapper->MouseToPlatform(static_cast<uint32_t>(button));
        }

        bool Input::SimulateTypeText(const wchar_t *wstr, uint32_t charIntervalMs)
        {
            auto *mapper = GetMapper();
            return mapper->SimulateTypeText(wstr, charIntervalMs);
        }

        bool Input::SimulateKey(KeyCode key, bool pressed)
        {
            auto *mapper = GetMapper();
            return mapper->SimulateKey(static_cast<uint32_t>(key), pressed);
        }
        bool Input::SimulateMouse(MouseCode button, bool pressed)
        {
            auto *mapper = GetMapper();
            return mapper->SimulateMouse(static_cast<uint32_t>(button), pressed);
        }

        bool Input::SimulateMouseWheel(float delta)
        {
            auto *mapper = GetMapper();
            return mapper->SimulateMouseWheel(delta);
        }

        bool Input::EatKey(KeyCode key, EatMode mode)
        {
            return GetMapper()->EatKey(static_cast<uint32_t>(key), mode);
        }

        bool Input::EatMouse(MouseCode button, EatMode mode)
        {
            return GetMapper()->EatMouse(static_cast<uint32_t>(button), mode);
        }

        bool Input::EatKey(KeyCode key, bool enabled)
        {
            return EatKey(key, enabled ? EatMode::Block : EatMode::Pass);
        }

        bool Input::EatMouse(MouseCode button, bool enabled)
        {
            return EatMouse(button, enabled ? EatMode::Block : EatMode::Pass);
        }

        bool Input::TryGetEatKey(KeyCode &key, bool &pressed)
        {
            uint32_t platformKey = 0;
            if (!GetMapper()->TryGetEatKey(platformKey, pressed))
                return false;
            key = static_cast<KeyCode>(GetMapper()->PlatformToKey(platformKey));
            return key != 0;
        }

        bool Input::TryGetEatMouse(MouseCode &button, bool &pressed)
        {
            uint32_t platformButton = 0;
            if (!GetMapper()->TryGetEatMouse(platformButton, pressed))
                return false;
            button = static_cast<MouseCode>(GetMapper()->PlatformToMouse(platformButton));
            return true;
        }

        void Input::ClearEatKeyQueue()
        {
            GetMapper()->ClearEatKeyQueue();
        }

        void Input::ClearEatMouseQueue()
        {
            GetMapper()->ClearEatMouseQueue();
        }

        void Input::ResetEatState()
        {
            GetMapper()->ResetEatState();
        }

        void Input::StopHooks()
        {
            GetMapper()->StopHooks();
        }

        bool Input::SimulateTypeText(const char *utf8Str, uint32_t charIntervalMs)
        {
            if (!utf8Str)
                return false;
            auto wopt = Decoder::Decode(
                Decode::UTF8Text{utf8Str},
                Decode::ToUTF16{});
            if (!wopt)
                return false;
            return SimulateTypeText(wopt->c_str(), charIntervalMs);
        }
        bool Input::SimulateTypeText(const std::string &utf8Str, uint32_t charIntervalMs)
        {
            return SimulateTypeText(utf8Str.c_str(), charIntervalMs);
        }
} // namespace X_Y
