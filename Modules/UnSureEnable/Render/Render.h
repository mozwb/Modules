#pragma once
#include"Core/Core.h"
#include<unordered_map>
#include"Render/RenderAPI.h"
#include"Render/renderEvent/renderEvent.h"
#include"Render/VertexArray.h"
#include"Render/Shader.h"
#include"Render/Camera.h"
#include"Middle/MeshData.h"
namespace X_Y {

    struct OwnedMesh {
        Ref<VertexArray> VAO;
        uint32_t IndexCount = 0;
    };

    class Render
    {
    public:
        Render() = default;
        ~Render() = default;

        static Render* instance()
        {
            static Render* inst = nullptr;
            if (!inst)
                inst = new Render();
            return inst;
        }

        void submitEvent(XMovement* e);
        RenderAPI* getCurrentAPI() { return m_LastSubmittedAPI; }

        // ---- RenderCommand 合并过来的 ----
        static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            instance()->getCurrentAPI()->SetViewport(x, y, width, height);
        }
        static void SetClearColor(const RenderMath::Vec4& color)
        {
            instance()->getCurrentAPI()->SetClearColor(color);
        }
        static void Clear()
        {
            instance()->getCurrentAPI()->Clear();
        }
        static void DrawIndexed(const Ref<VertexArray>& va, uint32_t count = 0)
        {
            instance()->getCurrentAPI()->DrawIndexed(va, count);
        }
        static void DrawLines(const Ref<VertexArray>& va, uint32_t count)
        {
            instance()->getCurrentAPI()->DrawLines(va, count);
        }
        static void DrawArrays(const Ref<VertexArray>& va, uint32_t count)
        {
            instance()->getCurrentAPI()->DrawArrays(va, count);
        }

        // ---- 渲染管线 ----
        static void Shutdown();
        static void OnWindowResize(uint32_t width, uint32_t height);
        static void BeginScene(const Camera& camera, const RenderMath::Mat4& view);
        static void EndScene();

        static int CreateMesh(const MeshData& meshData);
        static void DestroyMesh(int handle);

        static void Submit(const Ref<Shader>& shader, int meshHandle, const RenderMath::Mat4& transform = RenderMath::Mat4(1.0f));
        static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const RenderMath::Mat4& transform = RenderMath::Mat4(1.0f));

        Render(const Render&) = delete;
        Render& operator=(const Render&) = delete;

    private:
        struct SceneData {
            RenderMath::Mat4 ViewProjectionMatrix;
        };
        static Scope<SceneData> s_SceneData;
        static std::unordered_map<int, OwnedMesh> s_Meshes;
        static int s_NextHandle;

        RenderAPI* m_LastSubmittedAPI = nullptr;
        std::unordered_map<GraphicsType, Scope<RenderAPI>> m_RenderAPIs;
    };

}
