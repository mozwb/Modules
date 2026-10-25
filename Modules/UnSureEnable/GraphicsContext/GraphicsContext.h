#pragma once
#include "GraphicsType.h"

namespace X_Y {

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
class GraphicsContextFactory {
public:
    static GraphicsContext* Create(void* nativeHandle, GraphicsType type = GraphicsType::OpenGL);
};

}
