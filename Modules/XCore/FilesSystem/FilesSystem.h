#pragma once
#include "../Memory/Buffer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace X_Y
{
    using File = std::filesystem::path;

    // 将一组相对路径渲染成目录树文本（目录在前、文件在后，同级按名称排序）
    // rootName 非空时作为首行输出（如需结尾的 '/'，由调用方自行提供）
    inline std::string BuildTreeText(const std::vector<std::string> &relativePaths,
                                     const std::string &rootName = "")
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

    class FilesSystem
    {
    public:
        static bool FileExists(const File &filepath)
        {
            return std::filesystem::exists(filepath);
        }
        // 获取当前工作目录；失败时返回空路径
        static File CurrentDirectory()
        {
            std::error_code ec;
            File path = std::filesystem::current_path(ec);
            return ec ? File{} : path;
        }
        static Buffer ReadFileBinary(const File &filepath);
        static bool WriteFileBinary(const File &filepath, const Buffer &buffer);
        static bool AppendFileBinary(const File &filepath, const Buffer &buffer);

#ifdef XY_PLATFORM_WINDOWS
        static std::optional<std::wstring> OpenFileDialog(const char *filter = "All Files (*.*)\0*.*\0");
        static std::optional<std::wstring> SaveFileDialog(const char *filter = "All Files (*.*)\0*.*\0");
#endif
    };

    // @brief 路径对象，类似 Python 的 pathlib.Path
    //        每个 XPath 代表一个文件或目录路径，操作作用于自身
    class XPath
    {
    public:
        XPath() = default;
        XPath(const File &path) : m_Path(path) {}

        // 当前工作目录
        static XPath CurrentDirectory()
        {
            return XPath(FilesSystem::CurrentDirectory());
        }

        // ── 路径访问 ──
        const File &Path() const { return m_Path; }
        void SetPath(const File &path) { m_Path = path; }
        bool Empty() const { return m_Path.empty(); }
        // 通用操作
        bool IsAbsolute() const { return m_Path.is_absolute(); }
        bool IsRelative() const { return m_Path.is_relative(); }
        bool Exists() const { return FilesSystem::FileExists(m_Path); }
        bool Remove() const { return std::filesystem::remove(m_Path); }
        bool Rename(const File &newPath)
        {
            std::error_code ec;
            std::filesystem::rename(m_Path, newPath, ec);
            return !ec;
        }
        XPath GetParent() const { return XPath(m_Path.parent_path()); }
        bool IsPathEqual(const XPath &other) const { return std::filesystem::equivalent(m_Path, other.m_Path); }
        std::string getName() const { return m_Path.filename().string(); }
        std::string getRelativePath(const XPath &base) const
        {
            return std::filesystem::relative(m_Path, base.m_Path).string();
        }
        bool MatchName(const std::string &pattern) const
        {
            if (pattern.empty())
                return false;
            std::string name = getName();
            std::regex reg(pattern);
            return std::regex_match(name, reg);
        }

        // ── 文件操作（对自身路径） ──
        bool CreateFile() const
        {
            std::ofstream ofs(m_Path);
            return ofs.good();
        }
        bool IsFile() const { return std::filesystem::is_regular_file(m_Path); }
        Buffer ReadBinary() const { return FilesSystem::ReadFileBinary(m_Path); }
        bool WriteBinary(const Buffer &buf) const { return FilesSystem::WriteFileBinary(m_Path, buf); }
        bool AppendBinary(const Buffer &buf) const { return FilesSystem::AppendFileBinary(m_Path, buf); }
        bool MatchExtension(const std::string &extension) const { return std::filesystem::path(m_Path).extension() == extension; }
        bool CopyTo(const File &dest) const
        {
            std::error_code ec;
            std::filesystem::copy_file(m_Path, dest, std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        }
        // ——文件夹操作——
        bool CreateDirectory()
        {
            std::error_code ec;
            std::filesystem::create_directories(m_Path, ec);
            return !ec;
        }
        bool IsDirectory() const { return std::filesystem::is_directory(m_Path); }
        std::vector<XPath> ListDirectory() const
        {
            std::vector<XPath> entries;
            if (IsDirectory())
            {
                for (const auto &entry : std::filesystem::directory_iterator(m_Path))
                {
                    entries.emplace_back(entry.path());
                }
            }
            return entries;
        }
        std::vector<XPath> ListMatching(const std::string &pattern) const
        {
            std::vector<XPath> matchingEntries;
            if (IsDirectory())
            {
                for (const auto &entry : std::filesystem::directory_iterator(m_Path))
                {
                    XPath xpath(entry.path());
                    if (xpath.MatchName(pattern))
                    {
                        matchingEntries.push_back(xpath);
                    }
                }
            }
            return matchingEntries;
        }
        // 列出目录下的所有普通文件；recursive=true 时递归子目录
        std::vector<XPath> ListFiles(bool recursive = true) const
        {
            std::vector<XPath> files;
            if (!IsDirectory())
                return files;

            std::error_code ec;
            if (recursive)
            {
                for (std::filesystem::recursive_directory_iterator it(
                         m_Path, std::filesystem::directory_options::skip_permission_denied, ec),
                     end;
                     it != end; it.increment(ec))
                {
                    if (ec)
                        break;
                    if (it->is_regular_file(ec))
                        files.emplace_back(it->path());
                }
            }
            else
            {
                for (std::filesystem::directory_iterator it(m_Path, ec), end;
                     it != end; it.increment(ec))
                {
                    if (ec)
                        break;
                    if (it->is_regular_file(ec))
                        files.emplace_back(it->path());
                }
            }
            return files;
        }
        // 以自身为根渲染目录树；rootName 非空时作为首行
        std::string ToTree(const std::string &rootName = "") const
        {
            std::vector<std::string> relativePaths;
            for (const XPath &file : ListFiles(true))
                relativePaths.push_back(file.getRelativePath(*this));
            return BuildTreeText(relativePaths, rootName);
        }
        bool RemoveAll()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_Path, ec);
            return !ec;
        }
        bool CopyDirectoryTo(const File &dest) const
        {
            std::error_code ec;
            std::filesystem::copy(m_Path, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        }
        // ── 路径拼接 ──
        XPath &operator=(const File &sub)
        {
            m_Path = sub;
            return *this;
        }
        XPath &operator/=(const File &sub)
        {
            m_Path /= sub;
            return *this;
        }
        XPath &operator+=(const File &sub)
        {
            m_Path /= sub;
            return *this;
        }
        XPath operator/(const File &sub) const
        {
            XPath r = *this;
            r.m_Path /= sub;
            return r;
        }
        XPath operator+(const File &sub) const
        {
            XPath r = *this;
            r.m_Path /= sub;
            return r;
        }

        // ── 自动转 File（std::filesystem::path），方便传参 ──
        operator const File &() const { return m_Path; }

    private:
        File m_Path;
    };
}
