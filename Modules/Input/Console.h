#pragma once

/*
Console.h
version 0.0.0

控制台按键：RawMode（RAII 关行缓冲/回显/信号）+ ReadKey（逐键读）。

窗口 / 全局键鼠仍然走 Input.h 的静态接口；这里只处理进程的 stdin 控制台。
键码和事件类型都复用 Input 层（Input::Key / Input::KeyEvent），不再另造一套。

────────────────────────────────────────────────────────────────
做

  RawMode       进作用域关掉行缓冲、回显、Ctrl+C 信号；析构自动还原。
  ReadKey()     返回 Key：
                  Character  Text 是 UTF-8 字符
                  Control    Text 是 1 字节控制码（Ctrl+C=0x03、Ctrl+D=0x04、Ctrl+L=0x0C）
                  Special    Code 是 Input::Key 的键码（方向键、Enter、Backspace、…）
                  Eof / Unknown
  KeyKind       普通字符 / 控制码 / 特殊键 / EOF / 未知

不做

  行编辑、历史、补全     -> 下一步，用 ReadKey 自己拼
  鼠标、窗口尺寸变化     -> 以后
  输入法 / 组合键         -> Windows 先按 UnicodeChar 收，复杂 IME 以后再说

平台

  Windows   ReadConsoleInputW，按 KEY_EVENT 读；关 ENABLE_LINE_INPUT /
            ENABLE_ECHO_INPUT / ENABLE_PROCESSED_INPUT，方向键给虚拟键码。
  Linux     termios 关 ICANON / ECHO / ISIG / IEXTEN；方向键是 ESC 序列，
            UTF-8 按字节解。

────────────────────────────────────────────────────────────────
注意

  RawMode 只该包住「读一行」这一段。执行外部命令前必须退出 RawMode，
  否则子进程会继承关掉行缓冲的控制台/终端。

  非 tty 时 RawMode::Ok() 为 false；ReadKey() 在管道上可能阻塞或返回 Eof，
  非交互程序不要调它。

────────────────────────────────────────────────────────────────
用法

    #include <Input/Console.h>
    using namespace X_Y;

    Console::RawMode raw;
    if (!raw.Ok())
        return 1;

    for (;;)
    {
        const auto key = Console::ReadKey();
        if (key.Type == Console::KeyKind::Eof)
            break;

        if (key.Type == Console::KeyKind::Character)
            Terminal::Write(key.Text);
        else if (key.Type == Console::KeyKind::Special &&
                 key.Code == Input::Key::Up)
            ...
    }
────────────────────────────────────────────────────────────────
*/

#include <Input/Input.h>

#include <memory>

namespace X_Y::Console
{
    // 事件类型和键码都在 Input 层：Console 只做别名。
    using KeyKind = Input::KeyKind;
    using Key     = Input::KeyEvent;

    // 进作用域进 raw，析构还原。不可拷贝。
    class RawMode
    {
    public:
        RawMode();
        ~RawMode();

        RawMode(const RawMode &)            = delete;
        RawMode &operator=(const RawMode &) = delete;

        bool Ok() const
        {
            return _ok;
        }

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
        bool                  _ok = false;
    };

    // 逐键读。失败 / EOF 返回 KeyKind::Eof。
    Key ReadKey();
}
