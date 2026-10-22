#include "Input/Audio/AudioOutputDevice.h"

#ifdef XY_PLATFORM_WINDOWS

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iterator>
#include <mutex>
#include <thread>
#include <system_error>
#include <utility>

namespace X_Y
{
	namespace
	{
		constexpr REFERENCE_TIME bufferDuration = 1000000;

		std::string HResultMessage(const char *operation, HRESULT result)
		{
			return std::string(operation) + " failed (HRESULT " +
				   std::to_string(static_cast<unsigned long>(result)) + ")";
		}

		class WasapiOutputDevice final : public Input::AudioOutputDevice
		{
		public:
			~WasapiOutputDevice() override
			{
				Close();
			}

			bool Open(const Input::AudioFormat &format, Input::RenderCallback callback) override
			{
				std::lock_guard lifecycleLock(m_LifecycleMutex);
				CloseInternal();
				if (format.SampleRate == 0 || format.Channels == 0 ||
					format.Channels > 2 ||
					format.SampleRate > UINT32_MAX / (format.Channels * sizeof(float)) ||
					!callback)
					return SetError("WASAPI output currently requires mono/stereo float32 PCM and a valid callback");

				m_Format = format;
				m_Callback = std::move(callback);
				m_StartupComplete = false;
				m_StartupSucceeded = false;
				m_Shutdown.store(false);
				m_Playing.store(false);
				m_Error.clear();

				m_ControlEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				m_AudioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (!m_ControlEvent || !m_AudioEvent)
				{
					SetError(HResultMessage("CreateEventW", HRESULT_FROM_WIN32(GetLastError())));
					CloseHandles();
					return false;
				}

				try
				{
					m_Thread = std::thread(&WasapiOutputDevice::ThreadMain, this);
				}
				catch (const std::system_error &error)
				{
					SetError(std::string("Failed to start WASAPI worker thread: ") + error.what());
					CloseHandles();
					return false;
				}
				{
					std::unique_lock lock(m_Mutex);
					m_StartupCondition.wait(lock, [this] { return m_StartupComplete; });
					if (!m_StartupSucceeded)
					{
						lock.unlock();
						if (m_Thread.joinable())
							m_Thread.join();
						CloseHandles();
						return false;
					}
				}
				m_Open.store(true);
				return true;
			}

			bool Start() override
			{
				std::lock_guard lifecycleLock(m_LifecycleMutex);
				if (!m_Open.load())
					return SetError("Audio output device is not open");
				m_Playing.store(true);
				SetEvent(m_ControlEvent);
				return true;
			}

			void Stop() override
			{
				std::lock_guard lifecycleLock(m_LifecycleMutex);
				if (!m_Open.load())
					return;
				m_Playing.store(false);
				SetEvent(m_ControlEvent);
			}

			void Close() override
			{
				std::lock_guard lifecycleLock(m_LifecycleMutex);
				CloseInternal();
			}

			bool IsOpen() const override
			{
				return m_Open.load();
			}

			Input::AudioFormat GetFormat() const override
			{
				return m_Format;
			}

			std::string GetErrorMessage() const override
			{
				std::lock_guard lock(m_Mutex);
				return m_Error;
			}

		private:
			void CloseInternal()
			{
				if (m_Thread.joinable())
				{
					m_Shutdown.store(true);
					if (m_ControlEvent)
						SetEvent(m_ControlEvent);
					m_Thread.join();
				}
				m_Open.store(false);
				m_Playing.store(false);
				m_Callback = {};
				CloseHandles();
			}
			bool SetError(std::string message)
			{
				std::lock_guard lock(m_Mutex);
				m_Error = std::move(message);
				return false;
			}

			void CloseHandles()
			{
				if (m_AudioEvent)
				{
					CloseHandle(m_AudioEvent);
					m_AudioEvent = nullptr;
				}
				if (m_ControlEvent)
				{
					CloseHandle(m_ControlEvent);
					m_ControlEvent = nullptr;
				}
			}

			void SignalStartup(bool succeeded, std::string error = {})
			{
				{
					std::lock_guard lock(m_Mutex);
					m_StartupSucceeded = succeeded;
					m_StartupComplete = true;
					if (!error.empty())
						m_Error = std::move(error);
				}
				m_StartupCondition.notify_one();
			}

			void ThreadMain()
			{
				const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				if (FAILED(comResult))
				{
					SignalStartup(false, HResultMessage("CoInitializeEx", comResult));
					return;
				}

				struct ComApartment
				{
					~ComApartment() { CoUninitialize(); }
				} apartment;

				HRESULT result = S_OK;
				Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
				Microsoft::WRL::ComPtr<IMMDevice> device;
				Microsoft::WRL::ComPtr<IAudioClient> audioClient;
				Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
				WAVEFORMATEX format{};
				format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
				format.nChannels = static_cast<WORD>(m_Format.Channels);
				format.nSamplesPerSec = m_Format.SampleRate;
				format.wBitsPerSample = 32;
				format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(float));
				format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

				result = CoCreateInstance(
					__uuidof(MMDeviceEnumerator),
					nullptr,
					CLSCTX_ALL,
					__uuidof(IMMDeviceEnumerator),
					reinterpret_cast<void **>(enumerator.GetAddressOf()));
				if (SUCCEEDED(result))
					result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
				if (SUCCEEDED(result))
					result = device->Activate(
						__uuidof(IAudioClient),
						CLSCTX_ALL,
						nullptr,
						reinterpret_cast<void **>(audioClient.GetAddressOf()));
				if (SUCCEEDED(result))
				{
					result = audioClient->Initialize(
						AUDCLNT_SHAREMODE_SHARED,
						AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
							AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
							AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
						bufferDuration,
						0,
						&format,
						nullptr);
				}
				if (SUCCEEDED(result))
					result = audioClient->SetEventHandle(m_AudioEvent);
				if (SUCCEEDED(result))
					result = audioClient->GetService(
						__uuidof(IAudioRenderClient),
						reinterpret_cast<void **>(renderClient.GetAddressOf()));
				if (FAILED(result))
				{
					SignalStartup(false, HResultMessage("Initialize WASAPI output", result));
					return;
				}

				UINT32 bufferFrames = 0;
				result = audioClient->GetBufferSize(&bufferFrames);
				if (FAILED(result) || bufferFrames == 0)
				{
					SignalStartup(false, FAILED(result)
						? HResultMessage("IAudioClient::GetBufferSize", result)
						: "WASAPI returned an empty audio buffer");
					return;
				}

				SignalStartup(true);
				const HANDLE events[] = {m_ControlEvent, m_AudioEvent};
				bool clientStarted = false;
				while (!m_Shutdown.load())
				{
					const DWORD waitResult = WaitForMultipleObjects(
						static_cast<DWORD>(std::size(events)),
						events,
						FALSE,
						INFINITE);
					if (waitResult == WAIT_OBJECT_0)
					{
						if (m_Shutdown.load())
							break;
						if (m_Playing.load() && !clientStarted)
						{
							result = audioClient->Start();
							if (FAILED(result))
							{
								SetError(HResultMessage("IAudioClient::Start", result));
								m_Playing.store(false);
							}
							else
							{
								clientStarted = true;
							}
						}
						else if (!m_Playing.load() && clientStarted)
						{
							result = audioClient->Stop();
							if (FAILED(result))
								SetError(HResultMessage("IAudioClient::Stop", result));
							clientStarted = false;
						}
						continue;
					}

					if (waitResult != WAIT_OBJECT_0 + 1 || !clientStarted)
					{
						if (waitResult == WAIT_FAILED)
							SetError(HResultMessage("WaitForMultipleObjects", HRESULT_FROM_WIN32(GetLastError())));
						continue;
					}

					UINT32 padding = 0;
					result = audioClient->GetCurrentPadding(&padding);
					if (FAILED(result) || padding > bufferFrames)
					{
						SetError(FAILED(result)
							? HResultMessage("IAudioClient::GetCurrentPadding", result)
							: "WASAPI reported invalid render buffer padding");
						m_Playing.store(false);
						audioClient->Stop();
						clientStarted = false;
						continue;
					}

					const UINT32 framesAvailable = bufferFrames - padding;
					if (framesAvailable == 0)
						continue;

					BYTE *buffer = nullptr;
					result = renderClient->GetBuffer(framesAvailable, &buffer);
					if (FAILED(result))
					{
						SetError(HResultMessage("IAudioRenderClient::GetBuffer", result));
						continue;
					}

					const size_t sampleCount =
						static_cast<size_t>(framesAvailable) * m_Format.Channels;
					auto *samples = reinterpret_cast<float *>(buffer);
					std::memset(samples, 0, sampleCount * sizeof(float));
					try
					{
						m_Callback(samples, framesAvailable, m_Format);
					}
					catch (const std::exception &error)
					{
						SetError(std::string("Audio render callback failed: ") + error.what());
					}
					catch (...)
					{
						SetError("Audio render callback failed with an unknown exception");
					}

					result = renderClient->ReleaseBuffer(framesAvailable, 0);
					if (FAILED(result))
					{
						SetError(HResultMessage("IAudioRenderClient::ReleaseBuffer", result));
						m_Playing.store(false);
						audioClient->Stop();
						clientStarted = false;
					}
				}

				if (clientStarted)
					audioClient->Stop();
			}

			Input::AudioFormat m_Format;
			Input::RenderCallback m_Callback;
			HANDLE m_ControlEvent = nullptr;
			HANDLE m_AudioEvent = nullptr;
			std::thread m_Thread;
			std::atomic<bool> m_Open{false};
			std::atomic<bool> m_Playing{false};
			std::atomic<bool> m_Shutdown{false};
			mutable std::mutex m_LifecycleMutex;
			mutable std::mutex m_Mutex;
			std::condition_variable m_StartupCondition;
			bool m_StartupComplete = false;
			bool m_StartupSucceeded = false;
			std::string m_Error;
		};
	}

	std::unique_ptr<Input::AudioOutputDevice> CreateWasapiOutputDevice()
	{
		return std::make_unique<WasapiOutputDevice>();
	}
}

#endif
