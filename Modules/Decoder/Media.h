#pragma once

#include "Decoder.h"
#include "Memory/Memory.h"
#include "Files/Files.h"

#include <cstdint>
#include <memory>
#include <string>

namespace X_Y::Decode
{
	enum class MediaError
	{
		None = 0,
		InvalidPath,
		FileNotFound,
		UnsupportedMedia,
		DecoderUnavailable,
		DecodeFailed
	};

	enum class MediaStreamType
	{
		Video,
		Audio
	};

	enum class MediaReadStatus
	{
		Frame,
		EndOfStream,
		Error
	};

	struct VideoStreamInfo
	{
		int32_t Index = -1;
		uint32_t Width = 0;
		uint32_t Height = 0;
		double FramesPerSecond = 0.0;
	};

	struct AudioStreamInfo
	{
		int32_t Index = -1;
		uint32_t SampleRate = 0;
		uint32_t Channels = 0;
	};

	struct MediaInfo
	{
		double DurationSeconds = 0.0;
		bool HasVideo = false;
		bool HasAudio = false;
		VideoStreamInfo Video;
		AudioStreamInfo Audio;
	};

	struct MediaFrame
	{
		MediaStreamType Type = MediaStreamType::Video;
		int32_t StreamIndex = -1;
		double TimestampSeconds = 0.0;

		// Video is tightly packed RGBA8; audio is interleaved float32 PCM.
		Buffer Data;
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint32_t SampleRate = 0;
		uint32_t Channels = 0;
		uint32_t SampleCount = 0;
	};

	class Media : public DecoderBlueprint<const File>,
				  public DecoderBlueprint<Stream>
	{
	public:
		using Data = MediaFrame;

		Media();
		~Media();

		Media(const Media &) = delete;
		Media &operator=(const Media &) = delete;
		Media(Media &&other) noexcept;
		Media &operator=(Media &&other) noexcept;

		void Decode(const File &filepath) override;
		void Decode(Stream &stream) override;
		bool Open(const File &filepath);
		// The stream is borrowed and must remain open while this decoder is in use.
		bool Open(Stream &stream);
		void Close();

		bool IsOpen() const;
		const MediaInfo &GetInfo() const;
		MediaError GetError() const;
		const std::string &GetErrorMessage() const;

		bool Seek(double timeSeconds);
		MediaReadStatus DecodeNext(MediaFrame &frame);
		MediaReadStatus ReadNext(MediaFrame &frame);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
