#pragma once

/*
Stream.h
version 0.1.0

读写字节流：只定义「流」这件事，不关心字节从哪来。
磁盘、内存，以后套接字，都实现同一组接口。

一个类同时实现读和写，用哪半边由 Open* 的打开模式决定：
  FileStream::Open(path)            只读
  FileStream::OpenWrite(path, app)  只写（app=false 覆盖，true 追加）
  MemoryStream::Open(view)          从一段已有字节读
  MemoryStream::OpenWrite(buf)      往 Buffer 里追加

Files.h 管路径和整文件，Stream.h 管一段段字节，两个头同属 IO 模块。

文件名不叫 IO.h：Windows 的大小写不敏感文件系统会让 <io.h> 撞上它，
把系统的 _dup2 / _fileno 声明顶掉。

────────────────────────────────────────────────────────────────
内容
  StreamReadStatus / StreamReadResult   一次读的结果
  ReadStream                            只读接口
  WriteStream                           只写接口
  FileStream                            磁盘（读写都支持）
  MemoryStream                          内存（读 BufferView / 写 Buffer）

────────────────────────────────────────────────────────────────
几条约定

. 公开头只带 <cstdint> / <memory> / <optional> / <string_view>；
  <fstream> 和 <filesystem> 全部藏在 src/Stream.cpp。
. 不支持 Seek 的流返回 false / nullopt，接口不拆成两个。
. Write 返回实际写入字节数，调用方用 written == size 判断写全了没。
. 没开的那半边调用会失败：读模式下 Write 返回 0，写模式下 Read 返回 Error。
────────────────────────────────────────────────────────────────
用法

    #include <IO/Stream.h>
    using namespace X_Y;

    // 读
    FileStream fs("assets/big.bin");
    std::vector<uint8_t> chunk(4096);
    StreamReadResult r;
    while ((r = fs.Read(chunk.data(), chunk.size())).Status == StreamReadStatus::Data)
    {
        consume(chunk.data(), r.BytesRead);
    }

    // 写
    FileStream out;
    out.OpenWrite("out.bin");
    out.Write(bytes, size);
    out.Flush();
────────────────────────────────────────────────────────────────
*/

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <Memory/Memory.h>

namespace X_Y
{

    enum class StreamReadStatus
    {
        Data,
        EndOfStream,
        Error
    };

    struct StreamReadResult
    {
        uint64_t BytesRead = 0;
        StreamReadStatus Status = StreamReadStatus::EndOfStream;
    };

    // ── 只读字节流接口 ──────────────────────────────────────────────────────
    class ReadStream
    {
    public:
        virtual ~ReadStream() = default;

        virtual StreamReadResult Read(void *destination, uint64_t capacity) = 0;
        virtual bool Seek(uint64_t position) = 0;
        virtual uint64_t Tell() const = 0;
        virtual std::optional<uint64_t> Size() const = 0;
        virtual bool IsOpen() const = 0;
        virtual void Close() = 0;
    };

    // ── 只写字节流接口 ──────────────────────────────────────────────────────
    class WriteStream
    {
    public:
        virtual ~WriteStream() = default;

        // 返回实际写入的字节数；0 表示没写进去（没开 / 出错 / size 为 0）
        virtual uint64_t Write(const void *source, uint64_t size) = 0;
        // 把缓冲里的东西真正落盘 / 落地；没有缓冲的流是空操作
        virtual void Flush() = 0;
        virtual bool IsOpen() const = 0;
        virtual void Close() = 0;
    };

    // ── 文件流 ──────────────────────────────────────────────────────────────
    // ifstream / ofstream 藏在 src/Stream.cpp 的 Impl 里，所以本头不需要 <fstream>。
    class FileStream final : public ReadStream, public WriteStream
    {
    public:
        FileStream();
        explicit FileStream(std::string_view path); // 只读打开
        ~FileStream() override;

        // 读模式
        bool Open(std::string_view path);
        // 写模式：append=false 覆盖（trunc），true 追加（app）
        bool OpenWrite(std::string_view path, bool append = false);

        StreamReadResult Read(void *destination, uint64_t capacity) override;
        uint64_t Write(const void *source, uint64_t size) override;
        void Flush() override;

        bool Seek(uint64_t position) override;
        uint64_t Tell() const override;
        std::optional<uint64_t> Size() const override;
        bool IsOpen() const override;
        void Close() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ── 内存流 ──────────────────────────────────────────────────────────────
    // 读：把一段已有字节（BufferView，不拥有）当流；
    // 写：往一个 Buffer 追加，target 不传就用自己内部那个，用 Data() 取。
    class MemoryStream final : public ReadStream, public WriteStream
    {
    public:
        MemoryStream() = default;
        explicit MemoryStream(BufferView source) { Open(source); }
        explicit MemoryStream(Buffer &target) { OpenWrite(target); }

        void Open(BufferView source);
        void OpenWrite(Buffer &target);
        void OpenWrite();

        StreamReadResult Read(void *destination, uint64_t capacity) override;
        uint64_t Write(const void *source, uint64_t size) override;
        void Flush() override {}

        bool Seek(uint64_t position) override;
        uint64_t Tell() const override { return m_Position; }
        std::optional<uint64_t> Size() const override;
        bool IsOpen() const override { return m_IsOpen; }
        void Close() override;

        // 写模式下攒下来的字节（没传 target 时才有意义）
        const Buffer &Data() const { return m_Owned; }

    private:
        BufferView m_Source;
        Buffer *m_Target = nullptr;
        Buffer m_Owned;
        uint64_t m_Position = 0;
        bool m_IsOpen = false;
        bool m_Writing = false;
    };

} // namespace X_Y
