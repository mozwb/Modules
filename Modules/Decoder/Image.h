#pragma once

#include "Decoder.h"
#include "Memory/Memory.h"
#include "IO/Files.h"

namespace X_Y::Decode
{

	/**
	 * Image — 引擎图片抽象层
	 *
	 * 封装常见栅格图片格式解码，提供像素数据、元信息。
	 * 当前由 stb_image 支持 JPEG、PNG、BMP、GIF、TGA、PSD、HDR、PIC、PNM。
	 * 设计目标：① 内存加载 ② 不暴露底层解码器 ③ 可直接喂给 Texture2D 创建 OpenGL 纹理。
	 *
	 * 典型用法：
	 *   Image img("textures/foo.png");
	 *   // 或
	 *   Buffer fileData = FilesSystem::ReadFileBinary("textures/foo.png");
	 *   Image img(fileData);
	 */

	enum class ImageError
	{
		None = 0,
		InvalidData,
		UnsupportedFormat,
		DecodeFailed,
		FileNotFound
	};

	class Image : public DecoderBlueprint<const Buffer>,
				  public DecoderBlueprint<const File>
	{
	public:
		/* ── 从原始文件数据构造 ── */

		explicit Image(const Buffer &fileData);
		explicit Image(const File &filepath);

		void Decode(const Buffer &fileData) override;
		void Decode(const File &filepath) override;

		Image() = default;
		Image(Image &&other) noexcept;
		Image &operator=(Image &&other) noexcept;

		Image(const Image &) = default;
		Image &operator=(const Image &) = default;

		~Image() = default;

		/* ── 元信息 ── */

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetChannels() const { return m_Channels; }

		/* 像素数据 — Buffer 不拥有该内存，仅作只读视图 */

		BufferView GetPixels() const { return BufferView(m_Pixels); }
		const Buffer &GetPixelBuffer() const { return m_Pixels; }

		/* 是否成功加载 */

		bool IsLoaded() const { return m_Loaded; }

		/* 最后一次加载的错误信息 */

		ImageError GetError() const { return m_Error; }

		/* ── 工具 ── */

		uint64_t GetPixelCount() const { return (uint64_t)m_Width * m_Height; }
		uint64_t GetDataSize() const { return (uint64_t)m_Width * m_Height * m_Channels; }

		/* 显式深拷贝 */

		Image Copy() const;

	private:
		Buffer m_Pixels; // 解码后的像素数据（owned）
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Channels = 0;
		bool m_Loaded = false;
		ImageError m_Error = ImageError::None;
	};

}
// namespace X_Y
