#include "Files/Files.h"

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
        std::ifstream file;
        std::optional<uint64_t> size;
        uint64_t position = 0;
    };

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
        m_Impl->file.open(p, std::ios::binary);
        if (!m_Impl->file)
            return false;

        std::error_code error;
        const auto size = std::filesystem::file_size(p, error);
        if (!error)
            m_Impl->size = size;
        return true;
    }

    StreamReadResult FileStream::Read(void *destination, uint64_t capacity)
    {
        if (!IsOpen() || (capacity > 0 && destination == nullptr))
            return {0, StreamReadStatus::Error};
        if (capacity == 0)
            return {0, StreamReadStatus::Data};

        const auto maxRead = static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max());
        const auto requested = std::min(capacity, maxRead);
        m_Impl->file.read(static_cast<char *>(destination), static_cast<std::streamsize>(requested));
        const auto bytesRead = static_cast<uint64_t>(m_Impl->file.gcount());
        m_Impl->position += bytesRead;

        if (bytesRead > 0)
            return {bytesRead, StreamReadStatus::Data};
        if (m_Impl->file.eof())
            return {0, StreamReadStatus::EndOfStream};
        return {0, StreamReadStatus::Error};
    }

    bool FileStream::Seek(uint64_t position)
    {
        if (!IsOpen() || position > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return false;

        m_Impl->file.clear();
        m_Impl->file.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!m_Impl->file)
            return false;

        m_Impl->position = position;
        return true;
    }

    uint64_t FileStream::Tell() const { return m_Impl->position; }
    std::optional<uint64_t> FileStream::Size() const { return m_Impl->size; }
    bool FileStream::IsOpen() const { return m_Impl->file.is_open(); }

    void FileStream::Close()
    {
        if (m_Impl->file.is_open())
            m_Impl->file.close();
        m_Impl->file.clear();
        m_Impl->size.reset();
        m_Impl->position = 0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // BufferStream
    // ════════════════════════════════════════════════════════════════════════

    void BufferStream::Open(BufferView source)
    {
        m_Source = source;
        m_Position = 0;
        m_IsOpen = true;
    }

    StreamReadResult BufferStream::Read(void *destination, uint64_t capacity)
    {
        if (!IsOpen() || (capacity > 0 && destination == nullptr))
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

    bool BufferStream::Seek(uint64_t position)
    {
        if (!IsOpen() || position > m_Source.Size)
            return false;
        m_Position = position;
        return true;
    }

    void BufferStream::Close()
    {
        m_Source = {};
        m_Position = 0;
        m_IsOpen = false;
    }

} // namespace X_Y
