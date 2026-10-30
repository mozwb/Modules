#pragma once

/*
Files.h
version 0.0.1

路径、目录遍历、整文件读写、文件对话框。

IO 模块的公开头之一，另一半是 <IO/Stream.h>（读写字节流）。
一个模块：Files.h 管「名字和整文件」，Stream.h 管「数据怎么一段段流」。

流那几个名字（ReadStream / FileStream / MemoryStream）在 Stream.h 里，
本头末尾留了 Stream / BufferStream 两个别名给老代码。

────────────────────────────────────────────────────────────────
内容
  File / BuildTreeText   路径别名、目录树文本
  FilesSystem            静态文件操作 + 打开/保存对话框（仅 Windows）
  XPath                  类似 Python pathlib 的路径对象

────────────────────────────────────────────────────────────────
几条约定

. 路径参数统一用 std::string_view 或 File(=std::filesystem::path)，
  不用裸 const char*。
. XPath 里那些短方法是内联的（都是 filesystem 的一行封装）；
  真正干活的 ReadFileBinary / WriteFileBinary / 对话框在 src/FilesSystem.cpp。
. 文件对话框是平台相关的，目前直接声明在公开头里（用 XY_PLATFORM_WINDOWS
  包住）。将来收进 engine 的平台层后，这里只留抽象接口。
────────────────────────────────────────────────────────────────
用法

    #include <IO/Files.h>
    using namespace X_Y;

    // 路径
    XPath dir("assets");
    for (auto &f : dir.ListFiles(true))
        if (f.MatchName(".*\\.png")) { ... }   // 处理 png

    // 读写
    Buffer buf = FilesSystem::ReadFileBinary("assets/a.bin");
    FilesSystem::WriteFileBinary("assets/b.bin", buf);
────────────────────────────────────────────────────────────────
*/

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <IO/Stream.h>
#include <Memory/Memory.h>

namespace X_Y
{

    // ═══════════════════════════════════════════════════════════════════════
    //  路径与文件系统
    // ═══════════════════════════════════════════════════════════════════════

    using File = std::filesystem::path;

    // 将一组相对路径渲染成目录树文本（目录在前、文件在后，同级按名称排序）
    // rootName 非空时作为首行输出（如需结尾的 '/'，由调用方自行提供）
    // 实现放在 src/FilesSystem.cpp —— 它需要 <map>/<algorithm>/<functional>，
    // 这些不该由公开头带给所有使用者。
    std::string BuildTreeText(const std::vector<std::string> &relativePaths,
                              const std::string &rootName = "");

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
        // 正则匹配文件名；实现放 .cpp（本头不背 <regex> 的 448ms）
        bool MatchName(const std::string &pattern) const;

        // ── 文件操作（对自身路径） ──
        // 实现放 .cpp（本头不背 <fstream> 的 309ms）
        bool CreateFile() const;
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

    // ── 兼容别名 ────────────────────────────────────────────────────────────
    // 流已经搬去 IO 模块（<IO/Stream.h>）。老代码里的 Stream / BufferStream 名字
    // 还认，新代码请直接用 ReadStream / MemoryStream。
    using Stream = ReadStream;
    using BufferStream = MemoryStream;

} // namespace X_Y
