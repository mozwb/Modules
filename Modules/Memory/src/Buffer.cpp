#include "Memory/Memory.h"

// ⚠️ 曾经是 "../../Memory/XMemory.h"（旧内存体系）。
//    Buffer 用到的 Memory::Instance().Alloc/Free 是门面的旧名兼容层，
//    声明在 XMemFacade.h 里 —— 旧头文件已被新体系取代，不再 include。
#include "Memory/Memory.h"
#include <cstdio>
#include <limits>

namespace X_Y
{

    // ── 构造/析构/拷贝/移动 ──

    Buffer::Buffer(uint64_t initialCapacity)
    {
        Reserve(initialCapacity);
    }

    Buffer::Buffer(const Buffer &other)
        : Data(nullptr), Size(0), Capacity(0)
    {
        if (other.Data && other.Size > 0)
        {
            Reserve(other.Size);
            memcpy(Data, other.Data, other.Size);
            Size = other.Size;
        }
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : Data(other.Data), Size(other.Size), Capacity(other.Capacity)
    {
        other.Data = nullptr;
        other.Size = 0;
        other.Capacity = 0;
    }

    Buffer::~Buffer()
    {
        Release();
    }

    Buffer &Buffer::operator=(const Buffer &other)
    {
        if (this != &other)
        {
            Release();
            if (other.Data && other.Size > 0)
            {
                Reserve(other.Size);
                memcpy(Data, other.Data, other.Size);
                Size = other.Size;
            }
        }
        return *this;
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept
    {
        if (this != &other)
        {
            Release();
            Data = other.Data;
            Size = other.Size;
            Capacity = other.Capacity;
            other.Data = nullptr;
            other.Size = 0;
            other.Capacity = 0;
        }
        return *this;
    }

    // ── 内存管理（统一走 Memory） ──

    bool Buffer::Reserve(uint64_t newCapacity)
    {
        if (newCapacity <= Capacity)
            return true;

        if (newCapacity > std::numeric_limits<uint64_t>::max() - 15)
            return false;

        // 16 字节对齐
        newCapacity = (newCapacity + 15) & ~15ULL;
        uint8_t *newData = static_cast<uint8_t *>(Memory::Instance().Alloc(newCapacity));
        if (!newData)
            return false;

        if (Data)
        {
            std::memcpy(newData, Data, Size);
            const uint64_t preservedSize = Size;
            Release();
            Size = preservedSize; // Release() 会清零 Size，扩容后必须恢复
        }
        Data = newData;
        Capacity = newCapacity;
        return true;
    }

    bool Buffer::Ensure(uint64_t neededSize)
    {
        if (neededSize > Capacity)
        {
            uint64_t newCap = Capacity == 0 ? 64 : Capacity * 2;
            if (Capacity > std::numeric_limits<uint64_t>::max() / 2)
                newCap = neededSize;
            while (newCap < neededSize)
            {
                if (newCap > std::numeric_limits<uint64_t>::max() / 2)
                {
                    newCap = neededSize;
                    break;
                }
                newCap *= 2;
            }
            return Reserve(newCap);
        }
        return true;
    }

    void Buffer::Allocate(uint64_t size)
    {
        Release();
        if (size == 0 || Reserve(size))
            Size = size;
    }

    void Buffer::Release()
    {
        if (Data)
        {
            Memory::Instance().Free(Data);
            Data = nullptr;
            Size = 0;
            Capacity = 0;
        }
    }

    void Buffer::ZeroInitialize()
    {
        if (Data)
            std::memset(Data, 0, Capacity);
    }

    // ── 读写操作 ──

    void Buffer::Append(const void *src, uint64_t len)
    {
        if (!src || len == 0)
            return;
        if (len > std::numeric_limits<uint64_t>::max() - Size)
            return;
        if (!Ensure(Size + len))
            return;
        std::memcpy(Data + Size, src, len);
        Size += len;
    }

    void Buffer::Overwrite(const void *src, uint64_t len)
    {
        if (!src && len > 0)
            return;
        Allocate(len);
        if (len > 0)
            std::memcpy(Data, src, len);
    }

    void Buffer::Overwrite(const Buffer &other)
    {
        Overwrite(other.Data, other.Size);
    }

    void Buffer::Overwrite(std::initializer_list<uint8_t> list)
    {
        Overwrite(list.begin(), list.size());
    }

    void Buffer::Overwrite(const char *str)
    {
        uint64_t len = str ? std::strlen(str) : 0;
        Overwrite(reinterpret_cast<const uint8_t *>(str), len);
    }

    BufferView Buffer::View(uint64_t offset, uint64_t size) const
    {
        return BufferView(*this, offset, size);
    }

    // ── toString ──

    std::string Buffer::toString() const
    {
        if (!Data || Size == 0)
            return "";

        uint64_t printable = 0;
        uint64_t sampleLen = Size < 128 ? Size : 128;
        for (uint64_t i = 0; i < sampleLen; i++)
            if (isprint(Data[i]) || Data[i] == '\n' || Data[i] == '\t')
                printable++;

        std::ostringstream oss;
        if (printable * 2 > Size)
        {
            uint64_t showLen = Size < 256 ? Size : 256;
            for (uint64_t i = 0; i < showLen; i++)
            {
                if (Data[i] == '\n')
                    oss << "\\n";
                else if (Data[i] == '\t')
                    oss << "\\t";
                else if (isprint(Data[i]))
                    oss << (char)Data[i];
                else
                    oss << '.';
            }
            if (Size > 256)
                oss << "...";
        }
        else
        {
            uint64_t showLen = Size < 32 ? Size : 32;
            for (uint64_t i = 0; i < showLen; i++)
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", Data[i]);
                oss << buf;
            }
            if (Size > 32)
                oss << "...";
        }
        return oss.str();
    }

    // ── BufferView::toString ──

    std::string BufferView::toString() const
    {
        if (!Data || Size == 0)
            return "";

        std::ostringstream oss;
        uint64_t showLen = Size < 24 ? Size : 24;
        for (uint64_t i = 0; i < showLen; i++)
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", Data[i]);
            oss << buf;
        }
        if (Size > 24)
            oss << "...";
        return oss.str();
    }

}
// namespace X_Y
