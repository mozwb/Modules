#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace X_Y
{
	class Input
	{
	public:
		using KeyCode = unsigned int;
		using MouseCode = unsigned int;

		enum class EatMode : unsigned char
		{
			Pass,
			Block,
			Capture
		};
		struct MousePosition
		{
			float x, y;
		};

		struct AudioFormat
		{
			uint32_t SampleRate = 48000;
			uint32_t Channels = 2;
		};

		// Samples are interleaved IEEE float32 in the normalized range [-1, 1].
		// frameCount counts samples per channel, not individual float values.
		// The device clears the output to silence before calling this function.
		// It runs on the audio worker thread: keep it short and do not block or allocate.
		using RenderCallback = std::function<void(
			float *interleavedSamples,
			uint32_t frameCount,
			const AudioFormat &format)>;

		class AudioOutputDevice
		{
		public:
			virtual ~AudioOutputDevice() = default;

			virtual bool Open(const AudioFormat &format, RenderCallback callback) = 0;
			virtual bool Start() = 0;
			virtual void Stop() = 0;
			virtual void Close() = 0;
			virtual bool IsOpen() const = 0;
			virtual AudioFormat GetFormat() const = 0;
			virtual std::string GetErrorMessage() const = 0;
		};

		class AudioOutputDeviceFactory
		{
		public:
			static std::unique_ptr<AudioOutputDevice> CreateDefault();
		};

		class AudioPlayer
		{
		public:
			AudioPlayer();
			~AudioPlayer();

			AudioPlayer(const AudioPlayer &) = delete;
			AudioPlayer &operator=(const AudioPlayer &) = delete;

			bool Open(const std::filesystem::path &filePath);
			bool Play();
			void Pause();
			bool Stop();
			void Close();

			bool IsOpen() const;
			bool IsPlaying() const;
			std::string GetErrorMessage() const;

		private:
			struct Impl;
			std::unique_ptr<Impl> m_Impl;
		};

		// pressed适合Ui,窗口行为，否则建议使用down的相关函数
		// 检测按键/鼠标按下状态。返回 true 表示当前帧按下，false 表示未按下。
		// 注意：IsKeyPressed/IsMouseButtonPressed 走 KeyMapper(供 UI/窗口事件)，
		// 与 IsKeyDown/IsMouseDown 走硬件轮询并存，各司其职。
		static bool IsKeyPressed(KeyCode key);
		static bool IsMouseButtonPressed(MouseCode button);
			// 返回当前按下的键码/鼠标按键码,如果没有按下返回0
			// 多个按键同时按下时,返回第一个或者键码值小的，反正只能获取一个
			static KeyCode GetKeyPressed();
			static MouseCode GetMouseButtonPressed();

			// 获取鼠标位置。返回的是全局屏幕坐标，非窗口客户区坐标。
			// 需要窗口客户区坐标时，去 BaseWin 找 ScreenToClient / GetMouseScreenPos 等接口。
		static MousePosition GetMousePosition();
			static float GetMouseX();
			static float GetMouseY();

			// 设置鼠标位置。参数为全局屏幕坐标，直接移动真实系统光标。
			// 需要窗口客户区坐标时，去 BaseWin 找 ScreenToClient / GetMouseScreenPos 等接口。
			static void SetMousePosition(float x, float y);

			// ── 设备层(全局)能力 ──────────────────────
			// 隐藏/显示系统光标。true=显示，false=隐藏。隐藏时是全局隐藏(对整系统生效)，
			// 可用于"自绘光标"场景(如 MouseFlight 用自绘箭头替代系统光标)。
			// ⚠️ 注意：这会隐藏整个系统的光标，关闭程序前记得 SetCursorVisible(true) 还原。
			static void SetCursorVisible(bool visible);

			// 主显示器尺寸(物理像素)。封装 GetSystemMetrics，作为设备层能力供上层使用。
			static int GetScreenWidth();
			static int GetScreenHeight();

			// ── 设备层：硬件直读轮询(不依赖窗口消息泵) ──
			// 适合游戏/高频/全局输入(如 MouseFlight)，实时、无焦点限制。
			// 注：现有 IsKeyPressed/IsMouseButtonPressed 走 KeyMapper(供 UI/窗口事件)，
			//     与这里的硬件轮询并存，各司其职。
			static bool IsKeyDown(KeyCode key);		   // GetAsyncKeyState 直读
			static bool IsMouseDown(MouseCode button); // GetAsyncKeyState 直读
			static KeyCode GetKeyDown();			   // GetAsyncKeyState 直读
			static MouseCode GetMouseDown();		   // GetAsyncKeyState 直读
			// 平台键 → 内部键（接收 uint32_t 类型键码）
		static KeyCode Translate(uint32_t platformKey);

			// 内部键 → 平台键
		static uint32_t TranslateKey(KeyCode key);

			// 平台鼠标键 → 内部鼠标键
		static MouseCode TranslateMouse(uint32_t platformButton);

			// 内部鼠标键 → 平台键
		static uint32_t TranslateMouseKey(MouseCode button);

			// 给Input添加模拟输入的方法,直接输入到系统输入流,不依赖窗口焦点,可用于全局输入
			/**
			 * @brief 模拟输入一段文本（走 SendInput KEYEVENTF_UNICODE，全局输入流）
			 * @param wstr 要输入的UTF‑16宽字符串，nullptr终止
			 * @param charIntervalMs 字符之间的等待时间（毫秒），0表示不等待
			 */
			static bool SimulateTypeText(const wchar_t *wstr, uint32_t charIntervalMs = 0);
			static bool SimulateTypeText(const char *utf8Str, uint32_t charIntervalMs = 0);
			static bool SimulateTypeText(const std::string &utf8Str, uint32_t charIntervalMs = 0);
			/**
			 * @brief 模拟物理按键（VK虚拟键路径，SendInput）
			 * @param key 内部GLFW风格KeyCode
			 * @param pressed true=按下，false=松开
			 * @note 内部会通过KeyMapper翻译成平台VK；映射失败直接return无动作
			 */
			static bool SimulateKey(KeyCode key, bool pressed);

			/**
			 * @brief 模拟鼠标按键按下/释放
			 * @param button 内部MouseCode（Left / Right / Middle）
			 * @param pressed true按下；false松开
			 */
			static bool SimulateMouse(MouseCode button, bool pressed);

			/**
			 * @brief 模拟鼠标滚轮
			 * @param delta Windows标准滚轮单位：一个齿是 WHEEL_DELTA(120)；正数向上滚，负数向下滚
			 */
			static bool SimulateMouseWheel(float delta);

			// 全局吞掉指定的真实输入，使其他窗口/页面收不到该按键。
			// Pass=本程序通过查询接口/队列接收且放行给其他窗口，
			// Block=本程序仍可通过原始查询接口接收，但拦截其他窗口，
			// Capture=本程序通过 TryGetEat* 接收且拦截其他窗口。
			static bool EatKey(KeyCode key, EatMode mode = EatMode::Block);
			static bool EatMouse(MouseCode button, EatMode mode = EatMode::Block);
			// 兼容旧接口：true=Block，false=Pass。
			static bool EatKey(KeyCode key, bool enabled);
			static bool EatMouse(MouseCode button, bool enabled);
			// 取出一个 Capture 模式捕获的事件，返回 false 表示队列为空。
			static bool TryGetEatKey(KeyCode &key, bool &pressed);
			static bool TryGetEatMouse(MouseCode &button, bool &pressed);
			static void ClearEatKeyQueue();
			static void ClearEatMouseQueue();
			static void ResetEatState();
			static void StopHooks();

		struct Key
		{
			enum : KeyCode
			{
				// From glfw3.h
				Space = 32,
				Apostrophe = 39, /* ' */
				Comma = 44,		 /* , */
				Minus = 45,		 /* - */
				Period = 46,	 /* . */
				Slash = 47,		 /* / */

				D0 = 48, /* 0 */
				D1 = 49, /* 1 */
				D2 = 50, /* 2 */
				D3 = 51, /* 3 */
				D4 = 52, /* 4 */
				D5 = 53, /* 5 */
				D6 = 54, /* 6 */
				D7 = 55, /* 7 */
				D8 = 56, /* 8 */
				D9 = 57, /* 9 */

				Semicolon = 59, /* ; */
				Equal = 61,		/* = */

				A = 65,
				B = 66,
				C = 67,
				D = 68,
				E = 69,
				F = 70,
				G = 71,
				H = 72,
				I = 73,
				J = 74,
				K = 75,
				L = 76,
				M = 77,
				N = 78,
				O = 79,
				P = 80,
				Q = 81,
				R = 82,
				S = 83,
				T = 84,
				U = 85,
				V = 86,
				W = 87,
				X = 88,
				Y = 89,
				Z = 90,

				LeftBracket = 91,  /* [ */
				Backslash = 92,	   /* \ */
				RightBracket = 93, /* ] */
				GraveAccent = 96,  /* ` */

				World1 = 161, /* non-US #1 */
				World2 = 162, /* non-US #2 */

				/* Function keys */
				Escape = 256,
				Enter = 257,
				Tab = 258,
				Backspace = 259,
				Insert = 260,
				Delete = 261,
				Right = 262,
				Left = 263,
				Down = 264,
				Up = 265,
				PageUp = 266,
				PageDown = 267,
				Home = 268,
				End = 269,
				CapsLock = 280,
				ScrollLock = 281,
				NumLock = 282,
				PrintScreen = 283,
				Pause = 284,
				F1 = 290,
				F2 = 291,
				F3 = 292,
				F4 = 293,
				F5 = 294,
				F6 = 295,
				F7 = 296,
				F8 = 297,
				F9 = 298,
				F10 = 299,
				F11 = 300,
				F12 = 301,
				F13 = 302,
				F14 = 303,
				F15 = 304,
				F16 = 305,
				F17 = 306,
				F18 = 307,
				F19 = 308,
				F20 = 309,
				F21 = 310,
				F22 = 311,
				F23 = 312,
				F24 = 313,
				F25 = 314,

				/* Keypad */
				KP0 = 320,
				KP1 = 321,
				KP2 = 322,
				KP3 = 323,
				KP4 = 324,
				KP5 = 325,
				KP6 = 326,
				KP7 = 327,
				KP8 = 328,
				KP9 = 329,
				KPDecimal = 330,
				KPDivide = 331,
				KPMultiply = 332,
				KPSubtract = 333,
				KPAdd = 334,
				KPEnter = 335,
				KPEqual = 336,

				LeftShift = 340,
				LeftControl = 341,
				LeftAlt = 342,
				LeftSuper = 343,
				RightShift = 344,
				RightControl = 345,
				RightAlt = 346,
				RightSuper = 347,
				Menu = 348
			};
		};

		struct Mouse
		{
			enum : MouseCode
			{
				// From glfw3.h
				Button0 = 0,
				Button1 = 1,
				Button2 = 2,
				Button3 = 3,
				Button4 = 4,
				Button5 = 5,
				Button6 = 6,
				Button7 = 7,

				ButtonLast = Button7,
				ButtonLeft = Button0,
				ButtonRight = Button1,
				ButtonMiddle = Button2
			};
		};
	};
}