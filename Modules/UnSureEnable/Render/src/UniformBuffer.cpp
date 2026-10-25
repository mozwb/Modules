#include<Render/UniformBuffer.h>
#include<Render/src/OpenGL/OpenGLUniformBuffer.h>
#include<Render/Render.h>
namespace X_Y {
	Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
	{
		RApiType Api = Render::instance()->getCurrentAPI()->getType();
		switch (Api)
		{
		case RApiType::None:    XY_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RApiType::OpenGL:  return CreateRef<OpenGLUniformBuffer>(size, binding);
		}

		XY_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
