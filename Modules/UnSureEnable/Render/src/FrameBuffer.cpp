#include"Render/Render.h"
#include"Render/FrameBuffer.h"
#include"Render/src/OpenGL/OpenGLFrameBuffer.h"
namespace X_Y {

	Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
	{
		RApiType Api = Render::instance()->getCurrentAPI()->getType();
		switch (Api)
		{
		case RApiType::None:    XY_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RApiType::OpenGL:  return CreateRef<OpenGLFramebuffer>(spec);
		}

		XY_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}

