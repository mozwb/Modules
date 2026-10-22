#include "Input/Input.h"

#include "Decoder/Media.h"
#include "Input/Audio/AudioOutputDevice.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace X_Y
{
	struct Input::AudioPlayer::Impl
	{
		std::unique_ptr<Decode::Media> Decoder;
		std::unique_ptr<Input::AudioOutputDevice> Device;
		std::thread DecodeThread;
		std::filesystem::path FilePath;
		Input::AudioFormat Format;
		std::vector<float> Samples;
		std::atomic<uint64_t> ReadPosition{0};
		std::atomic<uint64_t> WritePosition{0};
		std::atomic<bool> Shutdown{false};
		std::atomic<bool> Playing{false};
		std::atomic<bool> EndOfStream{false};
		std::atomic<bool> Failed{false};
		mutable std::mutex LifecycleMutex;
		mutable std::mutex ErrorMutex;
		std::string Error;

		bool SetError(std::string message)
		{
			std::lock_guard lock(ErrorMutex);
			Error = std::move(message);
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
			Samples.clear();
			ReadPosition.store(0, std::memory_order_relaxed);
			WritePosition.store(0, std::memory_order_relaxed);
			EndOfStream.store(false, std::memory_order_relaxed);
			Failed.store(false, std::memory_order_relaxed);
			Shutdown.store(false, std::memory_order_relaxed);
		}

		bool OpenDecoder()
		{
			auto decoder = std::make_unique<Decode::Media>();
			if (!decoder->Open(FilePath))
				return SetError(decoder->GetErrorMessage());

			const Decode::MediaInfo &info = decoder->GetInfo();
			if (!info.HasAudio)
				return SetError("The media file contains no audio stream");
			if (info.Audio.SampleRate == 0 || info.Audio.Channels == 0 ||
				info.Audio.Channels > 2)
				return SetError("Audio playback currently supports mono or stereo media");

			Decoder = std::move(decoder);
			Format.SampleRate = info.Audio.SampleRate;
			Format.Channels = info.Audio.Channels;
			return true;
		}

		bool StartDecodeThread()
		{
			Shutdown.store(false, std::memory_order_release);
			EndOfStream.store(false, std::memory_order_release);
			Failed.store(false, std::memory_order_release);
			try
			{
				DecodeThread = std::thread([this]
				{
					DecodeLoop();
				});
			}
			catch (const std::system_error &error)
			{
				Shutdown.store(true, std::memory_order_release);
				Failed.store(true, std::memory_order_release);
				return SetError(std::string("Failed to start the audio decoder thread: ") + error.what());
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
					return;
				}
				if (status == Decode::MediaReadStatus::Error)
				{
					SetError(Decoder->GetErrorMessage());
					Failed.store(true, std::memory_order_release);
					EndOfStream.store(true, std::memory_order_release);
					return;
				}
				if (frame.Type != Decode::MediaStreamType::Audio)
					continue;
				if (frame.SampleRate != Format.SampleRate ||
					frame.Channels != Format.Channels ||
					frame.Data.Size % sizeof(float) != 0)
				{
					SetError("Decoder returned audio with an inconsistent PCM format");
					Failed.store(true, std::memory_order_release);
					EndOfStream.store(true, std::memory_order_release);
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

					const uint64_t writePosition = WritePosition.load(std::memory_order_relaxed);
					const uint64_t readPosition = ReadPosition.load(std::memory_order_acquire);
					const uint64_t queued = writePosition - readPosition;
					if (queued > Samples.size())
					{
						SetError("Audio sample queue indices are inconsistent");
						Failed.store(true, std::memory_order_release);
						EndOfStream.store(true, std::memory_order_release);
						return;
					}

					const uint64_t available = Samples.size() - queued;
					uint64_t copyCount = std::min(available, sampleCount - frameOffset);
					copyCount -= copyCount % Format.Channels;
					if (copyCount == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
						continue;
					}

					const uint64_t capacity = Samples.size();
					const uint64_t ringIndex = writePosition % capacity;
					const uint64_t firstPart = std::min(copyCount, capacity - ringIndex);
					std::memcpy(
						Samples.data() + ringIndex,
						reinterpret_cast<const float *>(frame.Data.Data) + frameOffset,
						static_cast<size_t>(firstPart) * sizeof(float));
					if (firstPart < copyCount)
					{
						std::memcpy(
							Samples.data(),
							reinterpret_cast<const float *>(frame.Data.Data) + frameOffset + firstPart,
							static_cast<size_t>(copyCount - firstPart) * sizeof(float));
					}
					WritePosition.store(writePosition + copyCount, std::memory_order_release);
					frameOffset += copyCount;
				}
			}
		}

		void Render(float *output, uint32_t frameCount, const Input::AudioFormat &format)
		{
			const uint64_t requestedSamples =
				static_cast<uint64_t>(frameCount) * format.Channels;
			std::memset(output, 0, static_cast<size_t>(requestedSamples) * sizeof(float));
			if (!Playing.load(std::memory_order_acquire) || format.Channels != Format.Channels)
				return;

			const uint64_t readPosition = ReadPosition.load(std::memory_order_relaxed);
			const uint64_t writePosition = WritePosition.load(std::memory_order_acquire);
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
				ReadPosition.store(readPosition + copyCount, std::memory_order_release);
			}

			if (EndOfStream.load(std::memory_order_acquire) &&
				copyCount < requestedSamples)
			{
				Playing.store(false, std::memory_order_release);
			}
		}
	};

	Input::AudioPlayer::AudioPlayer() : m_Impl(std::make_unique<Impl>()) {}

	Input::AudioPlayer::~AudioPlayer()
	{
		Close();
	}

	bool Input::AudioPlayer::Open(const std::filesystem::path &filePath)
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		m_Impl->CloseLocked();
		m_Impl->ClearError();
		m_Impl->FilePath = filePath;
		if (filePath.empty())
		{
			m_Impl->FilePath.clear();
			return m_Impl->SetError("Audio file path is empty");
		}

		if (!m_Impl->OpenDecoder())
		{
			m_Impl->Decoder.reset();
			m_Impl->FilePath.clear();
			return false;
		}

		const uint64_t queueFrames = static_cast<uint64_t>(m_Impl->Format.SampleRate) * 2;
		if (queueFrames > std::numeric_limits<size_t>::max() / m_Impl->Format.Channels)
		{
			m_Impl->CloseLocked();
			return m_Impl->SetError("Audio sample queue is too large");
		}
		m_Impl->Samples.resize(
			static_cast<size_t>(queueFrames * m_Impl->Format.Channels));

		m_Impl->Device = Input::AudioOutputDeviceFactory::CreateDefault();
		if (!m_Impl->Device)
		{
			m_Impl->CloseLocked();
			return m_Impl->SetError("No audio output backend is available on this platform");
		}
		if (!m_Impl->Device->Open(
				m_Impl->Format,
				[this](float *output, uint32_t frames, const Input::AudioFormat &format)
				{
					m_Impl->Render(output, frames, format);
				}))
		{
			const std::string error = m_Impl->Device->GetErrorMessage();
			m_Impl->CloseLocked();
			return m_Impl->SetError(error.empty() ? "Failed to open the audio output device" : error);
		}

		if (!m_Impl->StartDecodeThread())
		{
			const std::string error = GetErrorMessage();
			m_Impl->CloseLocked();
			return m_Impl->SetError(error);
		}
		return true;
	}

	bool Input::AudioPlayer::Play()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (!m_Impl->Device || !m_Impl->Device->IsOpen())
			return m_Impl->SetError("No audio file is open");
		if (m_Impl->Failed.load(std::memory_order_acquire))
			return m_Impl->SetError("Audio decoding failed: " + GetErrorMessage());
		if (m_Impl->EndOfStream.load(std::memory_order_acquire))
			return m_Impl->SetError("Audio has ended; call Stop before playing it again");
		if (m_Impl->Playing.exchange(true, std::memory_order_acq_rel))
			return true;
		if (!m_Impl->Device->Start())
		{
			m_Impl->Playing.store(false, std::memory_order_release);
			return m_Impl->SetError(m_Impl->Device->GetErrorMessage());
		}
		return true;
	}

	void Input::AudioPlayer::Pause()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		m_Impl->Playing.store(false, std::memory_order_release);
		if (m_Impl->Device)
			m_Impl->Device->Stop();
	}

	bool Input::AudioPlayer::Stop()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		if (m_Impl->FilePath.empty() || !m_Impl->Decoder)
			return m_Impl->SetError("No audio file is open");

		m_Impl->Playing.store(false, std::memory_order_release);
		if (m_Impl->Device)
			m_Impl->Device->Stop();
		m_Impl->StopDecodeThread();
		m_Impl->ReadPosition.store(0, std::memory_order_relaxed);
		m_Impl->WritePosition.store(0, std::memory_order_relaxed);
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
		return true;
	}

	void Input::AudioPlayer::Close()
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		m_Impl->CloseLocked();
		m_Impl->ClearError();
	}

	bool Input::AudioPlayer::IsOpen() const
	{
		std::lock_guard lifecycleLock(m_Impl->LifecycleMutex);
		return m_Impl->Device && m_Impl->Device->IsOpen();
	}

	bool Input::AudioPlayer::IsPlaying() const
	{
		return m_Impl->Playing.load(std::memory_order_acquire);
	}

	std::string Input::AudioPlayer::GetErrorMessage() const
	{
		std::lock_guard lock(m_Impl->ErrorMutex);
		return m_Impl->Error;
	}
}
