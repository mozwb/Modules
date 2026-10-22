#pragma once
#ifdef XY_DEBUG
#include "Log/Log.h"
#include "DataStoreDevice.h"
namespace X_Y
{
	//仅供DEBUG时测试用
	template <typename T>
	using LogBase = LogConfigure::LogConfigure<T>;
	using LogConfigure::DEVICE;
	using LogConfigure::LFile;
	// 建议一个类当作一个对象，否则可能模板会冲突
	struct LOG : public LogBase<LOG>
	{
		using LogBase<LOG>::LogBase;
		LOG_LEVEL_EXT(Pink)
		LOG_LEVEL_API(Pink)
		LOG(str name) : LogBase<LOG>(name)
		{
			Pink::setModel("%255:105:180%[位置][日期][时间][发起者][等级]:[内容]%#");
			this->add(new DEVICE());
			this->add(new DataStoreDevice()); // 追加到 DataStore，key=当天日期.log
		}
		static LOG &Instance()
		{
			static LOG logger("log");
			return logger;
		}
	};
}
// inline X_Y::LOG logger("log");
#define logger X_Y::LOG::Instance()
#define LOG(Logger, LEVEL, ...) \
	Logger.Log<std::remove_reference_t<decltype(logger)>::LEVEL>(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);
#define XINFO(...) \
	LOG(logger, Info, __VA_ARGS__)
#define XTRACE(...) \
	LOG(logger, Trace, __VA_ARGS__)
#define XWARN(...) \
	LOG(logger, Warn, __VA_ARGS__)
#define XDEBUG(...) \
	LOG(logger, Debug, __VA_ARGS__)
#define XERROR(...) \
	LOG(logger, Error, __VA_ARGS__)
#define XFATAL(...) \
	LOG(logger, Fatal, __VA_ARGS__)
#define XPINK(...) \
	LOG(logger, Pink, __VA_ARGS__)

#if defined(XY_PLATFORM_WINDOWS)
#define XY_DEBUGBREAK() __debugbreak();
#elif defined(XY_PLATFORM_LINUX)
#define XY_DEBUGBREAK() __builtin_trap()
#elif defined(XY_PLATFORM_MAC)
#define XY_DEBUGBREAK() __builtin_trap()
#else
#define XY_DEBUGBREAK()
#endif

#define XY_PP_GET_MACRO(_1, _2, _3, _4, NAME, ...) NAME

#define XY_CORE_ASSERT_1(condition)   \
	do                                \
	{                                 \
		if (!(condition))             \
		{                             \
			XFATAL("[ASSERT FAILED]") \
			XY_DEBUGBREAK()           \
		}                             \
	} while (0)

#define XY_CORE_ASSERT_2(condition, ...)               \
	do                                                 \
	{                                                  \
		if (!(condition))                              \
		{                                              \
			XFATAL("[ASSERT FAILED]: {}", __VA_ARGS__) \
			XY_DEBUGBREAK()                            \
		}                                              \
	} while (0)

// 支持最多 4 参数(condition + 消息 + 插值…)：恰好 1 参数走 _1(仅条件)，
// 其余(消息可带任意插值)都落到 _2 的变参版。
#define XY_CORE_ASSERT(...) XY_PP_GET_MACRO(__VA_ARGS__, XY_CORE_ASSERT_2, XY_CORE_ASSERT_2, XY_CORE_ASSERT_2, XY_CORE_ASSERT_1)(__VA_ARGS__)

#define XY_CORE_OUT_1(condition)      \
	do                                \
	{                                 \
		if (!(condition))             \
		{                             \
			XDEBUG("[ASSERT FAILED]") \
		}                             \
	} while (0)

#define XY_CORE_OUT_2(condition, ...)                  \
	do                                                 \
	{                                                  \
		if (!(condition))                              \
		{                                              \
			XDEBUG("[ASSERT FAILED]: {}", __VA_ARGS__) \
		}                                              \
	} while (0)

#define XY_CORE_OUT(...) XY_PP_GET_MACRO(__VA_ARGS__, XY_CORE_OUT_2, XY_CORE_OUT_2, XY_CORE_OUT_2, XY_CORE_OUT_1)(__VA_ARGS__)

#else
#define LOG(logger, LEVEL, ...)
#define XINFO(...)
#define XTRACE(...)
#define XDEBUG(...)
#define XERROR(...)
#define XFATAL(...)
#define XWARN(...)
#define XY_CORE_ASSERT(condition, ...)
#define XY_DEBUGBREAK()
#define XY_PROFILE_FUNCTION
#endif
