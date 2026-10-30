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

	Buffer FilesSystem::ReadFileBinary(const File &filepath)
	{
		std::ifstream stream(filepath, std::ios::binary | std::ios::ate);

		if (!stream)
		{
			// Failed to open the file
			return {};
		}

		std::streampos end = stream.tellg();
		stream.seekg(0, std::ios::beg);
		uint64_t size = end - stream.tellg();

		if (size == 0)
		{
			// File is empty
			return {};
		}

		Buffer buffer(size);
		stream.read(buffer.As<char>(), size);
		buffer.Size = size;
		stream.close();
		return buffer;
	}

	bool FilesSystem::WriteFileBinary(const File &filepath, const Buffer &buffer)
	{
		std::ofstream stream(filepath, std::ios::binary | std::ios::trunc);

		if (!stream)
		{
			// Failed to open the file for writing
			return false;
		}

		stream.write(reinterpret_cast<const char *>(buffer.Data), buffer.Size);
		stream.close();
		return true;
	}

	bool FilesSystem::AppendFileBinary(const File &filepath, const Buffer &buffer)
	{
		// 追加模式：文件不存在则创建，存在则追加到末尾
		std::ofstream stream(filepath, std::ios::binary | std::ios::app);

		if (!stream)
		{
			// 尝试创建目录
			std::error_code ec;
			std::filesystem::create_directories(filepath.parent_path(), ec);
			stream.open(filepath, std::ios::binary | std::ios::app);
			if (!stream)
				return false;
		}

		stream.write(reinterpret_cast<const char *>(buffer.Data), buffer.Size);
		stream.close();
		return true;
	}

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

namespace X_Y
{

	// ── 从 Files.h 挪进来的实现（避免公开头拖进 <regex>/<fstream>/<map>）──

	std::string BuildTreeText(const std::vector<std::string> &relativePaths,
	                          const std::string &rootName)
	{
		struct Node
		{
			std::map<std::string, Node> Children;
			bool IsDirectory() const { return !Children.empty(); }
		};

		Node root;
		for (const std::string &raw : relativePaths)
		{
			std::string path = raw;
			std::replace(path.begin(), path.end(), '\\', '/');

			Node *node = &root;
			std::size_t begin = 0;
			while (begin < path.size())
			{
				const std::size_t end = path.find('/', begin);
				const std::string part =
					path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);

				if (!part.empty() && part != ".")
					node = &node->Children[part];

				if (end == std::string::npos)
					break;
				begin = end + 1;
			}
		}

		std::string out;
		if (!rootName.empty())
			out += rootName + '\n';

		std::function<void(const Node &, const std::string &)> render =
			[&](const Node &node, const std::string &prefix)
		{
			std::vector<const std::pair<const std::string, Node> *> children;
			children.reserve(node.Children.size());
			for (const auto &child : node.Children)
				children.push_back(&child);

			std::sort(children.begin(), children.end(),
			          [](const auto *lhs, const auto *rhs)
			          {
				          const bool lhsDir = lhs->second.IsDirectory();
				          const bool rhsDir = rhs->second.IsDirectory();
				          if (lhsDir != rhsDir)
					          return lhsDir;
				          return lhs->first < rhs->first;
			          });

			for (std::size_t i = 0; i < children.size(); ++i)
			{
				const auto *child = children[i];
				const bool last = (i + 1 == children.size());

				out += prefix;
				out += last ? "└─ " : "├─ ";
				out += child->first;
				if (child->second.IsDirectory())
					out += '/';
				out += '\n';

				render(child->second, prefix + (last ? "   " : "│  "));
			}
		};

		render(root, "");
		return out;
	}

	bool XPath::MatchName(const std::string &pattern) const
	{
		if (pattern.empty())
			return false;
		std::string name = getName();
		std::regex reg(pattern);
		return std::regex_match(name, reg);
	}

	bool XPath::CreateFile() const
	{
		std::ofstream ofs(m_Path);
		return ofs.good();
	}

} // namespace X_Y
