#pragma once
#include "Memory/Memory.h"
#include "IO/Files.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

namespace X_Y
{

    // ── 统计 ──
    struct DataStoreStats
    {
        uint64_t EntryCount = 0;
        uint64_t TotalBytes = 0;
        uint64_t TotalCapacity = 0;

        struct EntryInfo
        {
            std::string Key;
            uint64_t Size;
            uint64_t Capacity;
        };
        std::vector<EntryInfo> Entries;
    };

    // ── DataStore ──
    // 基于文件的 key-value 存储
    //
    // 规则：
    //   - 所有条目由 DS 通过 GetOrCreate 创建，外部不挂载 Buffer
    //   - key = 文件路径（含后缀），如 "log"、"assets/tex/brick.img"
    //   - 保存时按 key 创建目录，文件内容 = Buffer 数据
    //   - .dsidx 索引文件记录所有 key 列表，启动时加载
    //   - Save = 覆盖写入，Flush = 追加写入
    //   - 析构不自动 Flush/Save，由业务决定
    //   - 析构时去除空的条目
    //$$ 好像对索引的维护有点问题，每次启动的时候都忘记重新加载文件内容，但还是照常保存索引，导致索引和实际文件不一致
    //$$ 我在Instance进行了补充
    class DataStore
    {
    public:
        static DataStore &Instance();

        // ── 核心操作 ──
        Buffer *GetOrCreate(const std::string &key, uint64_t reserveSize = 4096);
        Buffer *Get(const std::string &key);
        bool Append(const std::string &key, const void *data, uint64_t size,
                    uint64_t reserveSize = 4096);
        Buffer ReadCopy(const std::string &key) const;
        bool Remove(const std::string &key);
        void Rename(const std::string &oldKey, const std::string &newKey);
        bool Contains(const std::string &key) const;
        std::vector<std::string> ListKeys() const;

        // ── 目录遍历 ──
        // 列出数据目录下 dir 中的所有文件（相对 m_DataDir 的路径，自动排除 .dsidx）
        // dir 为空表示数据根目录；recursive=false 时只列出直属文件
        std::vector<std::string> ListFiles(const std::string &dir = "", bool recursive = true) const;
        // 将 dir 下的文件组织成树形文本；rootName 非空时作为首行（如 "assets/"）
        std::string ToTree(const std::string &dir = "", const std::string &rootName = "") const;

        // ── 生命周期 ──
        void ClearAll();

        // ── 持久化 ──
        bool Save(const std::string &key);
        bool SaveAll();
        void Flush(const std::string &key);
        void FlushAll();

        bool LoadFile(const std::string &filepath);
        void LoadDirectory(const XPath &dir);

        // ── 配置 ──
        void SetDataDir(const std::string &dir);
        const XPath &GetDataDir() const { return m_DataDir; }

        // ── 静音开关 ──
        // Enabled=true(默认)：正常工作。Enabled=false：所有操作空转（不读写 buffer/文件/索引），
        // 接口保持可用但啥也不干——用于让所有依赖方(如 Log 的 DataStoreDevice / LogViewer)集体失效。
        // ⚠️ 静音时 GetOrCreate/Get 返回 nullptr，调用方必须判空（现有 DataStoreDevice/LogViewer 均已判空）。
        void SetEnabled(bool on);
        bool IsEnabled() const { return m_Enabled; }

        // ── 统计 ──
        DataStoreStats GetStats() const;
        std::string ToString() const;

        ~DataStore();

    private:
        DataStore() = default;
        DataStore(const DataStore &) = delete;
        DataStore &operator=(const DataStore &) = delete;

        XPath KeyToPath(const std::string &key) const;
        XPath IndexPath() const;
        void EnsureDataDir();
        void SaveIndex();
        void LoadIndex();

        std::unordered_map<std::string, Buffer> m_Entries;
        XPath m_DataDir{"DataStore"};

        mutable std::shared_mutex m_Mutex;
        bool m_Enabled = true; // 静音开关（false=全部空转）
    };

} // namespace X_Y
