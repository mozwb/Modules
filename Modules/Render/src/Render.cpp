#include "Render/Render.h"
#include "Render/Buffer.h"

namespace X_Y {
    Scope<Render::SceneData> Render::s_SceneData = CreateScope<Render::SceneData>();
    std::unordered_map<int, OwnedMesh> Render::s_Meshes;
    int Render::s_NextHandle = 1;

    void Render::submitEvent(XMovement* e) {
        if (!e) return;
        XDEBUG("submit执行")

            RenderStart* p = dynamic_cast<RenderStart*>(e);
        if (!p)XFATAL("事件转化失败")

            GraphicsContext* GCT = p->m_GraphicsContext;
        if (!GCT)XFATAL("上下文是空指针")

            GraphicsType apiType = GCT->GetType();
        XDEBUG("当前API类型:{} ", apiType)

        auto it = m_RenderAPIs.find(apiType);
        if (it == m_RenderAPIs.end()) {
            XDEBUG("创建新 RenderAPI")
                auto api = RenderAPI::Create(apiType);
            api->Init();
            m_RenderAPIs[apiType] = std::move(api);
            it = m_RenderAPIs.find(apiType);
        }

        RenderAPI* currentAPI = it->second.get();
        m_LastSubmittedAPI = currentAPI;

        if (!GCT->IsCurrent()) {
            XDEBUG("切换窗口上下文")
                GCT->MakeCurrent();
        }
    }

    void Render::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const RenderMath::Mat4& transform)
    {
        shader->Bind();
        shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        shader->SetMat4("u_Transform", transform);

        vertexArray->Bind();
        Render::DrawIndexed(vertexArray);
    }

    void Render::Submit(const Ref<Shader>& shader, int meshHandle, const RenderMath::Mat4& transform)
    {
        auto it = s_Meshes.find(meshHandle);
        if (it == s_Meshes.end())
        {
            XERROR("Render::Submit: invalid mesh handle {}", meshHandle);
            return;
        }

        const auto& mesh = it->second;
        shader->Bind();
        shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
        shader->SetMat4("u_Transform", transform);

        mesh.VAO->Bind();
        Render::DrawIndexed(mesh.VAO, mesh.IndexCount);
    }

    void Render::BeginScene(const Camera& camera, const RenderMath::Mat4& view)
    {
        s_SceneData->ViewProjectionMatrix = camera.GetProjection() * view;
    }
    void Render::EndScene()
    {
    }
    void Render::OnWindowResize(uint32_t width, uint32_t height)
    {
        Render::SetViewport(0, 0, width, height);
    }

    namespace {
        ShaderDataType SizeToShaderDataType(uint32_t byteSize)
        {
            switch (byteSize)
            {
            case 4:  return ShaderDataType::Float;
            case 8:  return ShaderDataType::Float2;
            case 12: return ShaderDataType::Float3;
            case 16: return ShaderDataType::Float4;
            default:
                XWARN("Unknown attrib size {}, defaulting to Float3", byteSize);
                return ShaderDataType::Float3;
            }
        }
    }

    int Render::CreateMesh(const MeshData& meshData)
    {
        if (!meshData.IsValid())
        {
            XERROR("Render::CreateMesh: invalid MeshData");
            return -1;
        }

        auto va = VertexArray::Create();

        auto vb = VertexBuffer::Create(
            const_cast<float*>(meshData.Vertices.data()),
            static_cast<uint32_t>(meshData.Vertices.size() * sizeof(float)));

        // 构建 BufferLayout——从 Attribs 列表生成 initializer_list
        // 由于 Attribs 可能是运行时动态的，手动构造 BufferElement 数组
        switch (meshData.Attribs.size())
        {
        case 0:
            XERROR("Render::CreateMesh: no vertex attributes defined");
            return -1;
        case 1:
            vb->SetLayout({ {
                SizeToShaderDataType(meshData.Attribs[0].Size),
                meshData.Attribs[0].Name
            } });
            break;
        case 2:
            vb->SetLayout({ {
                SizeToShaderDataType(meshData.Attribs[0].Size),
                meshData.Attribs[0].Name
            }, {
                SizeToShaderDataType(meshData.Attribs[1].Size),
                meshData.Attribs[1].Name
            } });
            break;
        case 3:
            vb->SetLayout({ {
                SizeToShaderDataType(meshData.Attribs[0].Size),
                meshData.Attribs[0].Name
            }, {
                SizeToShaderDataType(meshData.Attribs[1].Size),
                meshData.Attribs[1].Name
            }, {
                SizeToShaderDataType(meshData.Attribs[2].Size),
                meshData.Attribs[2].Name
            } });
            break;
        default:
            XERROR("Render::CreateMesh: too many attributes ({})", meshData.Attribs.size());
            return -1;
        }

        va->AddVertexBuffer(vb);

        auto ib = IndexBuffer::Create(
            const_cast<uint32_t*>(meshData.Indices.data()),
            static_cast<uint32_t>(meshData.Indices.size()));
        va->SetIndexBuffer(ib);

        int handle = s_NextHandle++;
        s_Meshes[handle] = { va, static_cast<uint32_t>(meshData.Indices.size()) };
        XDEBUG("Render::CreateMesh: handle={}, {} unique verts, {} indices",
            handle,
            meshData.Vertices.size() / meshData.VertexStride,
            meshData.Indices.size());
        return handle;
    }

    void Render::DestroyMesh(int handle)
    {
        auto it = s_Meshes.find(handle);
        if (it != s_Meshes.end())
        {
            XDEBUG("Render::DestroyMesh: handle={}", handle);
            s_Meshes.erase(it);
        }
        else
        {
            XWARN("Render::DestroyMesh: handle {} not found", handle);
        }
    }
}
