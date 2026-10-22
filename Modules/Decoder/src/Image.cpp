#include "Decoder/Image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

#include "Files/Files.h"
#include <limits>

namespace X_Y::Decode
{
	Image::Image(const File &filepath)
	{
		Decode(filepath);
	}

	/* ────────────────────────────────────────────────
	 *  构造函数：从已读取的二进制数据
	 * ──────────────────────────────────────────────── */
	Image::Image(const Buffer &fileData)
	{
		Decode(fileData);
	}

	void Image::Decode(const File &filepath)
	{
		m_Pixels.Release();
		m_Width = 0;
		m_Height = 0;
		m_Channels = 0;
		m_Loaded = false;
		m_Error = ImageError::None;

		Buffer fileData = FilesSystem::ReadFileBinary(filepath);
		if (!fileData)
		{
			m_Error = ImageError::FileNotFound;
			return;
		}
		Decode(fileData);
	}

	void Image::Decode(const Buffer &fileData)
	{
		m_Pixels.Release();
		m_Width = 0;
		m_Height = 0;
		m_Channels = 0;
		m_Loaded = false;
		m_Error = ImageError::None;

		if (!fileData || fileData.Size == 0)
		{
			m_Error = ImageError::InvalidData;
			return;
		}

		if (fileData.Size > static_cast<uint64_t>(std::numeric_limits<int>::max()))
		{
			m_Error = ImageError::DecodeFailed;
			return;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc *pixels = stbi_load_from_memory(
			fileData.Data,
			static_cast<int>(fileData.Size),
			&width,
			&height,
			&channels,
			0);
		if (!pixels)
		{
			m_Error = ImageError::DecodeFailed;
			return;
		}

		const uint64_t pixelSize =
			static_cast<uint64_t>(width) *
			static_cast<uint64_t>(height) *
			static_cast<uint64_t>(channels);
		m_Pixels.Overwrite(pixels, pixelSize);
		stbi_image_free(pixels);
		if (m_Pixels.Size != pixelSize)
		{
			m_Error = ImageError::DecodeFailed;
			return;
		}

		m_Width = static_cast<uint32_t>(width);
		m_Height = static_cast<uint32_t>(height);
		m_Channels = static_cast<uint32_t>(channels);
		m_Loaded = true;
	}

	Image::Image(Image &&other) noexcept
		: m_Pixels(std::move(other.m_Pixels)),
		  m_Width(other.m_Width),
		  m_Height(other.m_Height),
		  m_Channels(other.m_Channels),
		  m_Loaded(other.m_Loaded),
		  m_Error(other.m_Error)
	{
		other.m_Width = 0;
		other.m_Height = 0;
		other.m_Channels = 0;
		other.m_Loaded = false;
		other.m_Error = ImageError::None;
	}

	Image &Image::operator=(Image &&other) noexcept
	{
		if (this != &other)
		{
			m_Pixels = std::move(other.m_Pixels);
			m_Width = other.m_Width;
			m_Height = other.m_Height;
			m_Channels = other.m_Channels;
			m_Loaded = other.m_Loaded;
			m_Error = other.m_Error;

			other.m_Width = 0;
			other.m_Height = 0;
			other.m_Channels = 0;
			other.m_Loaded = false;
			other.m_Error = ImageError::None;
		}
		return *this;
	}

	Image Image::Copy() const
	{
		return *this;
	}

} // namespace X_Y::Decode
