#pragma once
#include "Log/Log.h"
#include "DataStore/DataStore.h"
#include "Files/Files.h"

namespace X_Y
{

    //仅供DEBUG时测试用
    // ── DataStoreDevice ──
    // Log 设备：每次 Log 调用时追加到 DataStore 管理的固定容量 Buffer
    // 写满时自动 flush 到文件后重置，从头继续写
    // 本质就是个 append-only 定长队列
    //
    // 前后端分离：
    //   - Log 只管写入（Append 到 Buffer）
    //   - DataStoreDevice 满了自动 Flush 到文件后重置
    //   - LogViewer 直接从同一 Buffer 读取（零拷贝）

    class DataStoreDevice : public LogConfigure::DEVICE
    {
    public:
        explicit DataStoreDevice(const std::string &key = "",
                                 uint64_t capacity = 65536)
            : m_Key(key.empty() ? DefaultKey() : key), m_Capacity(capacity) {}

        std::string toString() const override
        {
            return "DataStoreBuf[" + m_Key + "]";
        }

        void Log(const std::string &message) const override
        {
            std::string record = message + "\n";
            DataStore::Instance().Append(m_Key, record.data(), record.size(), m_Capacity);
        }

        ~DataStoreDevice()
        {
            // 追加当前尚未写出的日志片段，避免覆盖之前已经落盘的内容
            DataStore::Instance().Flush(m_Key);
        }

        void SetKey(const std::string &key) { m_Key = key; }
        const std::string &GetKey() const { return m_Key; }
        void SetCapacity(uint64_t capacity) { m_Capacity = capacity; }
        uint64_t GetCapacity() const { return m_Capacity; }

    private:
        static std::string DefaultKey()
        {
            return SysClock::NowFormat("YY-MM-DD") + ".log";
        }

        mutable std::string m_Key;
        uint64_t m_Capacity;
    };

} // namespace X_Y
