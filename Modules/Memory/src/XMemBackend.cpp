// ═════════════════════════════════════════════════════════════════════════════
//  XMemBackend.cpp — SlabBackend 的实现（自研 slab 分配器）
//
//  本文件由旧 Modules/XCore/src/Memory/XMemory.cpp 的 slab 部分迁入，
//  但**不是照抄** —— 四处必须改，否则放不进新后端体系：
//
//    ① std::malloc           → ::malloc
//       铁律 1：后端内部只能用 ::malloc / ::free（或 OS 原语）。
//
//    ② std::vector / std::mutex / std::shared_mutex
//                            → ::malloc 手写数组 + 自旋锁
//       致命的一条：这些容器/锁的【构造/析构】会走全局 operator new，
//       而全局 operator new → 门面 allocate → 后端（就是自己）→ 递归。
//       若 SlabBackend 作为静态对象随程序启动而构造，必然自举死循环。
//       自旋锁只搬指针、临界区极短，且无构造无析构，静态期安全。
//
//    ③ FindChunkByAddr（内部二分）→ own(ptr)（对外接口）
//       同一套区间查询，但门面需要它来判断"这个地址敢不敢读"。
//
//    ④ memset 清零 → 保留（砚台定：与旧行为一致，便于逐位对比新旧实现）
//
//  ⚠️ 改动前必读的三条不变量（继承自旧实现，别踩）：
//    1. free list 存【实际 block 地址】，不存索引 ——
//       chunk 增删（含 chunk 数组重排）不会让待分配的地址失效。
//    2. chunk 能否回收**只能看它自己的 freeBlocks** ——
//       bin 的 freeCount 是所有 chunk 的总和，用它判断是错的。
//    3. 只有 block 全部空闲的 chunk 才还给 OS。
//
//  ⚠️ 本文件禁止出现：new / std::vector / std::string / std::mutex /
//     fprintf 之外的任何会分配的东西。
// ═════════════════════════════════════════════════════════════════════════════

#include "XMemBackend.h"

#include <cstdio>
#include <cstring>

namespace X_Y
{

    // ═════════════════════════════════════════════════════════════════════════
    //  档位表
    // ═════════════════════════════════════════════════════════════════════════

    namespace
    {
        // 8 档固定尺寸（与旧 XMemory.h 的 kSlabSizes 一致，别随手改：
        // 改了会让新旧实现的对比失去意义）
        constexpr uint64_t kBinSizes[SlabBackend::kBinCount] = {
            64, 128, 256, 512, 1024, 4096, 16384, 65536};
    }

    uint64_t SlabBackend::BinSize(uint32_t bin)
    {
        return bin < kBinCount ? kBinSizes[bin] : kBinSizes[kBinCount - 1];
    }

    // 按请求尺寸选档：向上对齐到最近的档；超过最大档的返回最后一档，
    // 由 allocate() 走 ::malloc 兜底（不由 slab 服务）。
    uint32_t SlabBackend::BinOf(uint64_t size)
    {
        for (uint32_t i = 0; i < kBinCount; ++i)
            if (size <= kBinSizes[i])
                return i;
        return kBinCount - 1;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  纯 ::malloc 小工具（不能用 STL，见文件头）
    // ═════════════════════════════════════════════════════════════════════════

    void *SlabBackend::RawAlloc(uint64_t bytes)
    {
        return std::malloc(static_cast<std::size_t>(bytes));
    }

    void SlabBackend::RawFree(void *p)
    {
        std::free(p);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  自旋锁
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  为什么可以自旋：临界区里全是指针搬运 + 计数（纳秒级），
    //  不存在长临界区，也不会在持锁时阻塞在 IO / 系统调用上。

    void SlabBackend::SpinLock::lock()
    {
        for (;;)
        {
            uint32_t expected = 0;
            if (m_Flag.compare_exchange_weak(expected, 1,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed))
                return;
#if defined(__GNUC__) || defined(__clang__)
            __builtin_ia32_pause();
#endif
        }
    }

    void SlabBackend::SpinLock::unlock()
    {
        m_Flag.store(0, std::memory_order_release);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  chunk 区间表（own 用）
    // ═════════════════════════════════════════════════════════════════════════

    // 保持 base 升序，供二分查找。
    // ⚠️ 必须按 base 找位置插入，不能无脑追加：own() 依赖有序性。
    void SlabBackend::IndexInsert(uintptr_t base, uintptr_t end)
    {
        // 满了就翻倍（初始 16 项）
        if (m_IndexCount == m_IndexCap)
        {
            const uint32_t newCap = m_IndexCap ? m_IndexCap * 2 : 16;
            void *raw = RawAlloc(sizeof(ChunkIndex) * newCap);
            if (!raw)
                return; // 索引登记失败：本 chunk 只是"own 答不出来"，不影响分配
            ChunkIndex *grown = static_cast<ChunkIndex *>(raw);
            for (uint32_t i = 0; i < m_IndexCount; ++i)
                grown[i] = m_Index[i];
            RawFree(m_Index);
            m_Index = grown;
            m_IndexCap = newCap;
        }

        // 找第一个 base 更大的位置，把它后面的整体后移一格
        uint32_t pos = 0;
        while (pos < m_IndexCount && m_Index[pos].base < base)
            ++pos;

        for (uint32_t i = m_IndexCount; i > pos; --i)
            m_Index[i] = m_Index[i - 1];

        m_Index[pos].base = base;
        m_Index[pos].end = end;
        ++m_IndexCount;
    }

    void SlabBackend::IndexRemove(uintptr_t base)
    {
        uint32_t pos = 0;
        while (pos < m_IndexCount && m_Index[pos].base != base)
            ++pos;
        if (pos == m_IndexCount)
            return;

        for (uint32_t i = pos; i + 1 < m_IndexCount; ++i)
            m_Index[i] = m_Index[i + 1];
        --m_IndexCount;
    }

    // 二分：找到最后一个 base <= addr 的项，再看是否落在它的区间内。
    bool SlabBackend::IndexContains(uintptr_t addr) const
    {
        uint32_t lo = 0;
        uint32_t hi = m_IndexCount;

        while (lo < hi)
        {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (m_Index[mid].base <= addr)
                lo = mid + 1;
            else
                hi = mid;
        }

        if (lo == 0)
            return false;

        const ChunkIndex &c = m_Index[lo - 1];
        return addr >= c.base && addr < c.end;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  chunk 申请 / 回收
    // ═════════════════════════════════════════════════════════════════════════

    // 调用方持 bin 锁。成功返回 true（bin 的 freeCount 至少 +1）。
    //
    //  ⚠️ 锁顺序铁律（整个后端只有这一条，务必守）：
    //     **持 bin 锁时【绝不】再去拿 m_IndexLock。**
    //     原因：deallocate 的流程是"先 m_IndexLock 查区间 → 放掉 →
    //     再拿 bin 锁"，而 AddChunk/DetachChunkNoFree 是"持 bin 锁时动区间表"。
    //     两种顺序并存就是经典 ABBA 死锁（低负载永远不炸，一上并发就卡死）。
    //     故：chunk 区间表的增删【一律移到 bin 锁外】做，
    //     先把 chunk 准备好、放锁、再登记 —— 见各调用点的写法。
    bool SlabBackend::AddChunk(uint32_t binIdx)
    {
        Bin &bin = m_Bins[binIdx];
        const uint64_t slabSize = BinSize(binIdx);

        // chunk 必须是 slab 尺寸的整数倍（64KB 档时 chunk 就是一块）
        uint64_t chunkSize = kChunkSize;
        if (chunkSize % slabSize != 0)
            chunkSize = slabSize;

        void *mem = RawAlloc(chunkSize);
        if (!mem)
            return false;

        const uintptr_t base = reinterpret_cast<uintptr_t>(mem);
        const uint64_t blockCount = chunkSize / slabSize;

        // ① chunk 数组（bin 自己的）
        if (bin.chunkCount == bin.chunkCap)
        {
            const uint32_t newCap = bin.chunkCap ? bin.chunkCap * 2 : 4;
            void *raw = RawAlloc(sizeof(ChunkRec) * newCap);
            if (!raw)
            {
                RawFree(mem);
                return false;
            }
            ChunkRec *grown = static_cast<ChunkRec *>(raw);
            for (uint32_t i = 0; i < bin.chunkCount; ++i)
                grown[i] = bin.chunks[i];
            RawFree(bin.chunks);
            bin.chunks = grown;
            bin.chunkCap = newCap;
        }

        // ② free list 扩容到能装下本 chunk 的所有 block
        //    （free list 是个栈，用尾部 push/pop）
        if (bin.freeCount + blockCount > bin.freeCap)
        {
            uint32_t newCap = bin.freeCap ? bin.freeCap : kChunkSize / 64;
            while (newCap < bin.freeCount + blockCount)
                newCap *= 2;

            void *raw = RawAlloc(sizeof(void *) * newCap);
            if (!raw)
            {
                RawFree(mem);
                return false;
            }
            void **grown = static_cast<void **>(raw);
            for (uint32_t i = 0; i < bin.freeCount; ++i)
                grown[i] = bin.freeList[i];
            RawFree(bin.freeList);
            bin.freeList = grown;
            bin.freeCap = newCap;
        }

        // ③ 切块：block 地址直接进 free list（不存索引，见不变量 1）
        //    ⚠️ 从后往前压栈，这样第一次取到的是地址最小的那块，
        //       和旧实现 memset 后按顺序使用的观感一致。
        for (uint64_t i = blockCount; i > 0; --i)
            bin.freeList[bin.freeCount++] = reinterpret_cast<void *>(base + (i - 1) * slabSize);

        ChunkRec &rec = bin.chunks[bin.chunkCount++];
        rec.base = base;
        rec.size = chunkSize;
        rec.bin = binIdx;
        rec.freeBlocks = static_cast<uint32_t>(blockCount);

        bin.totalBlocks += blockCount;
        m_Capacity.fetch_add(chunkSize, std::memory_order_relaxed);

        // ④ 登记到全局区间表（own 用）
        //    ⚠️ 见函数头的锁顺序铁律：这里必须出 bin 锁之后再做。
        //       AddChunk 内部【不再】碰 m_IndexLock，登记交给调用方。
        return true;
    }

    // chunk 准备/登记分离：调用方在【释放 bin 锁之后】调本函数。
    // 目的就是让 m_IndexLock 永远在 bin 锁之外被获取（无 ABBA）。
    void SlabBackend::IndexChunk(uintptr_t base, uintptr_t end)
    {
        m_IndexLock.lock();
        IndexInsert(base, end);
        m_IndexLock.unlock();
    }

    // ⚠️ 锁顺序：本函数拆成了两半。
    //    - DetachChunkNoFree：调用方持 bin 锁时做（纯 bin 内部状态）
    //    - UnindexChunk：调用方【放掉 bin 锁之后】做（拿 m_IndexLock）
    //    详见 AddChunk 函数头的锁顺序铁律。
    //
    //  ⚠️ 本函数【不 free】chunk 内存，只摘状态并减容量。
    //     真正 RawFree 由调用方在"摘下索引之后"执行 —— 这样保证
    //     own() 答 false 一定发生在内存归还【之前】。
    void SlabBackend::DetachChunkNoFree(uint32_t binIdx, uint32_t chunkIdx)
    {
        Bin &bin = m_Bins[binIdx];
        const ChunkRec rec = bin.chunks[chunkIdx];
        const uintptr_t base = rec.base;
        const uintptr_t end = base + rec.size;
        const uint64_t blockCount = rec.BlockCount();

        // ① 把这块内存对应的空闲地址从 free list 里摘掉
        //    （不摘的话 free list 上会留下已还给 OS 的地址 → 下次分配拿到野指针）
        uint32_t w = 0;
        for (uint32_t i = 0; i < bin.freeCount; ++i)
        {
            const uintptr_t a = reinterpret_cast<uintptr_t>(bin.freeList[i]);
            if (a >= base && a < end)
                continue; // 丢弃
            bin.freeList[w++] = bin.freeList[i];
        }
        bin.freeCount = w;

        // ② 从 bin 的 chunk 数组摘除（用尾元素填补空位，顺序无所谓）
        bin.chunks[chunkIdx] = bin.chunks[bin.chunkCount - 1];
        --bin.chunkCount;
        bin.totalBlocks -= blockCount;
        m_Capacity.fetch_sub(rec.size, std::memory_order_relaxed);
    }

    // 调用方【不持】bin 锁（本函数自己拿 m_IndexLock）。
    // ⚠️ 必须在 RawFree 之前调用：free 掉之后 own() 可能不再认那块地，
    //    但索引还留着就会让门面误以为"这是自家页"→ 读出垃圾 header。
    void SlabBackend::UnindexChunk(uintptr_t base)
    {
        m_IndexLock.lock();
        IndexRemove(base);
        m_IndexLock.unlock();
    }

    // 按 base 反查下标。找不到返回 chunkCount（哨兵，调用方必须判）。
    // ⚠️ 不是 static：要读 m_Bins。
    uint32_t SlabBackend::FindChunkIdx(uint32_t binIdx, uintptr_t base) const
    {
        const Bin &bin = m_Bins[binIdx];
        for (uint32_t i = 0; i < bin.chunkCount; ++i)
            if (bin.chunks[i].base == base)
                return i;
        return bin.chunkCount;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  分配
    // ═════════════════════════════════════════════════════════════════════════

    // 调用方持 bin 锁。
    // 若本次扩了新 chunk，新 chunk 的 base 通过 newChunkBase 回报给调用方，
    // 由调用方在【放掉 bin 锁之后】去登记区间表（锁顺序铁律）。
    void *SlabBackend::AllocFromBin(uint32_t binIdx, uintptr_t *newChunkBase,
                                    uintptr_t *newChunkEnd)
    {
        Bin &bin = m_Bins[binIdx];

        if (newChunkBase)
            *newChunkBase = 0;

        if (bin.freeCount == 0)
        {
            // 没有空闲 block → 惰性扩一个 chunk
            if (!AddChunk(binIdx))
                return nullptr;

            if (newChunkBase)
            {
                const ChunkRec &fresh = bin.chunks[bin.chunkCount - 1];
                *newChunkBase = fresh.base;
                *newChunkEnd = fresh.base + fresh.size;
            }
        }

        // 从尾部取一块（栈式 free list）
        void *ptr = bin.freeList[--bin.freeCount];

        // 找到该 block 归属的 chunk，维护它自己的 freeBlocks（不变量 2）
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        for (uint32_t i = 0; i < bin.chunkCount; ++i)
        {
            ChunkRec &rec = bin.chunks[i];
            if (addr >= rec.base && addr < rec.base + rec.size)
            {
                --rec.freeBlocks;
                return ptr;
            }
        }

        // 理论到不了：free list 里的地址必属于某个在场 chunk
        std::fprintf(stderr, "[XMem] SlabBackend: free list address orphaned (%p)\n",
                     ptr);
        return nullptr;
    }

    void *SlabBackend::allocate(uint64_t size)
    {
        if (size == 0)
            return nullptr;

        // ⚠️ 门面在调用本函数前已经加了它自己的分配头（32 字节，见
        //    XMemFacade.h 的 kHeaderSize），所以这里收到的 size 是
        //    "用户请求 + 32"。换言之用户请求 64KB 时，实际到这儿的
        //    是 64KB+32 → 超出最大档 → 落到下面的 ::malloc 兜底。
        //    这是刻意的：最多几字节的档位溢出不值得专门腾一档，
        //    但注释写清楚，免得以后看见"64KB 请求没走 slab"时困惑。
        if (size > kMaxSlabSize)
        {
            void *p = RawAlloc(size);
            if (p)
                std::memset(p, 0, static_cast<std::size_t>(size));
            return p;
        }

        const uint32_t binIdx = BinOf(size);
        Bin &bin = m_Bins[binIdx];

        uintptr_t newBase = 0;
        uintptr_t newEnd = 0;

        bin.lock.lock();
        void *ptr = AllocFromBin(binIdx, &newBase, &newEnd);
        bin.lock.unlock();

        // 扩了新 chunk → 现在（bin 锁已放）才登记区间表（锁顺序铁律）
        if (newBase != 0)
            IndexChunk(newBase, newEnd);

        if (!ptr)
            return nullptr;

        // 清零：保留旧实现的行为（砚台定）。
        // ⚠️ 清的是【整个 block】而不是 size —— block 尺寸是档位大小，
        //    这样复用同一块时不会残留上一轮的字节。
        std::memset(ptr, 0, static_cast<std::size_t>(BinSize(binIdx)));
        return ptr;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  释放
    // ═════════════════════════════════════════════════════════════════════════

    void SlabBackend::FreeToBin(void *ptr, uintptr_t addr, uint32_t binIdx,
                                uintptr_t *recycledBase, bool *recycled)
    {
        Bin &bin = m_Bins[binIdx];
        *recycled = false;

        // ① 找到归属 chunk，记账
        ChunkRec *owner = nullptr;
        uint32_t ownerIdx = 0;
        for (uint32_t i = 0; i < bin.chunkCount; ++i)
        {
            ChunkRec &rec = bin.chunks[i];
            if (addr >= rec.base && addr < rec.base + rec.size)
            {
                owner = &rec;
                ownerIdx = i;
                break;
            }
        }

        if (!owner)
        {
            // 不在本档任何 chunk 里 —— 释放了不属于本档的地址
            std::fprintf(stderr,
                         "[XMem] SlabBackend: free of foreign address %p\n", ptr);
            return;
        }

        // ② 地址还回 free list（不变量 1：存地址）
        if (bin.freeCount == bin.freeCap)
        {
            void *raw = RawAlloc(sizeof(void *) * (bin.freeCap * 2));
            if (!raw)
            {
                std::fprintf(stderr,
                             "[XMem] SlabBackend: free list growth failed, "
                             "leaking block %p\n", ptr);
                return;
            }
            void **grown = static_cast<void **>(raw);
            for (uint32_t i = 0; i < bin.freeCount; ++i)
                grown[i] = bin.freeList[i];
            RawFree(bin.freeList);
            bin.freeList = grown;
            bin.freeCap *= 2;
        }
        bin.freeList[bin.freeCount++] = ptr;

        ++owner->freeBlocks;

        // ③ 回收判断：只看【这个 chunk 自己】是否全空（不变量 2、3）
        if (owner->freeBlocks == owner->BlockCount() && bin.chunkCount > 1)
        {
            // ⚠️ 回收要走"摘索引 → 再 free"，而摘索引需要 m_IndexLock，
            //    它必须在 bin 锁之外拿（锁顺序铁律）。所以这里【只登记】，
            //    真正的回收由 deallocate 在放掉 bin 锁之后完成 ——
            //    见 deallocate 里 recycled 分支的顺序说明。
            *recycledBase = owner->base;
            *recycled = true;
            (void)ownerIdx;
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  释放
    // ═════════════════════════════════════════════════════════════════════════

    void SlabBackend::deallocate(void *ptr, uint64_t /*size*/)
    {
        if (!ptr)
            return;

        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

        // 先查归属：不在 chunk 区间表里的，是 allocate 里那条 >64KB 的
        // ::malloc 兜底路径发的（或根本不是本后端的）。
        {
            m_IndexLock.lock();
            const bool mine = IndexContains(addr);
            m_IndexLock.unlock();

            if (!mine)
            {
                RawFree(ptr); // 大块兜底路径 / 非本后端 → 交回标准库
                return;
            }
        }

        // 在表内 → 找出它属于哪一档。
        // ⚠️ block 是【档位对齐】的，不能靠 ptr 反推档位，
        //    必须遍历各 bin 的 chunk 区间（共 8 档，很便宜）。
        for (uint32_t b = 0; b < kBinCount; ++b)
        {
            Bin &bin = m_Bins[b];
            bin.lock.lock();

            bool found = false;
            for (uint32_t i = 0; i < bin.chunkCount; ++i)
            {
                const ChunkRec &rec = bin.chunks[i];
                if (addr >= rec.base && addr < rec.base + rec.size)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                bin.lock.unlock();
                continue;
            }

            uintptr_t recycledBase = 0;
            bool recycled = false;
            FreeToBin(ptr, addr, b, &recycledBase, &recycled);
            bin.lock.unlock();

            // chunk 已全空 → 收尾（此刻已放掉 bin 锁，锁顺序铁律）。
            //
            // ⚠️ 分两步是为了同时满足两条约束：
            //   ① 锁顺序：m_IndexLock 必须在 bin 锁之外拿（防 ABBA）；
            //   ② 顺序：必须"先摘索引、再 RawFree"（见 UnindexChunk 注释），
            //      否则门面可能在 own() 认账但内存已还的窗口里读野 header。
            //
            //    做法：在 bin 锁内把 chunk 从数组摘掉（此刻它已不属于任何
            //    bin，别的线程再也回收不到它），放锁 → 摘索引 →
            //    索引没了之后才真正 free。
            if (recycled)
            {
                bin.lock.lock();
                const uint32_t idx = FindChunkIdx(b, recycledBase);

                if (idx < bin.chunkCount)
                {
                    const uintptr_t base = bin.chunks[idx].base;
                    DetachChunkNoFree(b, idx); // 摘数组 + 记容量，不 free
                    bin.lock.unlock();

                    UnindexChunk(base);        // 先摘索引（own 从此答 false）
                    RawFree(reinterpret_cast<void *>(base)); // 再还内存
                }
                else
                {
                    bin.lock.unlock(); // 已被别的线程收走，无需处理
                }
            }
            return;
        }

        // 在区间表里却找不到归属 bin：索引与 bin 状态不同步（不该发生）
        std::fprintf(stderr,
                     "[XMem] SlabBackend: address %p in chunk index but "
                     "no owning bin\n", ptr);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  归属查询 / 容量
    // ═════════════════════════════════════════════════════════════════════════

    bool SlabBackend::own(void *ptr) const
    {
        if (!ptr)
            return false;

        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

        m_IndexLock.lock();
        const bool mine = IndexContains(addr);
        m_IndexLock.unlock();

        return mine;
    }

    uint64_t SlabBackend::capacity() const
    {
        return m_Capacity.load(std::memory_order_relaxed);
    }

    // ⚠️ 本函数是"程序收尾/全量回收"用，只在单线程收尾场景调用。
    //    顺序刻意做成"先整体清空索引，再逐个 free" ——
    //    这样就不必在持 bin 锁时反复去拿 m_IndexLock（锁顺序铁律），
    //    也不必走 DetachChunkNoFree/UnindexChunk 的两步拆分。
    //    （"先摘索引、再 free"这条顺序依然满足：索引是整张先清掉的）
    void SlabBackend::releaseAll()
    {
        // ① 先清空全局区间表：之后 own() 一律答 false，
        //    门面不会再把这些地址当"自家页"去读 header。
        m_IndexLock.lock();
        RawFree(m_Index);
        m_Index = nullptr;
        m_IndexCount = 0;
        m_IndexCap = 0;
        m_IndexLock.unlock();

        // ② 逐个 bin 回收 chunk（此后不再碰 m_IndexLock）
        for (uint32_t b = 0; b < kBinCount; ++b)
        {
            Bin &bin = m_Bins[b];
            bin.lock.lock();

            for (uint32_t i = 0; i < bin.chunkCount; ++i)
                RawFree(reinterpret_cast<void *>(bin.chunks[i].base));

            RawFree(bin.freeList);
            bin.freeList = nullptr;
            bin.freeCount = 0;
            bin.freeCap = 0;

            RawFree(bin.chunks);
            bin.chunks = nullptr;
            bin.chunkCount = 0;
            bin.chunkCap = 0;
            bin.totalBlocks = 0;

            bin.lock.unlock();
        }

        m_Capacity.store(0, std::memory_order_relaxed);
    }

} // namespace X_Y
