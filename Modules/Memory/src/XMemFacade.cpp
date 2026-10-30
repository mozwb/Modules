// ═════════════════════════════════════════════════════════════════════════════
//  XMemFacade.cpp — 门面的核心分配/释放实现
//
//  为什么这些实现在 .cpp 而不是头文件里内联：
//    它们是门面的"重活"（分配头维护、预算检查、归属判定、后端分发），
//    篇幅大、细节多，放在头文件里会淹没门面的**接口**，读起来很累。
//    例外：enabled / 命名空间级的 Malloc 等极小路留在头文件内联。
//    Instance() 不内联，定义在下面：它必须全进程只有一份，多 DLL 共用
//    exe 里的单例（否则每个 DLL 的头内联会各生成一份 s_Inst）。
//
//  ⚠️ 自举铁律（改本文件前必读）：
//     本文件不得使用任何会堆分配的东西：
//       std::string / std::vector / std::ostringstream / printf 的某些实现
//     因为它们会走全局 operator new → 回到门面 allocate → 递归。
//     fprintf(stderr, ...) 与 ::malloc / ::free 是安全的。
//     例外：ToString() 里的 ostringstream —— 它只在用户主动打印时调用，
//     那时门面早已就绪，且不在分配路径上。
// ═════════════════════════════════════════════════════════════════════════════

#include "Memory/Memory.h"
#include "XMemBackend.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace X_Y
{

    // 单例唯一定义：放在 exe 里，DLL 通过 import lib 调用同一个函数。
    // 不要挪回头文件 inline —— 多个 DLL 会各生成一份 s_Inst。
    // 调用时机：模块在 main 之后装载，此时 exe 的 CRT 已就绪；
    // 不要在 DllMain / 模块静态初始化期分配。
    Memory &Memory::Instance()
    {
        static Memory s_Inst;
        return s_Inst;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  内部工具
    // ═════════════════════════════════════════════════════════════════════════

    // 解析"这次该走哪个后端"（带 size，让策略能按大小决策）
    BackendType Memory::ResolveWithSize(BackendType t, uint64_t size) const
    {
        // 显式指定 → 单次覆盖，绕过策略
        if (t != BackendType::Default && t != BackendType::Count)
            return t;

        // 策略是裸函数指针：非空即有效，直接调用（不会分配，自举安全）
        AllocPolicy policy = m_Policy.load(std::memory_order_acquire);
        if (policy)
        {
            const BackendType chosen = policy(size);
            if (chosen != BackendType::Default && chosen != BackendType::Count)
                return chosen;
        }
        return m_DefaultBackend;
    }

    IMemoryBackend *Memory::backendFor(BackendType t)
    {
        const uint32_t i = static_cast<uint32_t>(t);
        if (i >= kBackendSlots)
            return nullptr;

        // Crt 后端是平凡类（只有 ::malloc/::free，无状态无构造），
        // 放在函数内 static：首次取用时已就绪，多线程安全。
        static CrtBackend s_Crt;

        // Slab 后端是【有状态】的（chunk 表 / free list），但构造是平凡的
        // （成员全是零初始化的指针与计数，见 SlabBackend 的成员声明）——
        // 它在静态期被取用时不会触发任何分配，故没有自举问题。
        // 内存按需惰性申请（第一次 allocate 才 AddChunk）。
        static SlabBackend s_Slab;

        if (t == BackendType::Crt && m_Backends[i].load(std::memory_order_acquire) == nullptr)
        {
            // 原子发布：多线程同时首次 allocate 时都写同一个值，无害。
            m_Backends[i].store(&s_Crt, std::memory_order_release);
        }
        else if (t == BackendType::Slab &&
                 m_Backends[i].load(std::memory_order_acquire) == nullptr)
        {
            m_Backends[i].store(&s_Slab, std::memory_order_release);
        }
        return m_Backends[i].load(std::memory_order_acquire);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  纯记账入口（★ 只给全局 operator new / delete 用）
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  全局 new/delete 走的是原生 malloc/free（跨 DLL 安全，见
    //  XMemGlobalNew.cpp 文件头），它不写 header、不挑后端。
    //  但"统计所有分配"这个目标还是要的 —— 这两个函数就是那条路
    //  唯一的记账出口。
    //
    //  ⚠️ 只动统计数字，不碰内存、不碰后端、不碰 header。
    //
    //  ⚠️ 后端槽固定记到 Crt：new 走的就是标准库堆，语义上正对应
    //     Crt 这一栏。这样"按后端分布"里 Crt 就代表"普通 new 的量"，
    //     Slab 那栏只反映显式调用的量 —— 数字反而更诚实。

    void Memory::notifyAlloc(uint64_t size)
    {
        if (size == 0)
            return;

        m_Counter.onAllocate(size, BackendType::Crt);
        // ⚠️ 【不】记 onOverhead：new 这条路径没有分配头，
        //    所以不能把那 32 字节算进去（以前会算，是错的）。
    }

    void Memory::notifyFree(uint64_t size)
    {
        // size == 0 表示"释放方没给出大小"（不带 size 的 operator delete
        // 被调用时就是这样）。此时只减次数，不动字节数 —— 宁可字节数偏高，
        // 也不要凭空减掉一个猜出来的值。
        m_Counter.onDeallocate(size, BackendType::Crt);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  归属判决（三连验真）—— 释放与自检共用的唯一判据
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  门面拿到的只有 ptr，必须回答"这是不是我发的"。
    //  ⚠️ 不查任何表、不遍历后端 —— 答案就在 ptr 前面的 header 里。
    //
    //  ⚠️ 读 ptr-32 本身：header 位于 [raw, raw+32)，而 raw = ptr-32
    //     与用户数据在同一块内存里，不会跨到未映射页去。
    //     传外部指针（CRT 的、栈地址）进来时读到的多半是垃圾，
    //     下面三条会把它们判为无效 —— 这正是预期行为。

    namespace
    {
        // 三连验真：这个 header 是不是门面自己写下的？
        // ptr 是用户拿到的指针，必须与 header 里记的 user 完全一致。
        inline bool HeaderValid(const AllocHeader *hdr, void *ptr)
        {
            if (hdr->magic != kAllocMagic)
                return false;
            if (hdr->user != reinterpret_cast<uintptr_t>(ptr))
                return false;
            return hdr->checksum == Checksum(*hdr);
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  归属判定（自检用，不在释放热路径上）
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  判据与 deallocate 完全一致：读 header 做三连验真。
    //  ⚠️ 它【不再】遍历后端问 own()：那是曾经的快路径，已删除 ——
    //     它没防住任何东西（三连验真照样要读同一块内存），
    //     却让每次 delete 都要抢一把全局锁、变成 O(后端数)。

    bool Memory::owns(void *ptr) const
    {
        if (!ptr)
            return false;

        const auto *hdr = reinterpret_cast<const AllocHeader *>(
            static_cast<const uint8_t *>(ptr) - kHeaderSize);
        return HeaderValid(hdr, ptr);
    }

    // ── 后端自检：这个地址属于哪个后端？─────────────────────────────────────
    // 与 owns() 不同，本函数【只问后端自己的区域表】，不读任何 header。
    // 用途：Slab 的 chunk 区间查询、将来"这个指针到底谁的"这类诊断。
    // ⚠️ 它【不在】分配/释放的任何路径上 —— 所以各后端的 own() 可以
    //    放心做得"精确但偏慢"（比如 Slab 要拿索引锁 + 二分）。
    IMemoryBackend *Memory::backendOwning(void *ptr)
    {
        if (!ptr)
            return nullptr;

        for (uint32_t i = 0; i < kBackendSlots; ++i)
        {
            IMemoryBackend *b = m_Backends[i].load(std::memory_order_acquire);
            if (b && b->own(ptr))
                return b;
        }
        return nullptr;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  核心分配
    // ═════════════════════════════════════════════════════════════════════════

    void *Memory::allocate(uint64_t size, BackendType type)
    {
        if (size == 0)
            return nullptr;

        const BackendType resolved = ResolveWithSize(type, size);
        IMemoryBackend *backend = backendFor(resolved);
        if (!backend)
            return nullptr; // 该后端未注册（Default 未配策略且默认后端无效等）

        // ── 预算检查 ──
        if (s_MaxBytes != 0)
        {
            const uint64_t used = m_Counter.UsedBytes();
            const uint64_t need = size + kHeaderSize;
            if (used + need > s_MaxBytes)
            {
                m_Counter.onOOM();
                if (s_OOMAction == OOMAction::Abort)
                {
                    std::fprintf(stderr,
                                 "[XMem] OOM: budget %llu exceeded "
                                 "(used=%llu, need=%llu)\n",
                                 (unsigned long long)s_MaxBytes,
                                 (unsigned long long)used,
                                 (unsigned long long)need);
                    std::terminate();
                }
                if (s_OOMAction == OOMAction::ReturnNull)
                    return nullptr;
                // Expand：继续分配（已记 OOM）
            }
        }

        // ── ① 多要一块 header 的空间 ──
        void *raw = backend->allocate(size + kHeaderSize);
        if (!raw)
        {
            m_Counter.onOOM();
            if (s_OOMAction == OOMAction::Abort)
                std::terminate();
            return nullptr;
        }

        // ── ② 返回 header 之后的用户区 ──
        void *user = static_cast<uint8_t *>(raw) + kHeaderSize;

        // ── ③ 在头部写清"我是谁"（释放时据此找对后端 + 验真）──
        //    ⚠️ 字段顺序有讲究：先把 size/backend/user 填好，最后才算
        //       checksum 并落 magic —— 这样 checksum 覆盖的必然是终值。
        auto *hdr = static_cast<AllocHeader *>(raw);
        hdr->size = size;
        hdr->backend = static_cast<uint8_t>(resolved);
        hdr->user = reinterpret_cast<uintptr_t>(user);
        hdr->checksum = Checksum(*hdr);
        hdr->magic = kAllocMagic;

        // ⚠️ 这里【不再】往任何表里登记 —— 判据已经全在 header 里了。
        //    旧实现有个 OwnedSet::insert，失败还要回滚本次分配；表退役后
        //    这条失败路径自然消失（少一次可能失败的插入，也少一次回滚）。

        m_Counter.onAllocate(size, resolved);
        m_Counter.onOverhead(kHeaderSize);
        return user;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  释放
    // ═════════════════════════════════════════════════════════════════════════

    void Memory::deallocate(void *ptr, uint64_t size)
    {
        if (!ptr)
            return;

        // ── 判决：直接读 header，看"口令"对不对 ──
        // 布局是 [ AllocHeader ][ 用户数据 ]，所以 header 就在 ptr 前面。
        // 判据三条（缺一不可）：
        //   magic    —— 是不是门面写的
        //   user     —— 是不是正好等于 ptr（防指针被改/偏移错）
        //   checksum —— 内部是否自洽（防内存被踩）
        //
        // ⚠️ 这里【不再】先遍历后端问 own()。曾经有过那样一层，已删除：
        //    它是净负债 —— 多抢一把全局锁、让每次 delete 变成 O(后端数)，
        //    却没防住任何东西（下面这三条照样要读同一块内存）。
        //    释放现在是纯粹 O(1)：读 header → 按 backend 字段找后端 → 归还。
        //
        // ⚠️ 本函数现在【只服务显式分配器】（路 B）—— 全局 delete
        //    已不再调用它（delete 现在是纯 ::free，见 XMemGlobalNew.cpp）。
        //    按成对约定，走到这里的指针【本该】都带合法 header。
        //    但下面仍然保留"判不过就交回 ::free"的兜底，因为：
        //      · Free()/Deallocate() 是 public 的，手滑传错指针有可能；
        //      · 判不过时用 ::free 至少不会比"直接崩"更坏。
        //    （这是低成本的保险，不是给混用开的口子 —— 成对约定依然成立）
        void *raw = static_cast<uint8_t *>(ptr) - kHeaderSize;
        auto *hdr = static_cast<AllocHeader *>(raw);

        if (!HeaderValid(hdr, ptr))
        {
            // 不是门面发的 → 交回标准库
            std::free(ptr);
            return;
        }

        // 走到这里 = 三条全过，确实是我发的。
        // 但还要防【重复释放】：归还前会把 magic 擦成 0，所以"已释放过"
        // 在这里表现为 magic 不对（而 user/checksum 仍自洽）。
        // 少了这道检查，同一块会被还两次 → 同一地址在 free list 里出现
        // 两次 → 后续两次分配拿到同一块内存（比崩溃难查得多）。
        if (hdr->magic != kAllocMagic)
        {
            std::fprintf(stderr,
                         "[XMem] deallocate: double free or corrupt header %p\n",
                         ptr);
            return; // 不再归还，避免把同一块还两次
        }

        const uint64_t realSize = hdr->size;
        if (size != 0 && size != realSize)
        {
            std::fprintf(stderr,
                         "[XMem] deallocate: size mismatch %p "
                         "(given=%llu, actual=%llu)\n",
                         ptr, (unsigned long long)size,
                         (unsigned long long)realSize);
        }

        const BackendType bt = static_cast<BackendType>(hdr->backend);
        IMemoryBackend *backend = backendFor(bt);

        // 防重复释放：擦掉魔数，第二次调用就走不到这里。
        hdr->magic = 0;

        m_Counter.onDeallocate(realSize, bt);
        if (backend)
            backend->deallocate(raw, realSize + kHeaderSize);
        else
            std::free(raw); // 理论到不了；兜底不泄漏
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  关闭
    // ═════════════════════════════════════════════════════════════════════════

    void Memory::Shutdown()
    {
        const uint64_t live = liveBlocks();
        if (live != 0)
        {
            std::fprintf(stderr,
                         "[XMem] Shutdown: %llu blocks still alive "
                         "(leak suspected; see Memory::stats())\n",
                         (unsigned long long)live);
        }
        m_Shutdown = true;
    }

} // namespace X_Y
