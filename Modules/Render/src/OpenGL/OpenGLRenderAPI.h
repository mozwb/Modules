#pragma once
#include "Render/RenderAPI.h"
#include <glad/glad.h>

// OpenGL 渲染API实现，继承自抽象接口 RenderAPI
namespace X_Y {
    class OpenGLRenderAPI : public RenderAPI
    {
    public:
        // 重写纯虚函数
        void Init() override;
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetClearColor(const RenderMath::Vec4& color) override;
        void SetLineWidth(float width) override;

        void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
        void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
        void DrawArrays(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
        void Cleanup() override;
        void Clear() override;
        RApiType getType() override;
    };
}
