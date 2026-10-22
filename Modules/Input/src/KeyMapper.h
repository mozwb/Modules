#pragma once
#include "Input/Input.h"

namespace X_Y
{

    // 平台无关的键码映射接口
    class KeyMapper
    {
    public:
        virtual ~KeyMapper() = default;

        // 平台虚拟键 → 内部 KeyCode
        virtual uint32_t PlatformToKey(uint32_t platformKey) const = 0;
        // 内部 KeyCode → 平台虚拟键
        virtual uint32_t KeyToPlatform(uint32_t keyCode) const = 0;
        // 平台鼠标键 → 内部 MouseCode
        virtual uint32_t PlatformToMouse(uint32_t platformButton) const = 0;
        // 内部 MouseCode → 平台鼠标键
        virtual uint32_t MouseToPlatform(uint32_t mouseCode) const = 0;
        // 按键状态查询（走消息队列的 GetKeyState，供 UI/窗口事件使用）
        virtual bool IsKeyPressed(uint32_t keyCode) const = 0;
        // 鼠标状态查询（走消息队列的 GetKeyState）
        virtual bool IsMousePressed(uint32_t mouseCode) const = 0;
        // 按键状态查询（硬件直读 GetAsyncKeyState，无焦点依赖，供游戏/高频/全局输入）
        virtual bool IsKeyDown(uint32_t keyCode) const = 0;
        // 鼠标状态查询（硬件直读 GetAsyncKeyState）
        virtual bool IsMouseDown(uint32_t mouseCode) const = 0;
        // 鼠标位置（返回的是全局屏幕坐标，非窗口客户区坐标）
        // 需要窗口客户区坐标时，去 BaseWin 找 ScreenToClient / GetMouseScreenPos 等接口
        virtual void GetMousePos(float &x, float &y) const = 0;
        // 设置鼠标位置（全局屏幕坐标）；移动真实系统光标
        // 需要窗口客户区坐标时，去 BaseWin 找 ScreenToClient / GetMouseScreenPos 等接口
        virtual void SetMousePos(float x, float y) = 0;

        virtual uint32_t GetKeyPressed() const = 0;
        virtual uint32_t GetMouseButtonPressed() const = 0;
        virtual uint32_t GetKeyDown() const = 0;
        virtual uint32_t GetMouseDown() const = 0;

        virtual bool SimulateTypeText(const wchar_t *wstr, uint32_t charIntervalMs) = 0;
        virtual bool SimulateKey(uint32_t keyCode, bool pressed) = 0;
        virtual bool SimulateMouse(uint32_t mouseCode, bool pressed) = 0;
        virtual bool SimulateMouseWheel(float delta) = 0;
        virtual bool EatKey(uint32_t keyCode, Input::EatMode mode) = 0;
        virtual bool EatMouse(uint32_t mouseCode, Input::EatMode mode) = 0;
        virtual bool TryGetEatKey(uint32_t &keyCode, bool &pressed) = 0;
        virtual bool TryGetEatMouse(uint32_t &mouseCode, bool &pressed) = 0;
        virtual void ClearEatKeyQueue() = 0;
        virtual void ClearEatMouseQueue() = 0;
        virtual void ResetEatState() = 0;
        virtual void StopHooks() = 0;
    };

    // 平台工厂
    // 因为目前只有一个平台实现，所以就没有单独写工厂函数实现的文件，而是把它 直接放在了win32的实现文件里了
    class KeyMapperFactory
    {
    public:
        static KeyMapper *Create();
    };

}
// namespace X_Y
