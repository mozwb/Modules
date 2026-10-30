#include "IO/Stream.h"

// <fstream> / <filesystem> 只在这里出现，Stream.h 不背这个成本。
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace X_Y
{

    // ════════════════════════════════════════════════════════════════════════
    // FileStream
    // ════════════════════════════════════════════════════════════════════════

    struct FileStream::Impl
    {
        std::ifstream in;
        std::ofstream out;
        std::optional<uint64_t> size; // 只有读模式知道文件多大
        uint64_t position = 0;        // 本次打开后读/写了多少
    };

    namespace
    {
        constexpr uint64_t MaxStreamChunk()
        {
            return static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max());
        }
    }

    FileStream::FileStream() : m_Impl(std::make_unique<Impl>()) {}

    FileStream::FileStream(std::string_view path) : m_Impl(std::make_unique<Impl>())
    {
        Open(path);
    }

    FileStream::~FileStream() = default;

    bool FileStream::Open(std::string_view path)
    {
        Close();
        const auto p = std::filesystem::path(std::string(path));
        m_Impl->in.open(p, std::ios::binary);
        if (!m_Impl->in)
            return false;

        std::error_code error;
        const auto size = std::filesystem::file_size(p, error);
        if (!error)
            m_Impl->size = size;
        return true;
    }

    bool FileStream::OpenWrite(std::string_view path, bool append)
    {
        Close();
        const auto p = std::filesystem::path(std::string(path));
        const auto mode = std::ios::binary | std::ios::out |
                          (append ? std::ios::app : std::ios::trunc);
        m_Impl->out.open(p, mode);
        if (!m_Impl->out)
            return false;

        // 写模式不报文件多大：trunc 之后是 0，app 之后是「写之前有多大」，
        // 对着调方都没意义；写完想知道多大，看 Tell() 或自己去 stat。
        m_Impl->size.reset();
        return true;
    }

    StreamReadResult FileStream::Read(void *destination, uint64_t capacity)
    {
        if (!m_Impl->in.is_open() || (capacity > 0 && destination == nullptr))
            return {0, StreamReadStatus::Error};
        if (capacity == 0)
            return {0, StreamReadStatus::Data};

        const auto requested = std::min(capacity, MaxStreamChunk());
        m_Impl->in.read(static_cast<char *>(destination), static_cast<std::streamsize>(requested));
        const auto bytesRead = static_cast<uint64_t>(m_Impl->in.gcount());
        m_Impl->position += bytesRead;

        if (bytesRead > 0)
            return {bytesRead, StreamReadStatus::Data};
        if (m_Impl->in.eof())
            return {0, StreamReadStatus::EndOfStream};
        return {0, StreamReadStatus::Error};
    }

    uint64_t FileStream::Write(const void *source, uint64_t size)
    {
        if (!m_Impl->out.is_open() || (size > 0 && source == nullptr) || size == 0)
            return 0;

        const auto requested = std::min(size, MaxStreamChunk());
        m_Impl->out.write(static_cast<const char *>(source), static_cast<std::streamsize>(requested));
        if (!m_Impl->out)
            return 0;

        m_Impl->position += requested;
        return requested;
    }

    void FileStream::Flush()
    {
        if (m_Impl->out.is_open())
            m_Impl->out.flush();
    }

    bool FileStream::Seek(uint64_t position)
    {
        if (!IsOpen() || position > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return false;

        const auto offset = static_cast<std::streamoff>(position);
        if (m_Impl->in.is_open())
        {
            m_Impl->in.clear();
            m_Impl->in.seekg(offset, std::ios::beg);
            if (!m_Impl->in)
                return false;
        }
        else
        {
            m_Impl->out.clear();
            m_Impl->out.seekp(offset, std::ios::beg);
            if (!m_Impl->out)
                return false;
        }

        m_Impl->position = position;
        return true;
    }

    uint64_t FileStream::Tell() const { return m_Impl->position; }
    std::optional<uint64_t> FileStream::Size() const { return m_Impl->size; }

    bool FileStream::IsOpen() const
    {
        return m_Impl->in.is_open() || m_Impl->out.is_open();
    }

    void FileStream::Close()
    {
        if (m_Impl->in.is_open())
            m_Impl->in.close();
        if (m_Impl->out.is_open())
        {
            m_Impl->out.flush();
            m_Impl->out.close();
        }
        m_Impl->in.clear();
        m_Impl->out.clear();
        m_Impl->size.reset();
        m_Impl->position = 0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // MemoryStream
    // ════════════════════════════════════════════════════════════════════════

    void MemoryStream::Open(BufferView source)
    {
        m_Source = source;
        m_Target = nullptr;
        m_Position = 0;
        m_IsOpen = true;
        m_Writing = false;
    }

    void MemoryStream::OpenWrite(Buffer &target)
    {
        m_Source = {};
        m_Target = &target;
        m_Position = 0;
        m_IsOpen = true;
        m_Writing = true;
    }

    void MemoryStream::OpenWrite()
    {
        m_Owned.Release();
        OpenWrite(m_Owned);
    }

    StreamReadResult MemoryStream::Read(void *destination, uint64_t capacity)
    {
        if (!m_IsOpen || m_Writing || (capacity > 0 && destination == nullptr))
            return {0, StreamReadStatus::Error};
        if (capacity == 0)
            return {0, StreamReadStatus::Data};
        if (m_Position >= m_Source.Size)
            return {0, StreamReadStatus::EndOfStream};

        const auto bytesRead = std::min(capacity, m_Source.Size - m_Position);
        std::memcpy(destination, m_Source.Data + m_Position, static_cast<size_t>(bytesRead));
        m_Position += bytesRead;
        return {bytesRead, StreamReadStatus::Data};
    }

    uint64_t MemoryStream::Write(const void *source, uint64_t size)
    {
        if (!m_IsOpen || !m_Writing || m_Target == nullptr)
            return 0;
        if (size == 0)
            return 0;
        if (source == nullptr)
            return 0;

        m_Target->Append(source, size);
        m_Position += size;
        return size;
    }

    bool MemoryStream::Seek(uint64_t position)
    {
        if (!m_IsOpen || m_Writing || position > m_Source.Size)
            return false;
        m_Position = position;
        return true;
    }

    std::optional<uint64_t> MemoryStream::Size() const
    {
        if (m_Writing)
            return m_Target == nullptr ? std::nullopt : std::optional<uint64_t>(m_Target->Size);
        return m_Source.Size;
    }

    void MemoryStream::Close()
    {
        m_Source = {};
        m_Target = nullptr;
        m_Position = 0;
        m_IsOpen = false;
        m_Writing = false;
    }

} // namespace X_Y
