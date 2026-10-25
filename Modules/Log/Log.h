#pragma once

/*
Log.h
version 0.0.0

日志内核。只有机制，没有策略：
不认识任何具体设备、不持有全局实例、不定义使用端宏。

────────────────────────────────────────────────────────────────
内核提供什么
  LogTools::*          占位符替换（{} / {{}}）与颜色转义
  DEVICE               设备抽象：一个 Log(std::string) + toString()
  LOG_LEVEL_EXT/API    声明日志等级的宏（每个 logger 自己声明要哪些等级）
  LogConfigure<T>      CRTP 基类：等级表、model 字符串、设备列表、Log<> 入口
  ProfileScope         作用域计时 RAII（XY_PROFILE_FUNCTION）

内核不提供什么（属于使用端，引擎侧自己写）
  ✗ 具体设备（控制台 / 文件 / DataStore）
  ✗ 全局 logger 实例
  ✗ XINFO / XWARN / XDEBUG / XERROR / XFATAL / XPINK 这类宏
  ✗ 断言宏

理由：同一个内核可以实例化出多个 logger（引擎长期日志、调试日志），
	  它们只是"设备组合不同、生命周期不同"，机制完全一样。

────────────────────────────────────────────────────────────────
性能分析的输出：钩子

  ProfileScope 不直接打印，而是把消息交给一个可安装的钩子：

	  using TraceSink = void (*)(const char *message);
	  void SetTraceSink(TraceSink sink);     // nullptr = 关闭（默认）

  为什么不给 XTRACE 宏留"内核空实现 + 使用端 #define 覆盖"的口子：
  那个做法要求使用端【必须在 include 之前】定义宏，只要有任何一个头
  先带进了 Log.h，内核的空宏就已经生效，使用端的定义彻底失效，
  而且不报错 —— 是隐雷。钩子是运行期的，顺序无关。

  引擎侧一行接上即可：
	  X_Y::SetTraceSink([](const char *msg) { XTRACE("{}", msg); });
  （无捕获 lambda 直接退化成函数指针，不碰堆。）

  没装钩子 = 连字符串都不拼，零开销。

────────────────────────────────────────────────────────────────
用法（使用端）

	#include <Log/Log.h>

	struct EngineLog : X_Y::LogConfigure::LogConfigure<EngineLog>
	{
		LOG_LEVEL_EXT(Engine)
		LOG_LEVEL_API(Engine)
		EngineLog() : LogBase<EngineLog>("engine")
		{
			// this->add(new ConsoleDevice());
		}
	};

	// 把性能分析接到日志上
	X_Y::SetTraceSink([](const char *msg) { EngineLog::Instance().Info("{}", msg); });

	void Foo() { XY_PROFILE_FUNCTION(); }
────────────────────────────────────────────────────────────────
*/

#include <cstdio>
#include <fstream>
#include <iostream>
#include <ostream>
#include <regex>
#include <string>
#include <utility>

#include <Core/Core.h>
#include <Timer/Timer.h>

// namespace X_Y
// {

//     // ── 性能分析的输出钩子 ────────────────────────────────────────────────
//     // 见文件头"性能分析的输出：钩子"。默认未安装 = 不输出。
//     using TraceSink = void (*)(const char *message);

//     void SetTraceSink(TraceSink sink);
//     TraceSink GetTraceSink();

//     // ── ProfileScope：作用域计时 ─────────────────────────────────────────────
//     // 析构时把 "[函数名] 耗时 x.xx ms | 附加信息" 交给已安装的钩子。
//     // 没装钩子就什么都不做（连字符串都不拼）。
//     struct ProfileScope
//     {
//         StopWatch timer;
//         std::string funcName;
//         std::string extraInfo;

//         // 附加信息直接用参数给出，例如：
//         //   XY_PROFILE_FUNCTION();
//         //   XY_PROFILE_FUNCTION("n=", n);          // 任意个，自动 To_Str
//         //
//         // ⚠ 原来的 << 链式写法从来没能编译过（宏末尾带分号，`decl << x` 不合法），
//         //   这里改成变参构造，两种需求一次满足，也不会有"语句无效果"的告警。
//         template <typename... Args>
//         ProfileScope(const char *func, Args &&...args) : funcName(func)
//         {
//             ((extraInfo += (extraInfo.empty() ? "" : " | "), extraInfo += To_Str(args)), ...);
//         }

//         ~ProfileScope()
//         {
//             TraceSink sink = GetTraceSink();
//             if (!sink)
//                 return;

//             char ms[32];
//             std::snprintf(ms, sizeof(ms), "%.2f", timer.Milliseconds());

//             std::string msg = "[" + funcName + "] 耗时 " + ms + "ms";
//             if (!extraInfo.empty())
//                 msg += " | " + extraInfo;

//             sink(msg.c_str());
//         }
//     };

// } // namespace X_Y

// @@ 宏辅助：拼接 token
#define XY_PASTE2(a, b) a##b
#define XY_PASTE(a, b) XY_PASTE2(a, b)

// #ifdef ENABLEPROFILE
// // @@ 创建 ProfileScope 变量，变量名 = prof_ + __LINE__，防止嵌套冲突
// //    XY_PROFILE_FUNCTION();            // 只记耗时
// //    XY_PROFILE_FUNCTION("n=", n);     // 追加任意个信息（自动 To_Str）
// #define XY_PROFILE_FUNCTION(...) \
//     X_Y::ProfileScope XY_PASTE(prof_, __LINE__)(__FUNCTION__ __VA_OPT__(, ) __VA_ARGS__)
// #else
// #define XY_PROFILE_FUNCTION(...)
// #endif

namespace X_Y
{

	// 这个工具类主要是给LogConfigure用的，外部不需要使用，字符串替换在LogConfigure里的format和pipei可以重写实现
	namespace LogTools
	{

		// 继承该类的函数，使用<<会自动使用To_Str调用ToString函数（如果有的话），否则输出NO_TO_STRING
		// 但是只能类内部使用，外部的<<是普通的输出
		struct CustomLogBase
		{
		};

		template <typename T>
		struct IsCustomLogBase : std::is_base_of<CustomLogBase, T>
		{
		};

		template <class T>
			requires IsCustomLogBase<T>::value // 【外部约束！最直观！】
		inline std::ostream &operator<<(std::ostream &os, const T &obj)
		{
			if constexpr (HasToString<T>::value || HasStaticToString<T>::value)
			{
				return os << To_Str<T>(obj);
			}
			else
			{
				return os << "NO_TO_STRING";
			}
		}

		template <typename T, typename... Args>
		inline static void replaceModel(std::string &content, T &&model, Args &&...args)
		{
			std::string mod = To_Str(std::forward<T>(model));
			// 逗号运算符你敢信，它能依次执行，结果是最后表达式结果
			(
				// lambda表达式
				[&](auto &&arg) // 参数
				{
					size_t l = content.find(mod);
					if (l != std::string::npos)
					{
						content.replace(l, mod.size(), To_Str(arg));
					}
				} // 函数体
				(std::forward<Args>(args)), // 调用
				...);						// 这里报错可能是编译器犯傻，但能编译运行
		};
		template <typename T, typename... Args>
		inline static void replaceModel(std::string &content, T &&le, T &&re, Args &&...args)
		{
			std::string left = To_Str(std::forward<T>(le));
			std::string right = To_Str(std::forward<T>(re));
			std::string cleft = left + left;	// {{
			std::string cright = right + right; // }}

			size_t pos = 0;

			// 正确遍历参数，一个一个替换
			([&](auto &&arg)
			 {
                while (pos < content.size())
                {
                    // 先找 {{
                    size_t check_double = content.find(cleft, pos);
                    size_t l = content.find(left, pos);

                    // 如果是 {{ ，跳过！
                    if (check_double != std::string::npos && check_double == l)
                    {
                        pos = l + 2;
                        continue;
                    }

                    // 找匹配的 }
                    size_t r = content.find(right, l);
                    if (l == std::string::npos || r == std::string::npos) break;

                    // 正常替换
                    content.replace(l, r - l + 1, To_Str(arg));
                    pos = r + 1;
                    break;
                } }(std::forward<Args>(args)), ...);
		}
		// 下面实现颜色替换
		//  \033[38;2;R;G;Bm 要显示的文字 \033[0m
		//\033[48; 2; 30; 30; 30m 改变背景\033[0m\n
		// 呜呜才发现能用正则，不过要注意转义，%和#都要转义成%%和##，否则正则会把它当成特殊字符
		inline static void processColorEscape(std::string &res)
		{
			res = std::regex_replace(res, std::regex(R"(%%)"), "%");
			res = std::regex_replace(res, std::regex(R"(##)"), "#");
		}
		inline static void setColorChar(std::string &res)
		{
			std::regex reg(R"((^|[^%])%(\d+):(\d+):(\d+)%)");
			std::smatch match;
			res = std::regex_replace(
				res,
				reg,
				"$1\x1B[38;2;$2;$3;$4m");
		}
		inline static void setColorBgd(std::string &res)
		{
			std::regex reg(R"((^|[^#])#(\d+):(\d+):(\d+)#)");
			std::smatch match;
			res = std::regex_replace(
				res,
				reg,
				"$1\x1B[48;2;$2;$3;$4m");
		}
		inline static void setColorEnd(std::string &res)
		{
			std::regex reg(R"((^|[^%])%#)");
			std::smatch match;
			res = std::regex_replace(
				res,
				reg,
				"$1\x1B[0m");
		}
		inline static void replaceColor(std::string &result)
		{
			setColorChar(result);
			setColorBgd(result);
			setColorEnd(result);
			processColorEscape(result);
		}
	}

}

// 配置里用到的类型
// #include <chrono>
// #include <ctime>
namespace X_Y
{

	// 这个里面是基础配置，该暴露在外面的都在外面了，拓展LEVEL用LOG_LEVEL_EXT,加LOG_LEVEL_API
	// 拓展设备直接继承DEVICE，重写toString和Log方法，添加成员变量（如果需要的话），然后add到LogConfigure里就行了
	namespace LogConfigure
	{
		using namespace LogTools;
		// struct TIME :public  CustomLogBase {
		//	static std::string now() {
		//		auto now = std::chrono::system_clock::now();
		//		std::time_t t = std::chrono::system_clock::to_time_t(now);
		//		char buf[32];
		//		std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
		//		return buf;
		//	}

		//	std::string toString() const {
		//		return now();
		//	}
		//};
		// struct DATE :public  CustomLogBase {
		//	static std::string now() {
		//		auto now = std::chrono::system_clock::now();
		//		std::time_t t = std::chrono::system_clock::to_time_t(now);
		//		char buf[32];
		//		std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
		//		return buf;
		//	}

		// 支持 toString（给你之前的自动替换用）
		//	std::string toString() const {
		//		return now(); // 直接返回当前时间
		//	}
		//};

		template <typename LEVEL_TYPE, typename SELF_TYPE>
		struct LEVEL : public CustomLogBase
		{
			LEVEL() = delete;
			static std::string &model()
			{
				static std::string m;
				return m;
			}
			static void setModel(std::string m) { model() = m; }
			static std::string getModel() { return model(); }
			static std::string toString() { return "LEVEL"; }
		};
		// template<typename LEVEL_TYPE, typename SELF_TYPE> std::string LEVEL<LEVEL_TYPE, SELF_TYPE>::model;
#define LOG_LEVEL_ALL                                     \
public:                                                   \
	struct Trace : LEVEL<Trace, Self>                     \
	{                                                     \
		static std::string toString() { return "TRACE"; } \
	};                                                    \
	struct Debug : LEVEL<Debug, Self>                     \
	{                                                     \
		static std::string toString() { return "DEBUG"; } \
	};                                                    \
	struct Info : LEVEL<Info, Self>                       \
	{                                                     \
		static std::string toString() { return "INFO"; }  \
	};                                                    \
	struct Warn : LEVEL<Warn, Self>                       \
	{                                                     \
		static std::string toString() { return "WARN"; }  \
	};                                                    \
	struct Error : LEVEL<Error, Self>                     \
	{                                                     \
		static std::string toString() { return "ERROR"; } \
	};                                                    \
	struct Fatal : LEVEL<Fatal, Self>                     \
	{                                                     \
		static std::string toString() { return "FATAL"; } \
	};

#define LOG_LEVEL_EXT(LevelName)                             \
	struct LevelName : LEVEL<LevelName, Self>                \
	{                                                        \
		static std::string toString() { return #LevelName; } \
	};

		struct DEVICE : public CustomLogBase
		{
			std::ostream *os = &std::cout;
			DEVICE() = default;
			DEVICE(std::ostream &os) : os(&os) {}
			// 或许拓展要写
			virtual ~DEVICE() = default;
			virtual std::string toString() const
			{
				return "DEVICE";
			};
			virtual void Log(const std::string &message) const
			{
				*os << message << std::endl;
			}
		};

		struct LFile : public DEVICE
		{
			// 成员变量
			std::string _filename;
			std::ofstream file;
			LFile(
				const std::string &filename,
				std::ios_base::openmode mode = std::ios::out)
				: DEVICE(),
				  _filename(filename),
				  file(filename, mode)
			{
				if (!file.is_open())
				{
					throw std::runtime_error("打开文件失败: " + filename);
				}
				os = &file;
			}
			std::string toString() const override { return file.is_open() ? _filename : "文件打开失败"; }
		};

		struct LogDevices : public CustomLogBase
		{
			std::vector<std::unique_ptr<DEVICE>> devices;

			// 1. 接收裸指针（别人new的）
			void add(DEVICE *dev)
			{
				if (dev)
					devices.emplace_back(dev);
			}

			// 2. 接收 unique_ptr（安全转移所有权）
			void add(std::unique_ptr<DEVICE> dev)
			{
				if (dev)
					devices.push_back(std::move(dev));
			}
			void clear()
			{
				devices.clear();
			}
			void remove(DEVICE *dev)
			{
				devices.erase(std::remove_if(devices.begin(), devices.end(),
											 [dev](const std::unique_ptr<DEVICE> &d)
											 { return d.get() == dev; }),
							  devices.end());
			}
			void logAll(const std::string &msg)
			{
				for (auto &dev : devices)
				{
					dev->Log(msg);
				}
			}
			std::string toString() const
			{
				std::string result;
				for (const auto &dev : devices)
				{
					result += dev->toString() + " ";
				}
				return result.empty() ? "No Devices" : result;
			}

			// 禁止外部拷贝管理器，防止乱生命周期
			LogDevices() = default;
			~LogDevices() = default;
			LogDevices(const LogDevices &) = delete;
			LogDevices &operator=(const LogDevices &) = delete;
		};

		// 单个等级的简易接口
#define LOG_LEVEL_API(LevelName)                             \
	template <typename... Args>                              \
	void log##LevelName(std::string content, Args &&...args) \
	{                                                        \
		this->template log<LevelName>(                       \
			std::forward<std::string>(content),              \
			std::forward<Args>(args)...);                    \
	}

// 批量生成所有等级的快捷接口 + 暴露两个通用模板接口
#define LOG_ALL_API              \
	/* 生成所有等级的快捷调用 */ \
	LOG_LEVEL_API(Trace)         \
	LOG_LEVEL_API(Debug)         \
	LOG_LEVEL_API(Info)          \
	LOG_LEVEL_API(Warn)          \
	LOG_LEVEL_API(Error)         \
	LOG_LEVEL_API(Fatal)
#define UN_KNOW "未知"
		template <typename T>
		class LogConfigure : public LogDevices
		{
		public:
			using str = std::string;
			template <typename V>
			using vec = std::vector<V>;
			using Self = T;
			using LogDevices::add;
			using LogDevices::clear;
			using LogDevices::logAll;
			using LogDevices::remove;
			using LogDevices::toString;
			template <typename A, typename B>
			using LEVEL = ::X_Y::LogConfigure::LEVEL<A, B>;

			LOG_LEVEL_ALL
			LOG_ALL_API

			LogConfigure()
			{
				name = "DefaultLogger";
				// this->add(new DEVICE());
				Trace::setModel("%180:180:180%[位置][日期][时间][发起者][等级]:[内容]%#");
				Debug::setModel("%90:180:255%[位置][日期][时间][发起者][等级]:[内容]%#");
				Info::setModel("%80:220:100%[位置][日期][时间][发起者][等级]:[内容]%#");
				Warn::setModel("%255:210:0%[位置][日期][时间][发起者][等级]:[内容]%#");
				Error::setModel("%255:80:80%[位置][日期][时间][发起者][等级]:[内容]%#");
				Fatal::setModel("%220:0:0%[位置][日期][时间][发起者][等级]:[内容]%#");
			}
			LogConfigure(str name)
				: name(name)
			{
				// this->add(new DEVICE());
				Trace::setModel("%180:180:180%[位置][日期][时间][发起者][等级]:[内容]%#");
				Debug::setModel("%90:180:255%[位置][日期][时间][发起者][等级]:[内容]%#");
				Info::setModel("%80:220:100%[位置][日期][时间][发起者][等级]:[内容]%#");
				Warn::setModel("%255:210:0%[位置][日期][时间][发起者][等级]:[内容]%#");
				Error::setModel("%255:80:80%[位置][日期][时间][发起者][等级]:[内容]%#");
				Fatal::setModel("%220:0:0%[位置][日期][时间][发起者][等级]:[内容]%#");
			}
			virtual ~LogConfigure() {};
			void setname(str name) { this->name = name; }
			std::string toString() const
			{
				std::ostringstream oss;
				oss << "日志发起者: " << name << "\n";
				oss << "链接输出设备:" << LogDevices::toString();
				return oss.str();
			}
			template <typename LevelType, typename U, typename... Args>
			void log(U &&content, Args &&...args)
			{
				// 内部直接调用私有版本，传默认值
				Log<LevelType>(nullptr, -1, nullptr,
							   std::forward<U>(content),
							   std::forward<Args>(args)...);
			}
			template <typename LevelType, typename U, typename... Args>
			void Log(const char *file, int line, const char *func, U &&content, Args &&...args)
			{
				str lev = LevelType::toString();
				str model = LevelType::model();
				str result = replace(content, args...);
				str Mfile = file == nullptr ? UN_KNOW : To_Str(file);
				str MLine = line == -1 ? UN_KNOW : To_Str(line);
				str Mfunc = func == nullptr ? UN_KNOW : To_Str(func);

				result = format(model, lev, result, Mfile, MLine, Mfunc);
				logAll(result);
			}

		private:
			template <typename U, typename... Args>
			str replace(U &&content, Args &&...args)
			{
				std::string s = To_Str(std::forward<U>(content));
				vec<str> args_vec;
				// 逗号运算符你敢信，它能依次执行，结果是最后表达式结果
				(args_vec.push_back(To_Str(std::forward<Args>(args))), ...);
				s = PiPei(s, args_vec);
				return s;
			}
			// 如果你不喜欢这个格式化方式，可以重写这个函数，匹配你想要的模板（model）,可以用regex库
			virtual str format(str mod, str lev, str content, str file, str line, str func)
			{
				replaceModel(mod, "[位置]", "[" + file + ":" + line + ":" + func + "]");
				replaceModel(mod, "[日期]", "[" + SysClock::NowFormat("YY-MM-DD") + "]");
				replaceModel(mod, "[时间]", "[" + SysClock::NowFormat("hh:mm:ss") + "]");
				replaceModel(mod, "[发起者]", "[" + name + "]");
				replaceModel(mod, "[等级]", "[" + lev + "]");
				replaceModel(mod, "[内容]", content);
				replaceColor(mod);
				return mod;
			}

			// 如果你不喜欢用{}匹配参数，可以重写这个函数，可以用regex库
			virtual str PiPei(str s, vec<str> args)
			{
				for (auto arg : args)
				{
					replaceModel(s, '{', '}', arg);
				}
				replaceModel(s, "{{", "{");
				replaceModel(s, "}}", '}');
				return s;
			}

		public:
			str name;
		};
	}
}
