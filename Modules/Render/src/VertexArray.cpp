#include"Render/VertexArray.h"
#include"Render/RenderAPI.h"
#include"Render/Render.h"
#include"Render/src/OpenGL/OpenGLVertexArray.h"
namespace X_Y {
	Ref<VertexArray> VertexArray::Create()
	{
		RApiType Api = Render::instance()->getCurrentAPI()->getType();
		switch (Api)
		{
		case RApiType::None:    XY_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RApiType::OpenGL:  return CreateRef<OpenGLVertexArray>();
		}

		XY_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}
