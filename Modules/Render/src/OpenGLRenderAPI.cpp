#include"Render/src/OpenGL/OpenGLRenderAPI.h"
#include"Core/Core.h"
namespace X_Y {

    void OpenGLMessageCallback(
        unsigned source,
        unsigned type,
        unsigned id,
        unsigned severity,
        int length,
        const char* message,
        const void* userParam)
    {
        switch (severity)
        {
        case GL_DEBUG_SEVERITY_HIGH:         XFATAL(message); return;
        case GL_DEBUG_SEVERITY_MEDIUM:       XERROR(message); return;
        case GL_DEBUG_SEVERITY_LOW:          XWARN(message); return;
        case GL_DEBUG_SEVERITY_NOTIFICATION: XTRACE(message); return;
        }

        XY_CORE_ASSERT(false, "Unknown severity level!");
    }
   

    // OpenGL 初始化
   void OpenGLRenderAPI::Init()
    {
        XY_PROFILE_FUNCTION();

    #ifdef XY_DEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(OpenGLMessageCallback, nullptr);

        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
    #endif

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LINE_SMOOTH);
    }

    // 设置视口
    void OpenGLRenderAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(x, y, width, height);
    }

    // 设置清屏颜色
    void OpenGLRenderAPI::SetClearColor(const RenderMath::Vec4& color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    // 设置线宽
    void OpenGLRenderAPI::SetLineWidth(float width)
    {
        glLineWidth(width);
    }

    // 释放资源
    void OpenGLRenderAPI::Cleanup()
    {

    }
    void OpenGLRenderAPI::Clear()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void OpenGLRenderAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount)
    {
        vertexArray->Bind();
        uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLRenderAPI::DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
    {
        vertexArray->Bind();
        glDrawArrays(GL_LINES, 0, vertexCount);
    }
    void OpenGLRenderAPI::DrawArrays(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
    {
        vertexArray->Bind();
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
    RApiType OpenGLRenderAPI::getType() {
        return RApiType::OpenGL;
    }


}
