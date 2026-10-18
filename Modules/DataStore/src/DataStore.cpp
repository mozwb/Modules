#include "DataStore/DataStore.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <mutex>

namespace X_Y
{

    // ═════════════════════════════════════════════════════════════════════════════
    //  单例
    // ═════════════════════════════════════════════════════════════════════════════
    DataStore::~DataStore()
    {
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
        {
            if (!it->second)
            {
                it = m_Entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
        SaveIndex();
    }
    DataStore &DataStore::Instance()
    {
        static DataStore inst;
        static std::once_flag loadIndexOnce;
        std::call_once(loadIndexOnce, [&]()
                       { inst.LoadIndex(); });
        return inst;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  内部工具
    // ═════════════════════════════════════════════════════════════════════════════

    namespace
    {
        // 统一为 '/' 分隔，并去掉首尾分隔符与开头的 "./"
        std::string NormalizeKeyPath(const std::string &path)
        {
            std::string normalized;
            normalized.reserve(path.size());

            bool lastSlash = true;
            for (char ch : path)
            {
                if (ch == '\\')
                    ch = '/';

                if (ch == '/')
                {
                    if (!lastSlash)
                        normalized += '/';
                    lastSlash = true;
                }
                else
                {
                    normalized += ch;
                    lastSlash = false;
                }
            }

            while (normalized.rfind("./", 0) == 0)
                normalized.erase(0, 2);
            while (!normalized.empty() && normalized.back() == '/')
                normalized.pop_back();
            if (normalized == ".")
                normalized.clear();

            return normalized;
        }
    }

    XPath DataStore::KeyToPath(const std::string &key) const
    {
        return m_DataDir / key;
    }

    XPath DataStore::IndexPath() const
    {
        return m_DataDir / ".dsidx";
    }

    void DataStore::EnsureDataDir()
    {
        if (!m_DataDir.Exists())
            m_DataDir.CreateDirectory();
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  索引管理
    // ═════════════════════════════════════════════════════════════════════════════

    void DataStore::SaveIndex()
    {
        if (!IsEnabled())
            return; // 静音：不写索引
        EnsureDataDir();

        std::ofstream ofs(IndexPath().Path().string(), std::ios::binary);
        if (!ofs)
            return;

        for (const auto &pair : m_Entries)
            ofs << pair.first << "\n";

        ofs.close();
    }

    void DataStore::LoadIndex()
    {
        XPath idxPath = IndexPath();
        if (!idxPath.Exists())
            return;

        std::ifstream ifs(idxPath.Path().string());
        if (!ifs)
            return;

        std::string line;
        while (std::getline(ifs, line))
        {
            if (!line.empty() && line[0] != '#')
            {
                // 只注册 key，不加载数据（空 Buffer 占位）
                if (m_Entries.find(line) == m_Entries.end())
                    m_Entries[line] = Buffer();
            }
        }

        ifs.close();
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  配置
    // ═════════════════════════════════════════════════════════════════════════════

    void DataStore::SetDataDir(const std::string &dir)
    {
        m_DataDir = dir;
    }

    void DataStore::SetEnabled(bool on)
    {
        std::unique_lock lock(m_Mutex);
        m_Enabled = on;
        if (!on)
        {
            // 静音：清空内存条目，避免残留
            m_Entries.clear();
        }
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  核心操作
    // ═════════════════════════════════════════════════════════════════════════════

    Buffer *DataStore::GetOrCreate(const std::string &key, uint64_t reserveSize)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Enabled)
            return nullptr; // 静音：不创建，调用方需判空

        auto it = m_Entries.find(key);
        if (it != m_Entries.end())
        {
            // key 已注册但数据未加载（空 Buffer 占位）
            if (!it->second.Data)
            {
                XPath path = KeyToPath(key);
                if (path.Exists())
                {
                    Buffer loaded = FilesSystem::ReadFileBinary(path);
                    if (loaded)
                        it->second = std::move(loaded);
                }
            }
            return &it->second;
        }

        // 创建新条目
        Buffer buf(reserveSize);
        auto result = m_Entries.emplace(key, std::move(buf));
        SaveIndex();
        return &result.first->second;
    }

    Buffer *DataStore::Get(const std::string &key)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return nullptr; // 静音：不返回条目，调用方需判空

        auto it = m_Entries.find(key);
        if (it == m_Entries.end())
            return nullptr;

        // 空 Buffer 占位 → 从文件加载
        if (!it->second.Data)
        {
            lock.unlock();
            std::unique_lock ulock(m_Mutex);

            // 二次检查（防止另一个线程已加载）
            if (!it->second.Data)
            {
                XPath path = KeyToPath(key);
                if (path.Exists())
                {
                    Buffer loaded = FilesSystem::ReadFileBinary(path);
                    if (loaded)
                        it->second = std::move(loaded);
                }
            }
            return &it->second;
        }

        return &it->second;
    }

    bool DataStore::Append(const std::string &key, const void *data, uint64_t size,
                           uint64_t reserveSize)
    {
        if (!data && size > 0)
        {
            return false;
        }

        std::unique_lock lock(m_Mutex);
        if (!m_Enabled)
        {
            return false;
        }

        auto it = m_Entries.find(key);
        if (it == m_Entries.end())
        {
            Buffer buffer(reserveSize);
            auto result = m_Entries.emplace(key, std::move(buffer));
            it = result.first;
            SaveIndex();
        }

        Buffer &buffer = it->second;
        if (!buffer.Data)
        {
            XPath path = KeyToPath(key);
            if (path.Exists())
            {
                Buffer loaded = FilesSystem::ReadFileBinary(path);
                if (loaded)
                    buffer = std::move(loaded);
            }
            if (!buffer.Data)
                buffer.Reserve(reserveSize);
        }

        if (buffer.Size + size > buffer.Capacity)
        {
            EnsureDataDir();
            XPath path = KeyToPath(key);
            XPath parent = path.GetParent();
            if (!parent.Exists())
                parent.CreateDirectory();
            if (buffer && !FilesSystem::AppendFileBinary(path, buffer))
            {
                return false;
            }
            buffer.Allocate(0);
            buffer.Reserve(reserveSize);
        }

        buffer.Append(data, size);
        return true;
    }

    Buffer DataStore::ReadCopy(const std::string &key) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return {};

        auto it = m_Entries.find(key);
        if (it == m_Entries.end() || !it->second)
            return {};
        return it->second.Copy();
    }

    bool DataStore::Remove(const std::string &key)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Enabled)
            return false; // 静音

        XPath path = KeyToPath(key);
        if (path.Exists())
            path.Remove();

        bool erased = m_Entries.erase(key) > 0;
        if (erased)
            SaveIndex();

        return erased;
    }

    void DataStore::Rename(const std::string &oldKey, const std::string &newKey)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Enabled)
            return; // 静音

        auto it = m_Entries.find(oldKey);
        if (it == m_Entries.end())
            return;

        m_Entries[newKey] = std::move(it->second);
        m_Entries.erase(it);

        XPath oldPath = KeyToPath(oldKey);
        XPath newPath = KeyToPath(newKey);
        if (oldPath.Exists())
            oldPath.Rename(newPath);

        SaveIndex();
    }

    bool DataStore::Contains(const std::string &key) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return false; // 静音
        return m_Entries.find(key) != m_Entries.end();
    }

    std::vector<std::string> DataStore::ListKeys() const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return {}; // 静音

        std::vector<std::string> keys;
        keys.reserve(m_Entries.size());
        for (const auto &pair : m_Entries)
            keys.push_back(pair.first);
        return keys;
    }

    std::vector<std::string> DataStore::ListFiles(const std::string &dir, bool recursive) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return {}; // 静音

        XPath root = dir.empty() ? m_DataDir : KeyToPath(dir);
        std::vector<std::string> files;
        for (const XPath &file : root.ListFiles(recursive))
        {
            std::error_code ec;
            const std::string relative =
                std::filesystem::relative(file.Path(), m_DataDir.Path(), ec).generic_string();
            if (ec || relative.empty() || relative == ".dsidx")
                continue;
            files.push_back(relative);
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    std::string DataStore::ToTree(const std::string &dir, const std::string &rootName) const
    {
        std::vector<std::string> files = ListFiles(dir, true);

        // ListFiles 返回相对 m_DataDir 的路径；树以 dir 为根时需要去掉该前缀
        const std::string prefix = NormalizeKeyPath(dir);
        if (!prefix.empty())
        {
            const std::string prefixWithSlash = prefix + '/';
            for (std::string &file : files)
            {
                if (file.compare(0, prefixWithSlash.size(), prefixWithSlash) == 0)
                    file.erase(0, prefixWithSlash.size());
            }
        }

        return BuildTreeText(files, rootName);
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  持久化
    // ═════════════════════════════════════════════════════════════════════════════

    bool DataStore::Save(const std::string &key)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return false; // 静音：不落盘

        auto it = m_Entries.find(key);
        if (it == m_Entries.end() || !it->second)
            return false;

        EnsureDataDir();

        // 确保父目录存在

        XPath path = KeyToPath(key);
        XPath parent = path.GetParent();
        if (!parent.Exists())
            parent.CreateDirectory();

        return FilesSystem::WriteFileBinary(path, it->second);
    }

    void DataStore::ClearAll()
    {
        std::lock_guard<std::shared_mutex> lock(m_Mutex);
        m_Entries.clear();
    }

    bool DataStore::SaveAll()
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return false; // 静音：不落盘

        if (m_Entries.empty())
            return true;

        EnsureDataDir();

        bool allOk = true;
        for (const auto &pair : m_Entries)
        {
            if (!pair.second)
                continue;

            XPath path = KeyToPath(pair.first);
            XPath parent = path.GetParent();
            if (!parent.Exists())
                parent.CreateDirectory();

            if (!FilesSystem::WriteFileBinary(path, pair.second))
                allOk = false;
        }

        SaveIndex();
        return allOk;
    }

    void DataStore::Flush(const std::string &key)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return; // 静音：不落盘

        auto it = m_Entries.find(key);
        if (it == m_Entries.end() || !it->second)
            return;

        EnsureDataDir();

        XPath path = KeyToPath(key);
        XPath parent = path.GetParent();
        if (!parent.Exists())
            parent.CreateDirectory();

        FilesSystem::AppendFileBinary(path, it->second);
    }

    void DataStore::FlushAll()
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Enabled)
            return; // 静音：不落盘

        if (m_Entries.empty())
            return;

        EnsureDataDir();

        for (const auto &pair : m_Entries)
        {
            if (!pair.second)
                continue;

            XPath path = KeyToPath(pair.first);
            XPath parent = path.GetParent();
            if (!parent.Exists())
                parent.CreateDirectory();

            FilesSystem::AppendFileBinary(path, pair.second);
        }
    }

    bool DataStore::LoadFile(const std::string &filepath)
    {
        if (!IsEnabled())
            return false; // 静音

        Buffer buf = FilesSystem::ReadFileBinary(filepath);
        if (!buf)
            return false;

        XPath path(filepath);
        std::string key = path.getName();

        std::unique_lock lock(m_Mutex);
        if (!m_Enabled)
            return false; // 静音（锁内复核）
        m_Entries[key] = std::move(buf);
        SaveIndex();

        return true;
    }

    void DataStore::LoadDirectory(const XPath &dir)
    {
        if (!IsEnabled())
            return; // 静音
        if (!dir.Exists() || !dir.IsDirectory())
            return;

        // 先重建索引
        LoadIndex();

        std::vector<XPath> files = dir.ListDirectory();
        for (const auto &file : files)
        {
            if (file.IsFile() && file.getName() != ".dsidx")
            {
                std::string key = file.getRelativePath(m_DataDir);
                if (m_Entries.find(key) == m_Entries.end())
                    m_Entries[key] = Buffer(); // 空占位
            }
        }

        SaveIndex();
    }

    // ═════════════════════════════════════════════════════════════════════════════
    //  统计
    // ═════════════════════════════════════════════════════════════════════════════

    DataStoreStats DataStore::GetStats() const
    {
        std::shared_lock lock(m_Mutex);

        DataStoreStats stats;
        stats.EntryCount = m_Entries.size();
        stats.Entries.reserve(m_Entries.size());

        for (const auto &pair : m_Entries)
        {
            stats.TotalBytes += pair.second.Size;
            stats.TotalCapacity += pair.second.Capacity;

            DataStoreStats::EntryInfo info;
            info.Key = pair.first;
            info.Size = pair.second.Size;
            info.Capacity = pair.second.Capacity;
            stats.Entries.push_back(std::move(info));
        }

        return stats;
    }

    std::string DataStore::ToString() const
    {
        DataStoreStats s = GetStats();

        std::ostringstream oss;
        oss << "=== DataStore Stats ===\n";
        oss << "Entries: " << s.EntryCount << "\n";
        oss << "Total: " << s.TotalBytes << " bytes (data) / "
            << s.TotalCapacity << " bytes (capacity)\n\n";

        for (const auto &info : s.Entries)
        {
            oss << "  [" << info.Key << "] "
                << info.Size << "/" << info.Capacity << " bytes\n";
        }

        oss << "========================\n";
        return oss.str();
    }

} // namespace X_Y
