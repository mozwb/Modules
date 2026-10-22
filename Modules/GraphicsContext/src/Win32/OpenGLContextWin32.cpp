#include "GraphicsContext/GraphicsContext.h"
#include "Log/XYLog.h"
#include "glad/glad.h"
#include <windows.h>

namespace X_Y {

class OpenGLContextWin32 : public GraphicsContext {
public:
    OpenGLContextWin32(HWND hwnd)
        : m_hWnd(hwnd), m_hDC(nullptr), m_hGLRC(nullptr)
    {
        m_Type = GraphicsType::OpenGL;
        m_hDC = ::GetDC(m_hWnd);
        if (!m_hDC) {
            XFATAL("GetDC 失败")
            return;
        }
        SetupPixelFormat();
    }

    ~OpenGLContextWin32() override {
        if (m_hGLRC) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_hGLRC);
            m_hGLRC = nullptr;
        }
        if (m_hWnd && m_hDC) {
            ::ReleaseDC(m_hWnd, m_hDC);
            m_hDC = nullptr;
        }
    }

    void Init() override {
        if (!m_hDC) {
            XFATAL("Init 时 m_hDC 为空")
            return;
        }

        m_hGLRC = wglCreateContext(m_hDC);
        if (!m_hGLRC) {
            XFATAL("wglCreateContext 失败")
            return;
        }

        if (!MakeCurrent()) {
            XFATAL("MakeCurrent 失败")
            return;
        }

        XINFO("OpenGL 上下文初始化成功")

        if (!gladLoadGL()) {

            XFATAL("glad 初始化失败")
            return;
        }
    }

    bool MakeCurrent() override {
        if (!m_hDC || !m_hGLRC) return false;
        return wglMakeCurrent(m_hDC, m_hGLRC) != FALSE;
    }

    void SwapBuffers() override {
        if (m_hDC) ::SwapBuffers(m_hDC);
    }

    bool IsCurrent() const override {
        return wglGetCurrentContext() == m_hGLRC;
    }

    void DoneCurrent() override {
        wglMakeCurrent(nullptr, nullptr);
    }

    OpenGLContextWin32(const OpenGLContextWin32&) = delete;
    OpenGLContextWin32& operator=(const OpenGLContextWin32&) = delete;

private:
    void SetupPixelFormat() {
        PIXELFORMATDESCRIPTOR pfd = {
            sizeof(PIXELFORMATDESCRIPTOR), 1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA, 32,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0
        };
        int fmt = ChoosePixelFormat(m_hDC, &pfd);
        if (!fmt) {
            XFATAL("ChoosePixelFormat 失败")
            return;
        }
        if (!SetPixelFormat(m_hDC, fmt, &pfd)) {
            XFATAL("SetPixelFormat 失败")
            return;
        }
    }

    HWND m_hWnd = nullptr;
    HDC m_hDC = nullptr;
    HGLRC m_hGLRC = nullptr;
};

GraphicsContext* GraphicsContextFactory::Create(void* nativeHandle, GraphicsType type) {
    switch (type) {
        case GraphicsType::OpenGL:
            return new OpenGLContextWin32((HWND)nativeHandle);
        default:
            return nullptr;
    }
}

}
