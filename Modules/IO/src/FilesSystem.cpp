#include "IO/Files.h"
#include <Decoder/Decoder.h>
#include <fstream>
#include <algorithm>
#include <functional>
#include <map>
#include <regex>
#include <filesystem>
#ifdef XY_PLATFORM_WINDOWS
#include <Windows.h>
#include <commdlg.h>
// Windows.h 把 CreateFile 定义成宏，会破坏 XPath::CreateFile 这个成员函数名
#undef CreateFile
#endif
namespace X_Y
{

#ifdef XY_PLATFORM_WINDOWS

	std::optional<std::wstring> FilesSystem::OpenFileDialog(const char *filter)
	{
		OPENFILENAMEA ofn = {};
		CHAR szFile[260] = {};
		CHAR currentDir[256] = {};

		ofn.lStructSize = sizeof(OPENFILENAMEA);
		ofn.hwndOwner = nullptr;
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		if (GetCurrentDirectoryA(256, currentDir))
			ofn.lpstrInitialDir = currentDir;

		if (!GetOpenFileNameA(&ofn))
		{
			// 用户点取消 / 对话框出错
			return std::nullopt;
		}

		// szFile 是 GBK char*，转换为wstring
		return Decoder::Decode(
			Decode::GBKText{ofn.lpstrFile},
			Decode::ToUTF16{});
	}

	std::optional<std::wstring> FilesSystem::SaveFileDialog(const char *filter)
	{
		OPENFILENAMEA ofn = {};
		CHAR szFile[260] = {};
		CHAR currentDir[256] = {};

		ofn.lStructSize = sizeof(OPENFILENAMEA);
		ofn.hwndOwner = nullptr;
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
		ofn.lpstrDefExt = strchr(filter, '\0') + 1;

		if (GetCurrentDirectoryA(256, currentDir))
			ofn.lpstrInitialDir = currentDir;

		if (!GetSaveFileNameA(&ofn))
		{
			return std::nullopt;
		}

		return Decoder::Decode(
			Decode::GBKText{ofn.lpstrFile},
			Decode::ToUTF16{});
	}

#endif

}

