#pragma once

/*
GraphicsContext/GraphicsContext.h
version 0.0.0

平台图形上下文抽象。Widget 只持有基类，不 include 平台头；
具体 OpenGL / Win32 上下文在 src/Win32 里。

工厂的 nativeHandle 由平台窗口层给（Win32 下就是 HWND）。
*/

#include "GraphicsType.h"

namespace X_Y
{
    class GraphicsContext
    {
    public:
        virtual ~GraphicsContext() = default;

        virtual void Init() = 0;
        virtual bool MakeCurrent() = 0;
        virtual void SwapBuffers() = 0;
        virtual bool IsCurrent() const = 0;
        virtual void DoneCurrent() = 0;

        GraphicsType GetType() const { return m_Type; }

    protected:
        GraphicsType m_Type = GraphicsType::None;
    };

    // 平台工厂
    class GraphicsContextFactory
    {
    public:
        static GraphicsContext *Create(void *nativeHandle,
                                       GraphicsType type = GraphicsType::OpenGL);
    };
}
