#include "Decoder/Media.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace X_Y::Decode
{
	struct Media::Impl
	{
		AVFormatContext *Format = nullptr;
		AVIOContext *IoContext = nullptr;
		AVCodecContext *VideoCodec = nullptr;
		AVCodecContext *AudioCodec = nullptr;
		AVPacket *Packet = nullptr;
		AVFrame *Frame = nullptr;
		SwsContext *Scaler = nullptr;
		SwrContext *Resampler = nullptr;
		std::unique_ptr<FileStream> OwnedStream;
		Stream *InputStream = nullptr;
		AVChannelLayout ResamplerLayout{};
		AVSampleFormat ResamplerFormat = AV_SAMPLE_FMT_NONE;
		int ResamplerRate = 0;

		AVCodecContext *PendingCodec = nullptr;
		MediaStreamType PendingType = MediaStreamType::Video;
		int VideoStream = -1;
		int AudioStream = -1;
		bool DemuxEnded = false;
		bool VideoFlushSent = false;
		bool AudioFlushSent = false;
		double SeekTargetSeconds = 0.0;
		MediaInfo Info;
		MediaError Error = MediaError::None;
		std::string ErrorMessage;

		void Reset()
		{
			av_packet_free(&Packet);
			av_frame_free(&Frame);
			avcodec_free_context(&VideoCodec);
			avcodec_free_context(&AudioCodec);
			avformat_close_input(&Format);
			avio_context_free(&IoContext);
			if (OwnedStream)
				OwnedStream->Close();
			OwnedStream.reset();
			InputStream = nullptr;
			sws_freeContext(Scaler);
			Scaler = nullptr;
			swr_free(&Resampler);
			av_channel_layout_uninit(&ResamplerLayout);
			ResamplerFormat = AV_SAMPLE_FMT_NONE;
			ResamplerRate = 0;
			PendingCodec = nullptr;
			VideoStream = -1;
			AudioStream = -1;
			DemuxEnded = false;
			VideoFlushSent = false;
			AudioFlushSent = false;
			SeekTargetSeconds = 0.0;
			Info = {};
			Error = MediaError::None;
			ErrorMessage.clear();
		}

		static int ReadPacket(void *opaque, uint8_t *buffer, int bufferSize)
		{
			if (bufferSize <= 0)
				return AVERROR(EINVAL);
			auto *stream = static_cast<Stream *>(opaque);
			const StreamReadResult result = stream->Read(
				buffer,
				static_cast<uint64_t>(bufferSize));
			if (result.Status == StreamReadStatus::Error)
				return AVERROR(EIO);
			if (result.BytesRead == 0)
				return AVERROR_EOF;
			if (result.BytesRead > static_cast<uint64_t>(std::numeric_limits<int>::max()))
				return AVERROR(EIO);
			return static_cast<int>(result.BytesRead);
		}

		static int64_t SeekStream(void *opaque, int64_t offset, int whence)
		{
			auto *stream = static_cast<Stream *>(opaque);
			if ((whence & AVSEEK_SIZE) != 0)
			{
				const std::optional<uint64_t> size = stream->Size();
				return size && *size <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
						   ? static_cast<int64_t>(*size)
						   : AVERROR(ENOSYS);
			}

			whence &= ~AVSEEK_FORCE;
			uint64_t base = 0;
			if (whence == SEEK_CUR)
				base = stream->Tell();
			else if (whence == SEEK_END)
			{
				const std::optional<uint64_t> size = stream->Size();
				if (!size)
					return AVERROR(ENOSYS);
				base = *size;
			}
			else if (whence != SEEK_SET)
			{
				return AVERROR(EINVAL);
			}

			uint64_t position = 0;
			if (offset < 0)
			{
				const uint64_t distance = static_cast<uint64_t>(-(offset + 1)) + 1;
				if (distance > base)
					return AVERROR(EINVAL);
				position = base - distance;
			}
			else
			{
				const uint64_t distance = static_cast<uint64_t>(offset);
				if (distance > std::numeric_limits<uint64_t>::max() - base)
					return AVERROR(EINVAL);
				position = base + distance;
			}

			if (position > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
				return AVERROR(EINVAL);
			if (!stream->Seek(position))
				return AVERROR(EIO);
			return static_cast<int64_t>(stream->Tell());
		}

		bool Fail(MediaError error, std::string message)
		{
			Error = error;
			ErrorMessage = std::move(message);
			return false;
		}

		std::string AvError(int error) const
		{
			char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
			av_strerror(error, buffer, sizeof(buffer));
			return buffer;
		}

		bool OpenCodec(int streamIndex, AVCodecContext **codecContext)
		{
			const AVStream *stream = Format->streams[streamIndex];
			const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
			if (!codec)
				return Fail(MediaError::DecoderUnavailable, "No FFmpeg decoder is available for a media stream");

			*codecContext = avcodec_alloc_context3(codec);
			if (!*codecContext)
				return Fail(MediaError::DecodeFailed, "Failed to allocate an FFmpeg decoder context");

			int result = avcodec_parameters_to_context(*codecContext, stream->codecpar);
			if (result < 0)
				return Fail(MediaError::DecodeFailed, "Failed to configure an FFmpeg decoder: " + AvError(result));

			result = avcodec_open2(*codecContext, codec, nullptr);
			if (result < 0)
				return Fail(MediaError::DecoderUnavailable, "Failed to open an FFmpeg decoder: " + AvError(result));
			return true;
		}

		bool OpenStream(Stream *stream)
		{
			InputStream = stream;
			constexpr int ioBufferSize = 32768;
			unsigned char *ioBuffer = static_cast<unsigned char *>(av_malloc(ioBufferSize));
			if (!ioBuffer)
				return Fail(MediaError::DecodeFailed, "Failed to allocate an FFmpeg input buffer");

			IoContext = avio_alloc_context(
				ioBuffer,
				ioBufferSize,
				0,
				stream,
				&ReadPacket,
				nullptr,
				&SeekStream);
			if (!IoContext)
			{
				av_free(ioBuffer);
				return Fail(MediaError::DecodeFailed, "Failed to create an FFmpeg input stream");
			}

			Format = avformat_alloc_context();
			if (!Format)
				return Fail(MediaError::DecodeFailed, "Failed to allocate an FFmpeg format context");
			Format->pb = IoContext;
			Format->flags |= AVFMT_FLAG_CUSTOM_IO;

			int result = avformat_open_input(&Format, nullptr, nullptr, nullptr);
			if (result < 0)
				return Fail(MediaError::UnsupportedMedia, "FFmpeg could not open the media stream: " + AvError(result));

			result = avformat_find_stream_info(Format, nullptr);
			if (result < 0)
				return Fail(MediaError::UnsupportedMedia, "FFmpeg could not read media stream information: " + AvError(result));

			const AVCodec *decoder = nullptr;
			VideoStream = av_find_best_stream(
				Format,
				AVMEDIA_TYPE_VIDEO,
				-1,
				-1,
				&decoder,
				0);
			if (VideoStream >= 0)
			{
				if (!OpenCodec(VideoStream, &VideoCodec))
				{
					const MediaError error = Error;
					const std::string message = ErrorMessage;
					Reset();
					return Fail(error, message);
				}

				AVStream *videoStream = Format->streams[VideoStream];
				const AVRational frameRate = av_guess_frame_rate(Format, videoStream, nullptr);
				Info.HasVideo = true;
				Info.Video.Index = VideoStream;
				Info.Video.Width = static_cast<uint32_t>(videoStream->codecpar->width);
				Info.Video.Height = static_cast<uint32_t>(videoStream->codecpar->height);
				if (frameRate.den != 0)
					Info.Video.FramesPerSecond = av_q2d(frameRate);
			}

			AudioStream = av_find_best_stream(
				Format,
				AVMEDIA_TYPE_AUDIO,
				-1,
				-1,
				&decoder,
				0);
			if (AudioStream >= 0)
			{
				if (!OpenCodec(AudioStream, &AudioCodec))
				{
					const MediaError error = Error;
					const std::string message = ErrorMessage;
					Reset();
					return Fail(error, message);
				}

				const AVStream *audioStream = Format->streams[AudioStream];
				Info.HasAudio = true;
				Info.Audio.Index = AudioStream;
				Info.Audio.SampleRate = static_cast<uint32_t>(audioStream->codecpar->sample_rate);
				Info.Audio.Channels =
					static_cast<uint32_t>(audioStream->codecpar->ch_layout.nb_channels);
			}

			if (!Info.HasVideo && !Info.HasAudio)
			{
				Reset();
				return Fail(MediaError::UnsupportedMedia, "The file contains no decodable audio or video stream");
			}

			if (Format->duration != AV_NOPTS_VALUE)
				Info.DurationSeconds =
					static_cast<double>(Format->duration) / AV_TIME_BASE;
			Packet = av_packet_alloc();
			Frame = av_frame_alloc();
			if (!Packet || !Frame)
			{
				Reset();
				return Fail(MediaError::DecodeFailed, "Failed to allocate FFmpeg packet and frame buffers");
			}
			return true;
		}

		bool SetResampler(const AVFrame *frame)
		{
			const auto inputFormat = static_cast<AVSampleFormat>(frame->format);
			const int inputRate = frame->sample_rate;
			const AVChannelLayout *inputLayout = &frame->ch_layout;
			AVChannelLayout fallbackLayout{};
			if (inputLayout->nb_channels <= 0)
			{
				const int channels = AudioCodec->ch_layout.nb_channels;
				if (channels <= 0)
					return false;
				av_channel_layout_default(&fallbackLayout, channels);
				inputLayout = &fallbackLayout;
			}

			const bool sameLayout =
				Resampler && av_channel_layout_compare(&ResamplerLayout, inputLayout) == 0;
			if (Resampler && sameLayout && ResamplerFormat == inputFormat &&
				ResamplerRate == inputRate)
			{
				av_channel_layout_uninit(&fallbackLayout);
				return true;
			}

			swr_free(&Resampler);
			av_channel_layout_uninit(&ResamplerLayout);
			ResamplerFormat = AV_SAMPLE_FMT_NONE;
			ResamplerRate = 0;

			int result = swr_alloc_set_opts2(
				&Resampler,
				inputLayout,
				AV_SAMPLE_FMT_FLT,
				inputRate,
				inputLayout,
				inputFormat,
				inputRate,
				0,
				nullptr);
			if (result >= 0)
				result = swr_init(Resampler);
			if (result < 0)
			{
				swr_free(&Resampler);
				av_channel_layout_uninit(&fallbackLayout);
				return false;
			}

			result = av_channel_layout_copy(&ResamplerLayout, inputLayout);
			av_channel_layout_uninit(&fallbackLayout);
			if (result < 0)
			{
				swr_free(&Resampler);
				return false;
			}

			ResamplerFormat = inputFormat;
			ResamplerRate = inputRate;
			return true;
		}

		bool ConvertFrame(MediaFrame &output)
		{
			const AVStream *stream = Format->streams[PendingType == MediaStreamType::Video ? VideoStream : AudioStream];
			output.Type = PendingType;
			output.StreamIndex = stream->index;
			if (Frame->best_effort_timestamp != AV_NOPTS_VALUE)
				output.TimestampSeconds = Frame->best_effort_timestamp * av_q2d(stream->time_base);

			if (PendingType == MediaStreamType::Video)
			{
				if (Frame->width <= 0 || Frame->height <= 0 ||
					Frame->width > std::numeric_limits<int>::max() / 4)
					return Fail(MediaError::DecodeFailed, "FFmpeg produced invalid video dimensions");

				const uint64_t rowBytes = static_cast<uint64_t>(Frame->width) * 4;
				if (static_cast<uint64_t>(Frame->height) >
					std::numeric_limits<uint64_t>::max() / rowBytes)
					return Fail(MediaError::DecodeFailed, "Decoded video frame is too large");

				const uint64_t dataSize = rowBytes * static_cast<uint64_t>(Frame->height);
				output.Data.Allocate(dataSize);
				if (!output.Data.Data || output.Data.Size != dataSize)
					return Fail(MediaError::DecodeFailed, "Failed to allocate decoded video pixels");

				Scaler = sws_getCachedContext(
					Scaler,
					Frame->width,
					Frame->height,
					static_cast<AVPixelFormat>(Frame->format),
					Frame->width,
					Frame->height,
					AV_PIX_FMT_RGBA,
					SWS_BILINEAR,
					nullptr,
					nullptr,
					nullptr);
				if (!Scaler)
					return Fail(MediaError::DecodeFailed, "Failed to initialize FFmpeg video conversion");

				const uint8_t *sourceData[AV_NUM_DATA_POINTERS] = {};
				for (size_t index = 0; index < AV_NUM_DATA_POINTERS; ++index)
					sourceData[index] = Frame->data[index];
				uint8_t *destinationData[] = {output.Data.Data};
				const int destinationStride = Frame->width * 4;
				const int destinationStrides[] = {destinationStride};
				const int rows = sws_scale(
					Scaler,
					sourceData,
					Frame->linesize,
					0,
					Frame->height,
					destinationData,
					destinationStrides);
				if (rows != Frame->height)
					return Fail(MediaError::DecodeFailed, "FFmpeg failed to convert a video frame");

				output.Width = static_cast<uint32_t>(Frame->width);
				output.Height = static_cast<uint32_t>(Frame->height);
				return true;
			}

			if (Frame->nb_samples <= 0 || Frame->sample_rate <= 0)
				return Fail(MediaError::DecodeFailed, "FFmpeg produced invalid audio frame metadata");
			if (!SetResampler(Frame))
				return Fail(MediaError::DecodeFailed, "Failed to initialize FFmpeg audio conversion");

			const int channels = Frame->ch_layout.nb_channels > 0
									 ? Frame->ch_layout.nb_channels
									 : ResamplerLayout.nb_channels;
			if (channels <= 0 || channels > std::numeric_limits<int>::max() / static_cast<int>(sizeof(float)))
				return Fail(MediaError::DecodeFailed, "FFmpeg produced invalid audio channel metadata");
			const int maxSamples = swr_get_out_samples(Resampler, Frame->nb_samples);
			if (channels <= 0 || maxSamples <= 0 ||
				static_cast<uint64_t>(maxSamples) >
					std::numeric_limits<uint64_t>::max() /
						(static_cast<uint64_t>(channels) * sizeof(float)))
				return Fail(MediaError::DecodeFailed, "Decoded audio frame is too large");

			const uint64_t capacity =
				static_cast<uint64_t>(maxSamples) * channels * sizeof(float);
			output.Data.Allocate(capacity);
			if (!output.Data.Data || output.Data.Size != capacity)
				return Fail(MediaError::DecodeFailed, "Failed to allocate decoded audio samples");

			std::vector<const uint8_t *> inputData(static_cast<size_t>(channels));
			for (int index = 0; index < channels; ++index)
				inputData[index] = Frame->extended_data[index];
			uint8_t *outputData[] = {output.Data.Data};
			const int samples = swr_convert(
				Resampler,
				outputData,
				maxSamples,
				inputData.data(),
				Frame->nb_samples);
			if (samples < 0)
				return Fail(MediaError::DecodeFailed, "FFmpeg failed to convert an audio frame: " + AvError(samples));

			output.Data.Size = static_cast<uint64_t>(samples) * channels * sizeof(float);
			output.SampleRate = static_cast<uint32_t>(Frame->sample_rate);
			output.Channels = static_cast<uint32_t>(channels);
			output.SampleCount = static_cast<uint32_t>(samples);
			return true;
		}

		MediaReadStatus ReadNext(MediaFrame &output)
		{
			output = {};
			for (;;)
			{
				if (PendingCodec)
				{
					const int result = avcodec_receive_frame(PendingCodec, Frame);
					if (result == 0)
					{
						if (!ConvertFrame(output))
						{
							av_frame_unref(Frame);
							return MediaReadStatus::Error;
						}
						av_frame_unref(Frame);

						if (output.TimestampSeconds < SeekTargetSeconds)
						{
							if (output.Type == MediaStreamType::Video ||
								output.SampleRate == 0 ||
								output.SampleCount == 0)
							{
								continue;
							}

							const double frameEndSeconds =
								output.TimestampSeconds +
								static_cast<double>(output.SampleCount) /
									output.SampleRate;
							if (frameEndSeconds <= SeekTargetSeconds)
								continue;

							const uint32_t samplesToSkip = static_cast<uint32_t>(
								std::ceil(
									(SeekTargetSeconds - output.TimestampSeconds) *
									output.SampleRate));
							const uint32_t skippedSamples =
								std::min(samplesToSkip, output.SampleCount);
							const uint64_t skippedBytes =
								static_cast<uint64_t>(skippedSamples) *
								output.Channels * sizeof(float);
							if (skippedBytes > output.Data.Size)
							{
								Fail(
									MediaError::DecodeFailed,
									"Decoded audio frame has an invalid sample buffer");
								return MediaReadStatus::Error;
							}
							std::memmove(
								output.Data.Data,
								output.Data.Data + skippedBytes,
								static_cast<size_t>(output.Data.Size - skippedBytes));
							output.Data.Size -= skippedBytes;
							output.SampleCount -= skippedSamples;
							output.TimestampSeconds =
								SeekTargetSeconds;
							if (output.SampleCount == 0)
								continue;
						}
						return MediaReadStatus::Frame;
					}

					PendingCodec = nullptr;
					if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
					{
						Fail(MediaError::DecodeFailed, "FFmpeg failed to decode a media frame: " + AvError(result));
						return MediaReadStatus::Error;
					}
				}

				if (DemuxEnded)
				{
					if (VideoCodec && !VideoFlushSent)
					{
						VideoFlushSent = true;
						const int result = avcodec_send_packet(VideoCodec, nullptr);
						if (result < 0 && result != AVERROR_EOF)
						{
							Fail(MediaError::DecodeFailed, "Failed to flush the video decoder: " + AvError(result));
							return MediaReadStatus::Error;
						}
						PendingCodec = VideoCodec;
						PendingType = MediaStreamType::Video;
						continue;
					}
					if (AudioCodec && !AudioFlushSent)
					{
						AudioFlushSent = true;
						const int result = avcodec_send_packet(AudioCodec, nullptr);
						if (result < 0 && result != AVERROR_EOF)
						{
							Fail(MediaError::DecodeFailed, "Failed to flush the audio decoder: " + AvError(result));
							return MediaReadStatus::Error;
						}
						PendingCodec = AudioCodec;
						PendingType = MediaStreamType::Audio;
						continue;
					}
					return MediaReadStatus::EndOfStream;
				}

				const int readResult = av_read_frame(Format, Packet);
				if (readResult == AVERROR_EOF)
				{
					DemuxEnded = true;
					continue;
				}
				if (readResult < 0)
				{
					Fail(MediaError::DecodeFailed, "Failed to read media packet: " + AvError(readResult));
					return MediaReadStatus::Error;
				}

				AVCodecContext *codec = nullptr;
				if (Packet->stream_index == VideoStream)
				{
					codec = VideoCodec;
					PendingType = MediaStreamType::Video;
				}
				else if (Packet->stream_index == AudioStream)
				{
					codec = AudioCodec;
					PendingType = MediaStreamType::Audio;
				}

				if (codec)
				{
					const int result = avcodec_send_packet(codec, Packet);
					av_packet_unref(Packet);
					if (result < 0)
					{
						Fail(MediaError::DecodeFailed, "Failed to submit a media packet: " + AvError(result));
						return MediaReadStatus::Error;
					}
					PendingCodec = codec;
				}
				else
				{
					av_packet_unref(Packet);
				}
			}
		}
	};

	Media::Media() : m_Impl(std::make_unique<Impl>()) {}

	Media::~Media() = default;

	Media::Media(Media &&other) noexcept = default;

	Media &Media::operator=(Media &&other) noexcept = default;

	void Media::Decode(const File &filepath)
	{
		Open(filepath);
	}

	void Media::Decode(Stream &stream)
	{
		Open(stream);
	}

	bool Media::Open(const File &filepath)
	{
		Close();
		if (filepath.empty())
			return m_Impl->Fail(MediaError::InvalidPath, "Media file path is empty");

		auto stream = std::make_unique<FileStream>();
		if (!stream->Open(filepath))
			return m_Impl->Fail(MediaError::FileNotFound, "Media file does not exist: " + filepath.string());

		m_Impl->OwnedStream = std::move(stream);
		if (m_Impl->OpenStream(m_Impl->OwnedStream.get()))
			return true;

		const MediaError error = m_Impl->Error;
		const std::string message = m_Impl->ErrorMessage;
		m_Impl->Reset();
		return m_Impl->Fail(error, message);
	}

	bool Media::Open(Stream &stream)
	{
		Close();
		if (!stream.IsOpen())
			return m_Impl->Fail(MediaError::InvalidPath, "Input stream is not open");
		if (m_Impl->OpenStream(&stream))
			return true;

		const MediaError error = m_Impl->Error;
		const std::string message = m_Impl->ErrorMessage;
		m_Impl->Reset();
		return m_Impl->Fail(error, message);
	}

	void Media::Close()
	{
		m_Impl->Reset();
	}

	bool Media::IsOpen() const
	{
		return m_Impl->Format != nullptr;
	}

	const MediaInfo &Media::GetInfo() const
	{
		return m_Impl->Info;
	}

	MediaError Media::GetError() const
	{
		return m_Impl->Error;
	}

	const std::string &Media::GetErrorMessage() const
	{
		return m_Impl->ErrorMessage;
	}

	bool Media::Seek(double timeSeconds)
	{
		if (!IsOpen())
			return m_Impl->Fail(MediaError::InvalidPath, "No media file is open");
		if (!std::isfinite(timeSeconds) || timeSeconds < 0.0)
			return m_Impl->Fail(MediaError::InvalidPath, "Media seek time is invalid");

		const double boundedTime = m_Impl->Info.DurationSeconds > 0.0
									   ? std::min(timeSeconds, m_Impl->Info.DurationSeconds)
									   : timeSeconds;
		const long double timestamp =
			static_cast<long double>(boundedTime) * AV_TIME_BASE +
			(m_Impl->Format->start_time == AV_NOPTS_VALUE
				 ? 0.0L
				 : static_cast<long double>(m_Impl->Format->start_time));
		if (timestamp < std::numeric_limits<int64_t>::min() ||
			timestamp > std::numeric_limits<int64_t>::max())
			return m_Impl->Fail(MediaError::InvalidPath, "Media seek time is out of range");

		const int result = avformat_seek_file(
			m_Impl->Format,
			-1,
			std::numeric_limits<int64_t>::min(),
			static_cast<int64_t>(timestamp),
			std::numeric_limits<int64_t>::max(),
			AVSEEK_FLAG_BACKWARD);
		if (result < 0)
			return m_Impl->Fail(
				MediaError::DecodeFailed,
				"Failed to seek in media: " + m_Impl->AvError(result));

		av_packet_unref(m_Impl->Packet);
		av_frame_unref(m_Impl->Frame);
		if (m_Impl->VideoCodec)
			avcodec_flush_buffers(m_Impl->VideoCodec);
		if (m_Impl->AudioCodec)
			avcodec_flush_buffers(m_Impl->AudioCodec);
		swr_free(&m_Impl->Resampler);
		av_channel_layout_uninit(&m_Impl->ResamplerLayout);
		m_Impl->ResamplerFormat = AV_SAMPLE_FMT_NONE;
		m_Impl->ResamplerRate = 0;
		m_Impl->PendingCodec = nullptr;
		m_Impl->DemuxEnded = false;
		m_Impl->VideoFlushSent = false;
		m_Impl->AudioFlushSent = false;
		m_Impl->SeekTargetSeconds =
			static_cast<double>(timestamp) / AV_TIME_BASE;
		m_Impl->Error = MediaError::None;
		m_Impl->ErrorMessage.clear();
		return true;
	}

	MediaReadStatus Media::DecodeNext(MediaFrame &frame)
	{
		if (!IsOpen())
		{
			m_Impl->Fail(MediaError::InvalidPath, "No media file is open");
			return MediaReadStatus::Error;
		}
		return m_Impl->ReadNext(frame);
	}

	MediaReadStatus Media::ReadNext(MediaFrame &frame)
	{
		return DecodeNext(frame);
	}
}
