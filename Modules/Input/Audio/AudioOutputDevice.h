#pragma once

// 音频输出后端的公开入口。
// 具体接口（AudioFormat / AudioOutputDevice / AudioOutputDeviceFactory）
// 统一定义在 Input/Input.h，这里只做子路径转发，保持
// #include "Input/Audio/AudioOutputDevice.h" 的旧写法可用。
#include "Input/Input.h"
