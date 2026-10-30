# IO —— 文件系统 + 读写字节流

> 版本 0.2.0 · 2026-10-30 · 状态：**读写都落地，全项目构建通过**
> 位置：`Modules/IO` · 依赖：`Memory` · 被依赖：`DataStore` / `Decoder` / `Widget`

---

## 1. 一句话

**一个模块，两件事**：`Files.h` 管「名字和整文件」，`Stream.h` 管「数据一段段流」。

原来的 `Files` 模块整个并进来了，不再有独立的 `Files` 目标。

---

## 2. 现在的结构

| 文件 | 内容 |
|---|---|
| `IO/Files.h` | `File`（path 别名）/ `BuildTreeText` / `FilesSystem` / `XPath` |
| `IO/Stream.h` | `StreamReadStatus` / `StreamReadResult` / `ReadStream` / `WriteStream` / `FileStream` / `MemoryStream` |
| `IO/src/FilesSystem.cpp` | 路径与整文件读写、Win32 文件对话框 |
| `IO/src/Stream.cpp` | 文件流 / 内存流实现 |

```
        DataStore / Decoder / Widget
                    │
                   IO ──→ Memory        （IO 只依赖 Memory）
              ┌─────┴─────┐
          Files.h      Stream.h
        （名字/整文件）  （一段段字节）
```

- `ReadStream` 就是原来的 `Stream`，只读接口；`FileStream` 原样搬过来，`BufferStream` 改名 `MemoryStream`。
- 写半边：`WriteStream`（`Write` / `Flush`），和 `ReadStream` 并列，同一个类两边都实现，
  开哪半边由打开方式决定：

  | 打开 | 行为 |
  |---|---|
  | `FileStream::Open(path)` | 只读 |
  | `FileStream::OpenWrite(path)` | 覆盖写（trunc） |
  | `FileStream::OpenWrite(path, true)` | 追加写（app） |
  | `MemoryStream::Open(view)` | 从已有字节读（不拥有） |
  | `MemoryStream::OpenWrite(buf)` | 追加写进外部 `Buffer` |
  | `MemoryStream::OpenWrite()` | 追加写进自己的 `Buffer`，用 `Data()` 取 |

  没开的那半边调用会失败：读模式下 `Write` 返回 0，写模式下 `Read` 返回 `Error`。
  `Write` 返回实际写入字节数，调用方用 `written == size` 判断写全了没。
- 末尾留着 `using Stream = ReadStream;` 和 `using BufferStream = MemoryStream;`，
  调用点还没逐个换，等换完再删这两个别名。

---

## 3. 边界

**做**：路径 / 目录 / 整文件读写 / 文件对话框；字节流读写（文件、内存）。
**不做**：缓冲装饰（`ifstream` / `ofstream` 自带缓冲）、文本层、`operator<<`、异常式 IO、异步、压缩加密。

---

## 4. 两个坑（都是真踩过的）

1. **公开头不能叫 `IO.h`**。Windows 文件系统大小写不敏感，`#include <io.h>`
   会命中它，把系统的 `_dup2` / `_fileno` 声明顶掉，`Widget/src/Entry.cpp` 直接编译失败。
   所以是 `IO/Files.h` + `IO/Stream.h`，没有 `IO/IO.h`。
2. **`FilesSystem.cpp` 是 Win32 后端**（对话框 + 借用 Decoder 的 GBK→UTF16 模板），
   非 Windows 用 `xy_module(IO ... EXCLUDE "src/FilesSystem\\.cpp")` 摘掉，流那半边照常编。

---

## 5. 验收

写了 19 项自测（临时脚本，没进仓库）：覆盖写 → 读回比对、追加写、`Tell()`、
写模式 `Read` 报错、读模式 `Write` 返回 0、内存流写进自己 / 写进外部 `Buffer`。
全过。

---

## 6. 记着但先不做

`BufferedStream`、`TextReader` / `TextWriter`（UTF-8 + `ReadLine`）、
`LimitedReadStream`、`StdStream`。有真实调用方了再捡，捡的时候不改上层接口。
