#include <IO/Files.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <regex>

// IO 的核心实现：跨平台文件/目录/整文件读写。
// Windows 文件对话框依赖 Decoder，留在 FilesSystem.cpp；
// xy 构建 IO 库时排除 FilesSystem.cpp，只编这个文件 + Stream.cpp。
namespace fs = std::filesystem;
namespace X_Y
{
    Buffer FilesSystem::ReadFileBinary(const File &filepath)
    {
        std::ifstream stream(filepath, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return {};
        }

        const std::streampos end = stream.tellg();
        stream.seekg(0, std::ios::beg);
        const uint64_t size = end - stream.tellg();
        if (size == 0)
        {
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
            return false;
        }

        stream.write(reinterpret_cast<const char *>(buffer.Data), buffer.Size);
        stream.close();
        return true;
    }

    bool FilesSystem::AppendFileBinary(const File &filepath, const Buffer &buffer)
    {
        std::ofstream stream(filepath, std::ios::binary | std::ios::app);
        if (!stream)
        {
            std::error_code ec;
            std::filesystem::create_directories(filepath.parent_path(), ec);
            stream.open(filepath, std::ios::binary | std::ios::app);
            if (!stream)
            {
                return false;
            }
        }

        stream.write(reinterpret_cast<const char *>(buffer.Data), buffer.Size);
        stream.close();
        return true;
    }

    File FilesSystem::FindFile(const File &directory, std::string filename, int direction, int depth)
    {
        if (direction == 0) // 向上查找：往父目录
        {
            File currentDir = directory;
            int currentDepth = 0;
            while (true)
            {
                File targetFile = currentDir / filename;
                std::error_code ec;
                if (fs::exists(targetFile, ec)) // 文件 OR 文件夹，存在即匹配
                {
                    return targetFile;
                }

                // 达到向上层数上限，停止
                if (depth >= 0 && currentDepth >= depth)
                {
                    break;
                }

                File parentDir = currentDir.parent_path();
                if (parentDir == currentDir) // 到达根目录
                {
                    break;
                }
                currentDir = parentDir;
                ++currentDepth;
            }
        }
        else if (direction == 1) // 向下查找：递归子目录
        {
            std::error_code ec;
            fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);

            // 根目录本身就打不开/不存在
            if (ec)
            {
                return {};
            }

            for (; it != fs::recursive_directory_iterator(); ++it)
            {
                const auto &entry = *it;

                if (depth >= 0 && it.depth() >= depth)
                {
                    it.disable_recursion_pending();
                    continue;
                }

                if (entry.path().filename() == filename)
                {
                    return entry.path();
                }
            }
        }

        return {}; // 未找到
    }
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
                const std::string part = path.substr(
                    begin, end == std::string::npos ? std::string::npos : end - begin);

                if (!part.empty() && part != ".")
                {
                    node = &node->Children[part];
                }

                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1;
            }
        }

        std::string out;
        if (!rootName.empty())
        {
            out += rootName + '\n';
        }

        std::function<void(const Node &, const std::string &)> render =
            [&](const Node &node, const std::string &prefix)
        {
            std::vector<const std::pair<const std::string, Node> *> children;
            children.reserve(node.Children.size());
            for (const auto &child : node.Children)
            {
                children.push_back(&child);
            }

            std::sort(children.begin(), children.end(),
                      [](const auto *lhs, const auto *rhs)
                      {
                          const bool lhsDir = lhs->second.IsDirectory();
                          const bool rhsDir = rhs->second.IsDirectory();
                          if (lhsDir != rhsDir)
                          {
                              return lhsDir;
                          }
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
                {
                    out += '/';
                }
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
        {
            return false;
        }
        const std::string name = getName();
        return std::regex_match(name, std::regex(pattern));
    }

    bool XPath::CreateFile() const
    {
        std::ofstream ofs(m_Path);
        return ofs.good();
    }
}
