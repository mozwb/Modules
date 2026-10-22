#include "Render/src/OpenGL/OpenGLTexture.h"
#include "Decoder/Image.h"
#include "Log/XYLog.h"

#include <glad/glad.h>

namespace X_Y {

	/* ────────────────────────────────────────────────
	 *  便利函数：ImageFormat → OpenGL 格式
	 * ──────────────────────────────────────────────── */
	static void GetGLFormats(ImageFormat fmt, GLenum& internal, GLenum& data)
	{
		switch (fmt)
		{
		case ImageFormat::R8:
			internal = GL_R8;
			data = GL_RED;
			break;
		case ImageFormat::RG8:
			internal = GL_RG8;
			data = GL_RG;
			break;
		case ImageFormat::RGB8:
			internal = GL_RGB8;
			data = GL_RGB;
			break;
		case ImageFormat::RGBA8:
			internal = GL_RGBA8;
			data = GL_RGBA;
			break;
		case ImageFormat::RGBA32F:
			internal = GL_RGBA32F;
			data = GL_RGBA;
			break;
		default:
			internal = GL_RGBA8;
			data = GL_RGBA;
			break;
		}
	}

	/* ────────────────────────────────────────────────
	 *  从 specification 构造（空纹理）
	 * ──────────────────────────────────────────────── */
	OpenGLTexture2D::OpenGLTexture2D(const TextureSpecification& specification)
		: m_Specification(specification)
		, m_Width(specification.Width)
		, m_Height(specification.Height)
		, m_Path("")
	{
		GetGLFormats(specification.Format, m_InternalFormat, m_DataFormat);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		if (specification.GenerateMips)
		{
			glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		else
		{
			glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		m_IsLoaded = true;
		XDEBUG("OpenGLTexture2D: created empty texture {0}x{1}", m_Width, m_Height);
	}

	/* ────────────────────────────────────────────────
	 *  从文件路径构造（通过 Image 层解码）
	 * ──────────────────────────────────────────────── */
	OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
		: m_Path(path)
	{
		Decode::Image img(path);
		if (!img.IsLoaded())
		{
			XFATAL("OpenGLTexture2D: failed to load image: {0}", path);
			m_RendererID = 0;
			m_Width = 0;
			m_Height = 0;
			m_IsLoaded = false;
			return;
		}

		m_Width  = img.GetWidth();
		m_Height = img.GetHeight();

		/* ── 根据通道数决定格式 ── */
		switch (img.GetChannels())
		{
		case 1:
			m_Specification.Format = ImageFormat::R8;
			m_InternalFormat = GL_R8;
			m_DataFormat = GL_RED;
			break;
		case 2:
			m_Specification.Format = ImageFormat::RG8;
			m_InternalFormat = GL_RG8;
			m_DataFormat = GL_RG;
			break;
		case 3:
			m_Specification.Format = ImageFormat::RGB8;
			m_InternalFormat = GL_RGB8;
			m_DataFormat = GL_RGB;
			break;
		case 4:
			m_Specification.Format = ImageFormat::RGBA8;
			m_InternalFormat = GL_RGBA8;
			m_DataFormat = GL_RGBA;
			break;
		default:
			XFATAL("OpenGLTexture2D: unsupported channel count {0}: {1}", img.GetChannels(), path);
			m_IsLoaded = false;
			return;
		}

		m_Specification.Width  = m_Width;
		m_Specification.Height = m_Height;

		/* ── 创建 OpenGL 纹理 ── */
		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (img.GetChannels() == 1)
		{
			glTextureParameteri(m_RendererID, GL_TEXTURE_SWIZZLE_G, GL_RED);
			glTextureParameteri(m_RendererID, GL_TEXTURE_SWIZZLE_B, GL_RED);
		}
		else if (img.GetChannels() == 2)
		{
			glTextureParameteri(m_RendererID, GL_TEXTURE_SWIZZLE_G, GL_RED);
			glTextureParameteri(m_RendererID, GL_TEXTURE_SWIZZLE_B, GL_RED);
			glTextureParameteri(m_RendererID, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
		}

		/* ── 上传像素数据 ── */
		const Buffer& pixels = img.GetPixelBuffer();
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
			m_DataFormat, GL_UNSIGNED_BYTE, pixels.Data);

		/* ── 生成 mipmap ── */
		if (m_Specification.GenerateMips)
		{
			glGenerateMipmap(GL_TEXTURE_2D);
			glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		}

		m_IsLoaded = true;
		XDEBUG("OpenGLTexture2D: loaded texture from {0}: {1}x{2}x{3}",
			path, m_Width, m_Height, img.GetChannels());
	}

	/* ────────────────────────────────────────────────
	 *  析构
	 * ──────────────────────────────────────────────── */
	OpenGLTexture2D::~OpenGLTexture2D()
	{
		if (m_RendererID)
			glDeleteTextures(1, &m_RendererID);
	}

	/* ────────────────────────────────────────────────
	 *  SetData：替换纹理内容
	 * ──────────────────────────────────────────────── */
	void OpenGLTexture2D::SetData(void* data, uint32_t size)
	{
		uint32_t expected = m_Width * m_Height * (
			m_DataFormat == GL_RGBA ? 4 :
			m_DataFormat == GL_RGB  ? 3 :
			m_DataFormat == GL_RG   ? 2 : 1
		);
		XY_CORE_ASSERT(size == expected, "OpenGLTexture2D::SetData: size mismatch ({0} != {1})", size, expected);

		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
			m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	/* ────────────────────────────────────────────────
	 *  Bind
	 * ──────────────────────────────────────────────── */
	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}

} // namespace X_Y
