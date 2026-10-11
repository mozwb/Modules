#include "Media/MediaPlayer.h"

#include "XCore/Input/Audio/AudioOutputDevice.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace X_Y::Media
{
	struct MediaPlayer::Impl
	{
		std::unique_ptr<Decode::Media> Decoder;
		std::unique_ptr<Input::AudioOutputDevice> Device;
		std::thread DecodeThread;
		std::filesystem::path FilePath;
		Decode::MediaInfo Info;
		Input::AudioFormat Format;
		std::vector<float> Samples;
		std::deque<Decode::MediaFrame> VideoFrames;
		std::atomic<uint64_t> ReadPosition{0};
		std::atomic<uint64_t> WritePosition{0};
		std::atomic<uint64_t> PlayedSamples{0};
		std::atomic<bool> Shutdown{false};
		std::atomic<bool> Playing{false};
		std::atomic<bool> EndOfStream{false};
		std::atomic<bool> Failed{false};
		std::atomic<PlaybackState> State{PlaybackState::Closed};
		std::mutex VideoMutex;
		std::condition_variable VideoQueueCondition;
		double VideoStartTimestamp = 0.0;
		bool HasVideoStartTimestamp = false;
		double VideoClockElapsedSeconds = 0.0;
		std::chrono::steady_clock::time_point VideoClockStartedAt;
		std::atomic<bool> AudioDrained{false};
		mutable std::mutex LifecycleMutex;
		mutable std::mutex ErrorMutex;
		std::string Error;

		bool SetError(std::string message)
		{
			{
				std::lock_guard lock(ErrorMutex);
				Error = std::move(message);
			}
			State.store(PlaybackState::Error, std::memory_order_release);
			return false;
		}

		void ClearError()
		{
			std::lock_guard lock(ErrorMutex);
			Error.clear();
		}

		void StopDecodeThread()
		{
			Shutdown.store(true, std::memory_order_release);
			if (DecodeThread.joinable())
				DecodeThread.join();
		}

		void CloseLocked()
		{
			Playing.store(false, std::memory_order_release);
			Shutdown.store(true, std::memory_order_release);
			VideoQueueCondition.notify_all();
			StopDecodeThread();
			if (Device)
			{
				Device->Close();
				Device.reset();
			}
			if (Decoder)
			{
				Decoder->Close();
				Decoder.reset();
			}
			FilePath.clear();
			Info = {};
			Samples.clear();
			ReadPosition.store(0, std::memory_order_relaxed);
			WritePosition.store(0, std::memory_order_relaxed);
			PlayedSamples.store(0, std::memory_order_relaxed);
			{
				std::lock_guard videoLock(VideoMutex);
				VideoFrames.clear();
				HasVideoStartTimestamp = false;
			}
			VideoClockElapsedSeconds = 0.0;
			AudioDrained.store(false, std::memory_order_relaxed);
			EndOfStream.store(false, std::memory_order_relaxed);
			Failed.store(false, std::memory_order_relaxed);
			Shutdown.store(false, std::memory_order_relaxed);
			State.store(PlaybackState::Closed, std::memory_order_release);
		}

		bool OpenDecoder()
		{
			auto decoder = std::make_unique<Decode::Media>();
			if (!decoder->Open(FilePath))
				return SetError(decoder->GetErrorMessage());

			Info = decoder->GetInfo();
			if (!Info.HasAudio && !Info.HasVideo)
				return SetError("The media file contains no audio or video stream");
			if (Info.HasAudio &&
				(Info.Audio.SampleRate == 0 || Info.Audio.Channels == 0 ||
				 Info.Audio.Channels > 2))
				return SetError("Audio playback currently supports mono or stereo media");

			Decoder = std::move(decoder);
			if (Info.HasAudio)
			{
				Format.SampleRate = Info.Audio.SampleRate;
				Format.Channels = Info.Audio.Channels;
			}
			return true;
		}

		bool StartDecodeThread()
		{
			Shutdown.store(false, std::memory_order_release);
			EndOfStream.store(false, std::memory_order_release);
			Failed.store(false, std::memory_order_release);
			try
			{
				DecodeThread = std::thread([this] { DecodeLoop(); });
			}
			catch (const std::system_error &error)
			{
				Shutdown.store(true, std::memory_order_release);
				Failed.store(true, std::memory_order_release);
				return SetError(
					std::string("Failed to start the media decoder thread: ") + error.what());
			}
			return true;
		}

		void DecodeLoop()
		{
			for (;;)
			{
				if (Shutdown.load(std::memory_order_acquire))
					return;

				Decode::MediaFrame frame;
				const Decode::MediaReadStatus status = Decoder->DecodeNext(frame);
				if (status == Decode::MediaReadStatus::EndOfStream)
				{
					EndOfStream.store(true, std::memory_order_release);
					VideoQueueCondition.notify_all();
					return;
				}
				if (status == Decode::MediaReadStatus::Error)
				{
					Failed.store(true, std::memory_order_release);
					SetError(Decoder->GetErrorMessage());
					EndOfStream.store(true, std::memory_order_release);
					VideoQueueCondition.notify_all();
					return;
				}
				if (frame.Type == Decode::MediaStreamType::Video)
				{
					std::unique_lock videoLock(VideoMutex);
					VideoQueueCondition.wait(videoLock, [this]
					{
						return Shutdown.load(std::memory_order_acquire) ||
							   (Playing.load(std::memory_order_acquire) &&
								VideoFrames.size() < 6);
					});
					if (Shutdown.load(std::memory_order_acquire))
						return;
					if (!HasVideoStartTimestamp)
					{
						VideoStartTimestamp = frame.TimestampSeconds;
						HasVideoStartTimestamp = true;
					}
					VideoFrames.push_back(std::move(frame));
					continue;
				}
				if (frame.Type != Decode::MediaStreamType::Audio)
					continue;
				if (frame.SampleRate != Format.SampleRate ||
					frame.Channels != Format.Channels ||
					frame.Data.Size % sizeof(float) != 0)
				{
					Failed.store(true, std::memory_order_release);
					SetError("Decoder returned audio with an inconsistent PCM format");
					EndOfStream.store(true, std::memory_order_release);
					VideoQueueCondition.notify_all();
					return;
				}

				const uint64_t sampleCount = frame.Data.Size / sizeof(float);
				if (!frame.Data.Data || sampleCount == 0 ||
					sampleCount % Format.Channels != 0)
					continue;

				uint64_t frameOffset = 0;
				while (frameOffset < sampleCount)
				{
					if (Shutdown.load(std::memory_order_acquire))
						return;

					const uint64_t writePosition =
						WritePosition.load(std::memory_order_relaxed);
					const uint64_t readPosition =
						ReadPosition.load(std::memory_order_acquire);
					const uint64_t queued = writePosition - readPosition;
					if (queued > Samples.size())
					{
						Failed.store(true, std::memory_order_release);
						SetError("Media audio sample queue indices are inconsistent");
						EndOfStream.store(true, std::memory_order_release);
						VideoQueueCondition.notify_all();
						return;
					}

					const uint64_t available = Samples.size() - queued;
					uint64_t copyCount =
						std::min(available, sampleCount - frameOffset);
					copyCount -= copyCount % Format.Channels;
					if (copyCount == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
						continue;
					}

					const uint64_t capacity = Samples.size();
					const uint64_t ringIndex = writePosition % capacity;
					const uint64_t firstPart =
						std::min(copyCount, capacity - ringIndex);
					std::memcpy(
						Samples.data() + ringIndex,
						reinterpret_cast<const float *>(frame.Data.Data) + frameOffset,
						static_cast<size_t>(firstPart) * sizeof(float));
					if (firstPart < copyCount)
					{
						std::memcpy(
							Samples.data(),
							reinterpret_cast<const float *>(frame.Data.Data) +
								frameOffset + firstPart,
							static_cast<size_t>(copyCount - firstPart) * sizeof(float));
					}
					WritePosition.store(
						writePosition + copyCount, std::memory_order_release);
					frameOffset += copyCount;
				}
			}
		}

		void Render(
			float *output,
			uint32_t frameCount,
			const Input::AudioFormat &format)
		{
			const uint64_t requestedSamples =
				static_cast<uint64_t>(frameCount) * format.Channels;
			std::memset(output, 0, static_cast<size_t>(requestedSamples) * sizeof(float));
			if (!Playing.load(std::memory_order_acquire) ||
				format.Channels != Format.Channels)
				return;

			const uint64_t readPosition =
				ReadPosition.load(std::memory_order_relaxed);
			const uint64_t writePosition =
				WritePosition.load(std::memory_order_acquire);
			const uint64_t queued = writePosition - readPosition;
			const uint64_t copyCount = std::min(requestedSamples, queued);
			const uint64_t capacity = Samples.size();
			if (copyCount > 0 && capacity > 0)
			{
				const uint64_t ringIndex = readPosition % capacity;
				const uint64_t firstPart = std::min(copyCount, capacity - ringIndex);
				std::memcpy(
					output,
					Samples.data() + ringIndex,
					static_cast<size_t>(firstPart) * sizeof(float));
				if (firstPart < copyCount)
				{
					std::memcpy(
						output + firstPart,
						Samples.data(),
						static_cast<size_t>(copyCount - firstPart) * sizeof(float));
				}
				ReadPosition.store(
					readPosition + copyCount, std::memory_order_release);
				PlayedSamples.fetch_add(copyCount, std::memory_order_release);
			}

			if (EndOfStream.load(std::memory_order_acquire) &&
				copyCount < requestedSamples)
			{
				AudioDrained.store(true, std::memory_order_release);
				if (!Info.HasVideo)
				{
					Playing.store(false, std::memory_order_release);
					State.store(
						Failed.load(std::memory_order_acquire)
							? PlaybackState::Error
							: PlaybackState::Ended,
						std::memory_order_release);
				}
			}
		}
	};

	MediaPlayer::MediaPlayer() : m_Impl(std::make_unique<Impl>()) {}

	MediaPlayer::~MediaPlayer()
	{
		Close();
	}

	bool MediaPlayer::Open(const std::filesystem::path &filePath)
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		m_Impl->CloseLocked();
		m_Impl->ClearError();
		m_Impl->FilePath = filePath;
		if (filePath.empty())
		{
			m_Impl->FilePath.clear();
			return m_Impl->SetError("Media file path is empty");
		}

		if (!m_Impl->OpenDecoder())
		{
			m_Impl->Decoder.reset();
			m_Impl->FilePath.clear();
			return false;
		}

		if (m_Impl->Info.HasAudio)
		{
			const uint64_t queueFrames =
				static_cast<uint64_t>(m_Impl->Format.SampleRate) * 2;
			if (queueFrames >
				std::numeric_limits<size_t>::max() / m_Impl->Format.Channels)
			{
				m_Impl->CloseLocked();
				return m_Impl->SetError("Media audio sample queue is too large");
			}
			m_Impl->Samples.resize(
				static_cast<size_t>(queueFrames * m_Impl->Format.Channels));

			m_Impl->Device = Input::AudioOutputDeviceFactory::CreateDefault();
			if (!m_Impl->Device)
			{
				m_Impl->CloseLocked();
				return m_Impl->SetError(
					"No audio output backend is available on this platform");
			}
			if (!m_Impl->Device->Open(
					m_Impl->Format,
					[this](
						float *output,
						uint32_t frames,
						const Input::AudioFormat &format)
					{
						m_Impl->Render(output, frames, format);
					}))
			{
				const std::string error = m_Impl->Device->GetErrorMessage();
				m_Impl->CloseLocked();
				return m_Impl->SetError(
					error.empty() ? "Failed to open the audio output device" : error);
			}
		}

		if (!m_Impl->StartDecodeThread())
		{
			const std::string error = GetErrorMessage();
			m_Impl->CloseLocked();
			return m_Impl->SetError(error);
		}
		m_Impl->State.store(
			m_Impl->Failed.load(std::memory_order_acquire)
				? PlaybackState::Error
				: PlaybackState::Ready,
			std::memory_order_release);
		return true;
	}

	bool MediaPlayer::Play()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (!m_Impl->Decoder || !m_Impl->Decoder->IsOpen())
			return m_Impl->SetError("No media file is open");
		if (m_Impl->Failed.load(std::memory_order_acquire))
			return m_Impl->SetError("Media decoding failed: " + GetErrorMessage());
		if (m_Impl->EndOfStream.load(std::memory_order_acquire))
			return m_Impl->SetError("Media has ended; call Stop before playing it again");
		if (m_Impl->Playing.exchange(true, std::memory_order_acq_rel))
			return true;
		m_Impl->VideoClockStartedAt = std::chrono::steady_clock::now();
		if (m_Impl->Device && !m_Impl->Device->Start())
		{
			m_Impl->Playing.store(false, std::memory_order_release);
			return m_Impl->SetError(m_Impl->Device->GetErrorMessage());
		}
		m_Impl->VideoQueueCondition.notify_all();
		m_Impl->State.store(PlaybackState::Playing, std::memory_order_release);
		return true;
	}

	void MediaPlayer::Pause()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (m_Impl->State.load(std::memory_order_acquire) != PlaybackState::Playing)
			return;
		m_Impl->VideoClockElapsedSeconds += std::chrono::duration<double>(
			std::chrono::steady_clock::now() - m_Impl->VideoClockStartedAt).count();
		m_Impl->Playing.store(false, std::memory_order_release);
		if (m_Impl->Device)
			m_Impl->Device->Stop();
		m_Impl->State.store(PlaybackState::Paused, std::memory_order_release);
	}

	bool MediaPlayer::Stop()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (m_Impl->FilePath.empty() || !m_Impl->Decoder)
			return m_Impl->SetError("No media file is open");

		m_Impl->Playing.store(false, std::memory_order_release);
		if (m_Impl->Device)
			m_Impl->Device->Stop();
		m_Impl->Shutdown.store(true, std::memory_order_release);
		m_Impl->VideoQueueCondition.notify_all();
		m_Impl->StopDecodeThread();
		m_Impl->ReadPosition.store(0, std::memory_order_relaxed);
		m_Impl->WritePosition.store(0, std::memory_order_relaxed);
		m_Impl->PlayedSamples.store(0, std::memory_order_relaxed);
		{
			std::lock_guard videoLock(m_Impl->VideoMutex);
			m_Impl->VideoFrames.clear();
			m_Impl->HasVideoStartTimestamp = false;
		}
		m_Impl->VideoClockElapsedSeconds = 0.0;
		m_Impl->AudioDrained.store(false, std::memory_order_relaxed);
		m_Impl->Decoder->Close();
		m_Impl->Decoder.reset();
		if (!m_Impl->OpenDecoder())
		{
			if (m_Impl->Device)
			{
				m_Impl->Device->Close();
				m_Impl->Device.reset();
			}
			return false;
		}
		if (!m_Impl->StartDecodeThread())
		{
			m_Impl->Device->Close();
			m_Impl->Device.reset();
			return false;
		}
		m_Impl->ClearError();
		m_Impl->State.store(PlaybackState::Ready, std::memory_order_release);
		return true;
	}

	void MediaPlayer::Close()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		m_Impl->CloseLocked();
		m_Impl->ClearError();
	}

	bool MediaPlayer::TakeVideoFrame(Decode::MediaFrame &frame)
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (!m_Impl->Info.HasVideo ||
			!m_Impl->Playing.load(std::memory_order_acquire))
			return false;

		const double elapsed = m_Impl->VideoClockElapsedSeconds +
			std::chrono::duration<double>(
				std::chrono::steady_clock::now() -
				m_Impl->VideoClockStartedAt).count();
		bool receivedFrame = false;
		{
			std::lock_guard videoLock(m_Impl->VideoMutex);
			if (m_Impl->HasVideoStartTimestamp)
			{
				const double targetTimestamp =
					m_Impl->VideoStartTimestamp + elapsed;
				while (!m_Impl->VideoFrames.empty() &&
					   m_Impl->VideoFrames.front().TimestampSeconds <= targetTimestamp)
				{
					frame = std::move(m_Impl->VideoFrames.front());
					m_Impl->VideoFrames.pop_front();
					receivedFrame = true;
				}
			}
		}
		if (receivedFrame)
			m_Impl->VideoQueueCondition.notify_one();

		if (m_Impl->EndOfStream.load(std::memory_order_acquire))
		{
			std::lock_guard videoLock(m_Impl->VideoMutex);
			if (m_Impl->VideoFrames.empty() &&
				(!m_Impl->Info.HasAudio ||
				 m_Impl->AudioDrained.load(std::memory_order_acquire)))
			{
				m_Impl->Playing.store(false, std::memory_order_release);
				if (m_Impl->Device)
					m_Impl->Device->Stop();
				m_Impl->State.store(
					m_Impl->Failed.load(std::memory_order_acquire)
						? PlaybackState::Error
						: PlaybackState::Ended,
					std::memory_order_release);
			}
		}
		return receivedFrame;
	}

	bool MediaPlayer::IsOpen() const
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		return m_Impl->Decoder && m_Impl->Decoder->IsOpen() &&
			   (!m_Impl->Device || m_Impl->Device->IsOpen());
	}

	bool MediaPlayer::IsPlaying() const
	{
		return m_Impl->Playing.load(std::memory_order_acquire);
	}

	PlaybackState MediaPlayer::GetState() const
	{
		return m_Impl->State.load(std::memory_order_acquire);
	}

	double MediaPlayer::GetCurrentTimeSeconds() const
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (!m_Impl->Info.HasAudio)
		{
			if (m_Impl->Playing.load(std::memory_order_acquire))
			{
				return m_Impl->VideoClockElapsedSeconds +
					   std::chrono::duration<double>(
						   std::chrono::steady_clock::now() -
						   m_Impl->VideoClockStartedAt).count();
			}
			return m_Impl->VideoClockElapsedSeconds;
		}
		if (m_Impl->Format.SampleRate == 0 || m_Impl->Format.Channels == 0)
			return 0.0;
		return static_cast<double>(
				   m_Impl->PlayedSamples.load(std::memory_order_acquire)) /
			   m_Impl->Format.Channels / m_Impl->Format.SampleRate;
	}

	Decode::MediaInfo MediaPlayer::GetMediaInfo() const
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		return m_Impl->Info;
	}

	std::string MediaPlayer::GetErrorMessage() const
	{
		std::lock_guard lock(m_Impl->ErrorMutex);
		return m_Impl->Error;
	}
}
