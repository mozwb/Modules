#include "Input/Audio/AudioOutputDevice.h"

#ifdef XY_PLATFORM_WINDOWS
namespace X_Y
{
	std::unique_ptr<Input::AudioOutputDevice> CreateWasapiOutputDevice();
}
#endif

namespace X_Y
{
	std::unique_ptr<Input::AudioOutputDevice> Input::AudioOutputDeviceFactory::CreateDefault()
	{
#ifdef XY_PLATFORM_WINDOWS
		return CreateWasapiOutputDevice();
#else
		return nullptr;
#endif
	}
}
