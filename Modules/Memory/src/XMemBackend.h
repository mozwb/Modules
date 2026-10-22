#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  XMemBackend.h — 内存后端接口
//
//  职责边界（重要）：
//    本文件【只负责"字节从哪来、怎么还"】。
//    不认识统计、不认识对象构造、不认识 UI。
//    统计在 XMemStats.h，门面在 XMemFacade.h，公共枚举在 XMemTypes.h。
//
//  铁律（改本文件前必读）：
//    1. 后端内部【只能】使用 ::malloc / ::free（或 OS 原语）拿内存，
//       绝对不许调用门面、更不许用 std::vector / std::string。
//       理由：门面首次使用时要让后端就位，若后端自己又回调门面，
//             就是自举递归（bootstrap recursion）→ 死循环 / UB。
//       将来 SlabBackend 的元数据（chunk 表、free list）也必须走 malloc。
//    2. 后端不保存任何需要"运行期构造"的全局状态。
//    3. 每个后端必须能回答 own(ptr)：这个指针是不是我发的？
//       —— 供"靠地址判断归属"的实现使用（当前门面走分配头方案）。
//
//  当前进度：
//    ✅ CrtBackend   —— 标准库堆（默认后端，标准库兜底）
//    ✅ SlabBackend  —— 自研 slab（已从旧 XMemory.h 迁入，见下方）
// ═════════════════════════════════════════════════════════════════════════════

#include "Memory/Memory.h"

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdlib>

namespace X_Y
{

    // ── 后端接口 ─────────────────────────────────────────────────────────────
    // 纯抽象：门面持有 IMemoryBackend*，外界永远看不到具体后端。
    //
    // ⚠️ allocate 与 deallocate 的分工：
    //    allocate  → 门面把 BackendType 解析成具体后端，再调到这里。
    //    deallocate→ 门面【无法】知道该找哪个后端（delete 不传回构造参数），
    //                所以门面要么靠 header、要么挨个问 own()。
    //                本接口保留 size 参数是为了让后端能自己记账/还给 OS。
    class IMemoryBackend
    {
    public:
        virtual ~IMemoryBackend() = default;

        // 分配 size 字节。返回的指针必须满足 alignof(std::max_align_t)。
        // 失败返回 nullptr（由门面决定抛 bad_alloc 还是返回空）。
        virtual void *allocate(uint64_t size) = 0;

        // 归还。size 是当初 allocate 的字节数（可能为 0 = 未知）。
        // 实现必须容忍 size == 0（门面在无法得知大小时会传 0）。
        virtual void deallocate(void *ptr, uint64_t size) = 0;

        // 这个指针是不是本后端发出的？
        // ★ 这是 deallocate(ptr) 能否只靠地址判断归属的关键接口。
        //   CrtBackend 无法精确回答（见其实现注释），故门面改用 header 方案；
        //   保留此接口供 SlabBackend 等"可精确自报"的后端使用。
        virtual bool own(void *ptr) const = 0;

        // 后端名（调试/统计用）
        virtual const char *name() const = 0;

        // 后端向 OS 拿到的总容量（字节）。统计"我占了多少内存"用。
        virtual uint64_t capacity() const = 0;
    };

    // ── CrtBackend：标准库堆 ─────────────────────────────────────────────────
    // 默认后端。目的就是"标准库兜底"：
    //   - 性能与别人直接用 new/malloc 完全一致（就是同一个堆）；
    //   - 行为可预期，出问题不是我们的分配器问题；
    //   - 自研 slab 只在需要优化的热点上按需启用，效果不好随时切回来。
    //
    // ⚠️ 本后端只用 ::malloc / ::free，且【不保存任何运行期状态】，
    //    因此它是平凡类：可以在静态初始化期安全使用，无自举问题。
    //    —— 这一点对"全局 operator new 重载"至关重要（见 XMemGlobalNew.cpp）。
    class CrtBackend final : public IMemoryBackend
    {
    public:
        void *allocate(uint64_t size) override
        {
            if (size == 0)
                return nullptr;
            return std::malloc(static_cast<std::size_t>(size));
        }

        void deallocate(void *ptr, uint64_t /*size*/) override
        {
            // 标准库 free 自己知道块大小，不需要 size。
            std::free(ptr);
        }

        bool own(void * /*ptr*/) const override
        {
            // ❌ 标准库堆没有"这个指针是不是我的"的 API。
            //    Windows 的 HeapValidate/HeapWalk 极慢，不能每次释放都调用。
            //    所以 CrtBackend 一律返回 false —— 门面据此事先知道：
            //    "归属判断不能依赖后端自报，必须用 header 记录"。
            return false;
        }

        const char *name() const override { return "CrtBackend"; }

        uint64_t capacity() const override
        {
            // 标准库堆不向外暴露容量。返回 0 表示"不可知"，
            // 门面统计里这一项显示 0，不代表没占用。
            return 0;
        }
    };

    // ── SlabBackend：自研 slab 分配器 ────────────────────────────────────────
    //
    //  一句话：预先拿 64KB 大块（chunk），按 8 档固定尺寸切成小块零售。
    //  适用场景：大量同尺寸小对象（UI 的控件、事件、字符串）。
    //
    //  ── 档位 ──
    //    64B | 128B | 256B | 512B | 1KB | 4KB | 16KB | 64KB   （chunk 64KB）
    //    请求尺寸向上对齐到最近的档；超过 64KB 的【不由本后端服务】——
    //    策略应把大块发给 Crt（见 allocate 里的兜底说明）。
    //
    //  ── 重要不变量（改实现前必读）──
    //    1. free list 存的是**实际 block 地址**，不是索引 ——
    //       这样 chunk 增删不会让待分配的地址失效。
    //    2. chunk 能否回收**只能看它自己的 FreeBlocks**，
    //       不能看 bin 的空闲总数（那是所有 chunk 的总和）。
    //    3. 只有 block 全部空闲的 chunk 才还给 OS。
    //
    //  ── 为什么实现里全是 ::malloc + 手写数组 + 自旋锁（铁律 1）──
    //    若用 std::vector / std::mutex，构造期就会 new →
    //    全局 operator new → 门面 → 后端 → 自己 → **自举递归死循环**。
    //    自旋锁只搬指针、临界区极短，且**无构造无析构**，适合静态期安全使用。
    //
    //  ── own(ptr) ──
    //    和 CrtBackend 相反，本后端【能】精确回答归属：
    //    查全局 chunk 区间表（base 升序二分）。门面用它走释放快路径。
    //
    //  ── 线程安全 ──
    //    每档一把自旋锁，跨档完全并行；chunk 区间表另有自己的锁。
    //    锁不嵌套，无死锁顺序问题。
    //    实现见 src/Memory/XMemBackend.cpp
    class SlabBackend final : public IMemoryBackend
    {
    public:
        SlabBackend() = default;

        SlabBackend(const SlabBackend &) = delete;
        SlabBackend &operator=(const SlabBackend &) = delete;

        void *allocate(uint64_t size) override;
        void deallocate(void *ptr, uint64_t size) override;
        bool own(void *ptr) const override;

        const char *name() const override { return "SlabBackend"; }

        // 向 OS 要来的总字节数（chunk 之和，不含元数据）。
        uint64_t capacity() const override;

        // 把空闲 chunk 还给 OS（程序收尾调用；之后仍可继续分配）。
        void releaseAll();

        // ── 档位参数（对外只读，调试/统计用）──
        static constexpr uint32_t kBinCount = 8;
        static constexpr uint64_t kChunkSize = 64 * 1024;
        static constexpr uint64_t kMaxSlabSize = 64 * 1024;

        static uint64_t BinSize(uint32_t bin);

    private:
        // ── 自旋锁 ──
        // ⚠️ 刻意不用 std::mutex：它需要运行期构造，而后端可能在静态初始化
        //    期就被触达（铁律 1）。自旋锁是平凡类型，静态零初始化即可用。
        struct SpinLock
        {
            void lock();
            void unlock();

        private:
            // 0 = 未锁，1 = 已锁。用 atomic 防止编译器把循环优化掉。
            std::atomic<uint32_t> m_Flag{0};
        };

        // ── 一个 chunk 的登记信息 ──
        struct ChunkRec
        {
            uintptr_t base = 0;
            uint64_t size = 0;
            uint32_t bin = 0;
            uint32_t freeBlocks = 0;

            uint64_t BlockCount() const { return size / BinSize(bin); }
        };

        // ── 一档（固定尺寸）的分配器 ──
        struct Bin
        {
            SpinLock lock;
            void **freeList = nullptr; // ::malloc 数组，存 block 地址
            uint32_t freeCount = 0;    // = freeList 的有效长度
            uint32_t freeCap = 0;      // freeList 的容量（2 的幂，满则翻倍）
            ChunkRec *chunks = nullptr;// ::malloc 数组
            uint32_t chunkCount = 0;
            uint32_t chunkCap = 0;
            uint64_t totalBlocks = 0;
        };

        // 每档一个 bin（与 kBinCount 对应）
        Bin m_Bins[kBinCount];

        // ── 全局 chunk 区间表（给 own 二分用）──
        // 这些内存全是本后端自己 ::malloc 来的，因此落在表内的地址
        // 一定可读，不会段错误。
        struct ChunkIndex
        {
            uintptr_t base = 0;
            uintptr_t end = 0;
        };

        mutable SpinLock m_IndexLock;
        ChunkIndex *m_Index = nullptr;
        uint32_t m_IndexCount = 0;
        uint32_t m_IndexCap = 0;

        // 向 OS 要来的总字节（原子，供统计无锁读取）
        std::atomic<uint64_t> m_Capacity{0};

        // ── 内部实现（调用方持对应锁）──
        static uint32_t BinOf(uint64_t size);
        void *AllocFromBin(uint32_t bin, uintptr_t *newChunkBase,
                           uintptr_t *newChunkEnd);
        void FreeToBin(void *ptr, uintptr_t addr, uint32_t bin,
                       uintptr_t *recycledBase, bool *recycled);
        bool AddChunk(uint32_t bin);

        // chunk 回收：bin 内部状态与索引登记【分两步】，
        // 目的是让 m_IndexLock 永远在 bin 锁之外获取（防 ABBA 死锁）。
        //   DetachChunkNoFree —— 调用方持 bin 锁；摘数组+记账，【不 free】
        //   UnindexChunk     —— 调用方已放掉 bin 锁；摘索引（own 从此答 false）
        // 之后调用方再 RawFree —— 保证"own 答 false"早于"内存归还"。
        void DetachChunkNoFree(uint32_t bin, uint32_t chunkIdx);
        void IndexChunk(uintptr_t base, uintptr_t end);
        void UnindexChunk(uintptr_t base);

        // 按 base 反查 chunk 在 bin.chunks 里的下标。
        // 找不到时返回 chunkCount（调用方须自行判断，别当有效下标用）。
        // ⚠️ 非 static：它要读 m_Bins（static 成员函数没有 this）。
        uint32_t FindChunkIdx(uint32_t bin, uintptr_t base) const;

        // ── 索引维护 ──
        // ⚠️ 这几个函数会拿 m_IndexLock，而 m_IndexLock 必须永远在 bin 锁
        //    【之外】获取。chunk 的增删在 AddChunk/DetachChunkNoFree，
        //    但区间表的登记/摘除由调用方在放掉 bin 锁之后调这里的函数完成 ——
        //    否则 deallocate（先索引锁、后 bin 锁）与它们（先 bin 锁、
        //    后索引锁）就是 ABBA 死锁。详见 .cpp 里的锁顺序铁律。
        void IndexInsert(uintptr_t base, uintptr_t end);
        void IndexRemove(uintptr_t base);
        bool IndexContains(uintptr_t addr) const;

        // ── 纯 ::malloc 的小工具（不能用 STL）──
        static void *RawAlloc(uint64_t bytes);
        static void RawFree(void *p);
    };

} // namespace X_Y
