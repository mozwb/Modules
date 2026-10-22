#include"Render/Texture.h"
#include"Render/Render.h"
#include<Render/src/OpenGL/OpenGLTexture.h>
namespace X_Y {

	Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification)
	{
		RApiType Api = Render::instance()->getCurrentAPI()->getType();
		switch (Api)
		{
		case RApiType::None:
			XY_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;
		case RApiType::OpenGL:
			return CreateRef<OpenGLTexture2D>(specification);
		}

		XY_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		RApiType Api = Render::instance()->getCurrentAPI()->getType();
		switch (Api)
		{
		case RApiType::None:
			XY_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;
		case RApiType::OpenGL:
			return CreateRef<OpenGLTexture2D>(path);
		}

		XY_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}
