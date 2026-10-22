#pragma once

/*
Memory.h
version 1.0.5

内存模块唯一公开头：分配头布局、后端枚举、统计、门面（Memory）、
以及两个常用容器（Buffer / BufferView / LoopQueue）。

────────────────────────────────────────────────────────────────
内容
  BackendType / OOMAction / SizeClass   后端与统计维度标签
  MemoryCounter / 统计快照              按后端、按大小档、峰值、未释放块
  AllocHeader / Checksum                分配头（让释放不必记来源）
  Memory                                门面：策略、统计、分配/释放、归属判定
  X_Y::Malloc/Free/New/Delete/NewFrom   门面上的便捷分配入口
  Buffer / BufferView / LoopQueue       二进制容器与环形队列

────────────────────────────────────────────────────────────────
两条路（老规矩，改之前必读）

  路 A：new / delete
        只统计，走原生 ::malloc/::free，不写 header、不问策略。
        跨 DLL 释放安全（见 src/XMemGlobalNew.cpp 文件头）。

  路 B：X_Y::New / X_Y::Delete / Malloc / Free / AllocFrom / NewFrom
        走门面：挑后端、写 header、完整统计。

  ⚠️ 两路不能混：路 A 的指针只能用 delete，路 B 的只能用 X_Y:: 那组。
     混用 = 把内存还给错误的堆 = 崩。不做检测、不做兜底。

────────────────────────────────────────────────────────────────
模块内部

  XMemBackend.h（后端实现：Crt / Slab）是内部头，放在 src/，
  不进公开面，也不被本头 include —— 本头只前置声明 IMemoryBackend*。
────────────────────────────────────────────────────────────────
用法

    #include <Memory/Memory.h>
    using namespace X_Y;

    Memory::Instance().markBaseline();       // main 开头去一次皮

    auto *w  = X_Y::New<Widget>(args...);    // 走策略（路 B）
    auto *sl = X_Y::NewFrom<BackendType::Slab, Widget>(args...);
    X_Y::Delete(w);                          // 释放不必记来源
    X_Y::Delete(sl);

    void *p = X_Y::Malloc(1024);
    X_Y::Free(p);

    Memory::Instance().stats();              // 全量快照
    Memory::Instance().liveBlocks();         // 泄漏自检，应为 0
────────────────────────────────────────────────────────────────
*/

#include <atomic>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 门面只用指针持有后端，所以这里只需要前置声明，
// 不必把 XMemBackend.h（内部头）暴露给使用者。
namespace X_Y
{
    class IMemoryBackend;
}

// ═════════════════════════════════════════════════════════════════════════════
//  XMemTypes.h — 内存模块公共类型（最底层，谁都可以 include）
//
//  只放"大家都要用的枚举/小结构"，不放任何实现。
//  依赖方向：XMemTypes ← XMemBackend ← XMemStats ← XMemFacade
//            （单向，永不反向，避免循环依赖）
//
//  ⚠️ 本文件必须保持零依赖、零分配，它会在自举期被引用。
// ═════════════════════════════════════════════════════════════════════════════


namespace X_Y
{

    // ── 后端类型标签 ─────────────────────────────────────────────────────────
    // 用于 allocate(size, type) 选择后端，也是"哪个后端"的可读名字。
    // 数组下标含义：统计器按本枚举值分槽记账（见 XMemStats.h）。
    enum class BackendType : uint8_t
    {
        Default = 0, // 走门面策略决定的后端（策略为空时 = Crt）
        Crt = 1,     // 标准库堆（malloc/free）—— 默认后端，标准库兜底
        Slab = 2,    // 自研 slab 分配器（已接入；仅显式指定或策略选用）
        Count        // 哨兵：统计槽位数
    };

    inline const char *BackendName(BackendType t)
    {
        switch (t)
        {
        case BackendType::Default:
            return "Default";
        case BackendType::Crt:
            return "Crt";
        case BackendType::Slab:
            return "Slab";
        default:
            return "Unknown";
        }
    }

    // ── OOM 处理策略 ─────────────────────────────────────────────────────────
    // "分配不到内存怎么办"属于分配层策略，门面与后端都要引用，故放最底层。
    enum class OOMAction
    {
        Abort,      // 开发期：超限直接 terminate，尽早暴露问题
        ReturnNull, // 发布版：超限返回 nullptr，调用方自己处理
        Expand,     // 调试期：突破预算继续分配，在统计中标记为超限
    };

    // ── 大小档（统计用）──────────────────────────────────────────────────────
    // ⚠️ 这不是分配器的分档，纯粹是【统计维度】：
    //    记录"程序在各尺寸区间分配了多少次"。
    //    用途：日后定分配策略时的数据依据 ——
    //    先看清程序到底在分配什么尺寸，才能定出有意义的分档策略。
    //
    //    Slab 分配器自己的档位（64B..64KB）在 SlabBackend 里，与本表无关。
    enum class SizeClass : uint8_t
    {
        Tiny = 0,    // ≤ 64B
        Small = 1,   // ≤ 256B
        Medium = 2,  // ≤ 1KB
        Large = 3,   // ≤ 4KB
        Huge = 4,    // ≤ 64KB
        Giant = 5,   // > 64KB
        Count
    };

    inline const char *SizeClassName(SizeClass c)
    {
        switch (c)
        {
        case SizeClass::Tiny:
            return "<=64B";
        case SizeClass::Small:
            return "<=256B";
        case SizeClass::Medium:
            return "<=1KB";
        case SizeClass::Large:
            return "<=4KB";
        case SizeClass::Huge:
            return "<=64KB";
        case SizeClass::Giant:
            return ">64KB";
        default:
            return "?";
        }
    }

    inline SizeClass ClassifySize(uint64_t size)
    {
        if (size <= 64)
            return SizeClass::Tiny;
        if (size <= 256)
            return SizeClass::Small;
        if (size <= 1024)
            return SizeClass::Medium;
        if (size <= 4096)
            return SizeClass::Large;
        if (size <= 65536)
            return SizeClass::Huge;
        return SizeClass::Giant;
    }

} // namespace X_Y


// ═════════════════════════════════════════════════════════════════════════════
//  XMemStats.h — 内存统计（声明）
//
//  职责边界（重要）：
//    本文件【只记账】，不认识后端、不认识分配、不认识对象。
//    它只暴露几个动作：
//      onAllocate(size, backend)   —— 有人分配了，记一笔
//      onDeallocate(size, backend) —— 有人释放了，销一笔
//    内存从哪来、怎么还，全是 XMemBackend.h 的事。
//    实现见 XMemStats.cpp。
//
//  三个必须分清的数字（别混成一个，这是验收泄漏的关键）：
//    UsedBytes  用户用量：真正 allocate 出去的字节。
//               ★ 验收"有没有泄漏"看它回到 0，不看 capacity。
//    Capacity   后端向 OS 要来的总字节。slab 会缓存 chunk 不还给 OS，
//               所以 capacity 不归零是【正常的】，不是泄漏。
//    Overhead   元数据 + header 的开销。调优时看，不影响泄漏判断。
//
//  统计维度（四个，缺一不可）：
//    ① 按后端   —— CRT 用了多少 / Slab 用了多少 → 决定"要不要切"
//    ② 按大小档 —— 各尺寸区间的分配次数 → 日后定策略的数据依据
//    ③ 峰值+当前 —— 看波动（UI 场景会周期性涨落）
//    ④ 未释放块 —— 泄漏自检
//
//  命名区分：
//    MemoryCounter —— 活的计数器（原子内部状态）
//    MemoryStats   —— 只读快照（POD，可安全传阅）
// ═════════════════════════════════════════════════════════════════════════════



namespace X_Y
{

    // ── 单个后端的统计量（快照用 POD）──────────────────────────────────────
    struct BackendStats
    {
        uint64_t UsedBytes = 0;     // 用户用量
        uint64_t CapacityBytes = 0; // 向 OS 要来的容量（0 = 不可知）
        uint64_t AllocCount = 0;    // 分配次数
        uint64_t FreeCount = 0;     // 释放次数
        uint64_t LiveCount = 0;     // 未释放块数（Alloc - Free），正常时 = 0

        // 字面上的"泄漏线索"
        uint64_t LeakedBlocks() const { return LiveCount; }
        uint64_t LeakedBytes() const { return UsedBytes; }
    };

    // ── 单个大小档的统计量（快照用 POD）────────────────────────────────────
    // 记录"该尺寸区间分配了多少次、峰值多少字节"。
    // 用途：定策略前先看清程序在分配什么尺寸（数据驱动，而非拍脑袋）。
    struct SizeClassStats
    {
        SizeClass Class = SizeClass::Tiny;
        uint64_t AllocCount = 0;   // 该区间累计分配次数
        uint64_t FreeCount = 0;    // 该区间累计释放次数
        uint64_t CurrentBytes = 0; // 该区间当前占用字节
        uint64_t PeakBytes = 0;    // 该区间峰值占用字节
    };

    // ── MemoryStats：只读快照（POD，与 Counter 分离）──────────────────────
    // Counter 是"活的"，Stats 是"照下来那一刻的"。拿到 Stats 后可随便
    // 传阅/打印/比较，不受后续分配影响。
    struct MemoryStats
    {
        uint64_t UsedBytes = 0;
        uint64_t PeakBytes = 0;
        uint64_t OverheadBytes = 0;
        uint64_t TotalAllocs = 0;
        uint64_t TotalFrees = 0;
        uint64_t LiveBlocks = 0;
        uint64_t OOMCount = 0;

        // ── 基线（可选）：调过 markBaseline() 后，上面各值已是"相对基线"的 ──
        bool HasBaseline = false;
        uint64_t EarlyAllocs = 0;       // 基线之前发生的分配次数（开机成本）
        uint64_t PreBaselineFrees = 0;  // 跨基线释放次数（基线前内存被回收）
        uint64_t BaselineUsedBytes = 0; // 基线当时的累计用量

        BackendStats Backends[static_cast<uint32_t>(BackendType::Count)];
        SizeClassStats SizeClasses[static_cast<uint32_t>(SizeClass::Count)];

        BackendStats Get(BackendType t) const
        {
            const uint32_t i = static_cast<uint32_t>(t);
            return i < static_cast<uint32_t>(BackendType::Count) ? Backends[i]
                                                                 : BackendStats{};
        }

        SizeClassStats Get(SizeClass c) const
        {
            const uint32_t i = static_cast<uint32_t>(c);
            return i < static_cast<uint32_t>(SizeClass::Count) ? SizeClasses[i]
                                                               : SizeClassStats{};
        }
    };

    // ── MemoryCounter：统计器 ───────────────────────────────────────────────
    // 线程安全（原子计数）。★ 自身零堆分配（固定数组 + 原子），
    // 因此可以在自举期（门面首次 allocate）安全使用，不会递归。
    //
    // 声明与实现分离：所有方法在 XMemStats.cpp。
    class MemoryCounter
    {
    public:
        MemoryCounter() = default;

        MemoryCounter(const MemoryCounter &) = delete;
        MemoryCounter &operator=(const MemoryCounter &) = delete;

        // ── 记账（门面在 allocate/deallocate 成功后调用）──
        // size 传【用户请求的字节数】，不是后端块大小。
        void onAllocate(uint64_t size, BackendType backend);
        void onDeallocate(uint64_t size, BackendType backend);

        // 后端容量上报（后端扩容时调；不可知的后端不调）
        void onCapacityChange(uint64_t capacity, BackendType backend);

        void onOverhead(uint64_t bytes); // header / 元数据
        void onOOM();

        // ── 读取（绝对累计值，不含基线偏移）──
        uint64_t UsedBytes() const;
        uint64_t PeakBytes() const;
        uint64_t OverheadBytes() const;
        uint64_t TotalAllocs() const;
        uint64_t TotalFrees() const;
        uint64_t OOMCount() const;

        BackendStats Get(BackendType t) const;
        SizeClassStats Get(SizeClass c) const;

        // 未释放块总数（绝对；>0 说明有对象没回收）
        uint64_t LiveBlocks() const;

        // ── 基线（统计零点校准 / "去皮"）──
        // 为什么需要：全局 operator new 重载后，程序启动期的分配（全局对象、
        // CRT 自身的环境/locale/stdio 缓冲）也会被统计进来，导致
        //   - main 刚进去 UsedBytes 就不是 0（懵）
        //   - 泄漏自检 liveBlocks()==0 永远不成立（判据失效）
        // 解法：在 main 开头调一次 markBaseline()，之后所有读取都减去基线。
        //
        // ⚠️ 基线【只影响读取】，不影响分配行为。
        // ⚠️ 跨基线释放（基线前分配、基线后释放）会做钳制，不让数字下溢。
        void markBaseline();
        void clearBaseline();
        bool hasBaseline() const;

        // ── 基线相对读取 ──
        uint64_t UsedBytesRelative() const;
        uint64_t PeakBytesRelative() const;
        uint64_t LiveBlocksRelative() const;

        uint64_t EarlyAllocs() const;
        uint64_t PreBaselineFrees() const;

        // ── 快照 / 重置 / 打印 ──
        MemoryStats Snapshot() const;
        void Reset();
        std::string ToString() const;

    private:
        // 槽位按枚举大小固定开。用固定数组而非 vector，
        // 保证统计器本身零堆分配 —— 它要在自举期就能用。
        static constexpr uint32_t kSlots = static_cast<uint32_t>(BackendType::Count);
        static constexpr uint32_t kSizeSlots = static_cast<uint32_t>(SizeClass::Count);

        // ⚠️ Slot 是嵌套类型，所以下面的访问器【不能】也叫 Slot()：
        //    类作用域内 Slot 会先解析成函数名 → "Slot does not name a type"。
        //    故访问器取名 slotOf / sizeSlotOf。
        struct Slot
        {
            std::atomic<uint64_t> UsedBytes{0};
            std::atomic<uint64_t> CapacityBytes{0};
            std::atomic<uint64_t> AllocCount{0};
            std::atomic<uint64_t> FreeCount{0};
        };

        struct SizeSlot
        {
            std::atomic<uint64_t> AllocCount{0};
            std::atomic<uint64_t> FreeCount{0};
            std::atomic<uint64_t> CurrentBytes{0};
            std::atomic<uint64_t> PeakBytes{0};
        };

        static uint32_t Idx(BackendType t);
        static uint32_t SizeIdx(SizeClass c);

        Slot &slotOf(BackendType t) { return m_Slots[Idx(t)]; }
        const Slot &slotOf(BackendType t) const { return m_Slots[Idx(t)]; }

        SizeSlot &sizeSlotOf(SizeClass c) { return m_SizeSlots[SizeIdx(c)]; }
        const SizeSlot &sizeSlotOf(SizeClass c) const { return m_SizeSlots[SizeIdx(c)]; }

        Slot m_Slots[kSlots];
        SizeSlot m_SizeSlots[kSizeSlots];

        std::atomic<uint64_t> m_UsedTotal{0};
        std::atomic<uint64_t> m_PeakTotal{0};
        std::atomic<uint64_t> m_Overhead{0};
        std::atomic<uint64_t> m_TotalAllocs{0};
        std::atomic<uint64_t> m_TotalFrees{0};
        std::atomic<uint64_t> m_OOMCount{0};

        // 基线
        std::atomic<bool> m_HasBaseline{false};
        uint64_t m_BaselineUsed = 0;
        uint64_t m_BaselinePeak = 0;
        uint64_t m_BaselineAllocs = 0;
        uint64_t m_BaselineFrees = 0;
        uint64_t m_EarlyAllocs = 0;
    };

} // namespace X_Y


// ═════════════════════════════════════════════════════════════════════════════
//  XMemFacade.h — 内存门面（Facade）
//
//  这是外界【唯一】要认识的文件。分工：
//      XMemTypes.h       ← 公共枚举（BackendType / OOMAction / SizeClass）
//      XMemBackend.h     ← 后端：字节从哪来、怎么还
//      XMemStats.h       ← 统计：只记账
//      XMemFacade.h      ← 门面：外界入口，选后端、记账、归属判定 ★
//      XMemGlobalNew.cpp ← 全局 operator new/delete 重载（★ 现在只统计）
//
//  ── 门面的两条意义（这是整个模块存在的理由）──
//    ① 统计：所有分配都记账（按后端 / 按大小档 / 峰值 / 未释放块）
//    ② 策略：可注入"这次该走哪个后端"，从而优化内存分配
//    分配器（slab）只是"策略的一种实现"，真正有价值的是这层可插拔的
//    策略 + 统计。
//
//  ── 两条路（各管各的，不要混）──
//
//    路 A：new / delete（XMemGlobalNew.cpp）
//          · 只统计 —— 原生 ::malloc / ::free，不走策略、不写 header
//          · 为什么不能让它接管分配：Windows 上 EXE 与 DLL 各有符号绑定，
//            MinGW 的 libstdc++-6.dll 是独立运行时 → 会"跨堆释放" → 崩。
//            详见 XMemGlobalNew.cpp 文件头。
//
//    路 B：X_Y::* 显式接口（本文件）—— 门面完整能力所在
//          X_Y::New<T> / NewFrom<B,T>    分配 + 构造
//          X_Y::Delete                   析构 + 归还（认 header，不必记来源）
//          X_Y::Malloc / AllocFrom       裸内存
//          X_Y::Free                     裸内存归还
//
//    ⚠️ 一句话规矩：路 A 的指针只能用 delete，路 B 的只能用 X_Y:: 那组。
//       混用 = 把内存还给错误的堆 = 崩。不做检测、不做兜底。
//
//  ── 设计要点（改之前必读）──
//
//  1. 为什么必须有 header（分配头）：
//     释放时（X_Y::Delete / Free）拿到只有 ptr，而 C++ 不会把"当初用哪个
//     后端分配"传回来。所以在 ptr 前面记一份（size / backend / user /
//     checksum），归还时读一眼就知道该找谁。
//     ★ 它的全部价值就是"让使用者不必记住来源"—— 所以路 B 里
//       New 和 NewFrom 共用同一个 Delete，不需要成对的两个名字。
//
//  2. 归属判定靠 header 验真，不靠"已分配指针表"、也不遍历后端：
//     要判断"是不是门面发的"，读 ptr 前面的 header 三条一起看，
//     答案就在 ptr 前面的 header 里，三条一起看：
//       magic（是不是我写的）+ user（是不是正好等于 ptr）+ checksum（自洽）
//     三条全过 = 我发的；任一不过 = 不是我的 → 交回 ::free。
//     故释放是 O(1)：读 header → 按 backend 字段找后端 → 归还。
//     ⚠️ 曾用过哈希表（OwnedSet）登记每个在册指针，已【退役】：
//        header 里的字段本来就能回答这个问题，表是重复存储 + 每次分配/释放
//        的额外开销；旧实现（XMemory.h）也从来没有表。
//     ⚠️ 也【曾】加过"先问各后端 own() 判断是不是自家页"的快路径，同样
//        已删除：它是净负债 —— 让每次 delete 抢一把全局锁、变成 O(后端数)，
//        却没防住任何东西（三连验真照样要读同一块内存）。
//
//  3. 门面必须是平凡可初始化的：
//     new 可能在静态初始化期（甚至更早）被触达，而它要调 notifyAlloc
//     → 门面必须在此之前就是可用的。故门面的成员都设计成
//     "不需要运行期构造"的形态，后端懒就位；策略是裸函数指针。
//
//  4. 自举铁律：
//     后端、统计器、策略存储内部【只用 ::malloc / ::free】，
//     绝不回调门面。否则第一次 allocate 就会死循环。
//     ⚠️ 这条也约束"用户传进来的东西"：策略是函数指针而非 std::function，
//        就是为了杜绝"用户 lambda 的捕获物触发 operator new → 回调门面"。
//
//  ── 用法 ──
//      // ① 普通 new：会被统计，但不受门面管理
//      auto* w = new Widget(args...);
//      delete w;
//
//      // ② 想走门面（策略 / 指定后端 / 完整统计）→ 用 X_Y:: 这一组
//      auto* a = X_Y::New<Widget>(args...);                    // 走策略
//      auto* b = X_Y::NewFrom<BackendType::Slab, Widget>(args...); // 指定后端
//      X_Y::Delete(a);                                         // ★ 都是它
//      X_Y::Delete(b);                                         //   （不必记来源）
//
//      void* p = X_Y::Malloc(1024);                            // 裸内存，走策略
//      void* q = X_Y::AllocFrom(BackendType::Crt, 1024);       // 裸内存，指定后端
//      X_Y::Free(p);
//      X_Y::Free(q);
//
//      // 策略：决定"路 B 里没指定后端的那部分"走哪个后端
//      // ⚠️ 不影响 new（new 已固定走原生 malloc）。
//      // ⚠️ 必须是【无捕获的普通函数】——捕获 lambda 编译不过（见 AllocPolicy）。
//      static BackendType SmallToSlab(uint64_t n)
//      {
//          return n <= 512 ? BackendType::Slab : BackendType::Default;
//      }
//      Memory::Instance().setPolicy(&SmallToSlab);
//
//      // 统计
//      Memory::Instance().markBaseline();   // main 开头调一次
//      Memory::Instance().stats();          // 全量快照
//      Memory::Instance().liveBlocks();     // 泄漏自检（应为 0）
//
//      // 旧名兼容（现有代码如 Buffer 零改动）
//      // ⚠️ 它们走的是【路 B 完整门面】（有 header、按策略选后端），
//      //    所以同样受成对约定约束：Alloc 发的只能用 Free 释放。
//      void* r = Memory::Instance().Alloc(1024);
//      Memory::Instance().Free(r);
//
//      // 排查手段：怀疑"门面接管的指针来自别的堆"时先关掉统计/门面
//      Memory::Instance().setEnabled(false);
// ═════════════════════════════════════════════════════════════════════════════



namespace X_Y
{

    // ── 分配头（AllocHeader）────────────────────────────────────────────────
    // 布局：  [ AllocHeader ][ 用户数据 ... ]
    //                        ↑ 返回给用户的地址
    //
    // 每个分配前面都藏一份，用来回答四件事：
    //   size     —— 原始请求大小（还给后端，并用于统计销账）
    //   backend  —— 该还给哪个后端
    //   user     —— 用户区地址（★ 验真用：见下）
    //   checksum —— ★ 验真用：由 size+backend+user 算出的自校验
    //   magic    —— 这个指针是不是我们发的？
    //
    //  ★ 为什么靠这两个新字段就够了（不再需要"已分配指针表"）：
    //    释放时门面拿到的只有 ptr，它必须回答"这是不是我发的"。
    //    旧方案维护一张哈希表逐个登记指针（每次分配插、释放删、扩容重哈希）；
    //    新方案把答案记在 ptr 前面，前提是"敢读 ptr - kHeaderSize"。
    //    敢不敢读由两步决定（见 XMemFacade.cpp 的 deallocate）：
    //      ① 地址落在已知自管区间内（问各后端 own()）→ 一定是自家页，必可读；
    //      ② 否则读 header 后做三连验真：magic 对 + user 自洽 + checksum 对。
    //    三连全过 = 门面发的；任一不过 = 不是门面发的 → 交回 ::free。
    //    ⚠️ 旧实现（XMemory.h 的 Free）本来就是这么干的 —— 它压根没有表。
    //
    // ⚠️ 对齐：header 必须占整数倍对齐字节，保证后面用户数据仍满足
    //    alignof(max_align_t)。故 kHeaderSize 向上取整到 16。
    //
    // ⚠️ 改动本结构 = 改动门面与释放路径的契约：加字段务必同步 Checksum()。
    struct AllocHeader
    {
        uint64_t size;     // 用户请求的字节数
        uint64_t checksum; // ★ 验真：Checksum(*this)
        uintptr_t user;    // ★ 验真：用户区地址（= raw + kHeaderSize）
        uint32_t magic;    // kAllocMagic 标记
        uint8_t backend;   // BackendType
        uint8_t pad[3];
    };

    inline constexpr uint32_t kAllocMagic = 0x58595F4D;         // 'X','Y','_','M'
    inline constexpr uint64_t kHeaderRaw = sizeof(AllocHeader); // 32
    inline constexpr uint64_t kHeaderSize =
        (kHeaderRaw + 15) & ~static_cast<uint64_t>(15);

    // ── 分配头自校验 ─────────────────────────────────────────────────────────
    // 由 size + backend + user 揉出来的一个值，写进 header->checksum。
    // 用途：捕获"内存被踩""指针被改""跨后端错配"这类 header 内部不自洽的情况。
    //
    // ⚠️ 它【不是】安全哈希，只求便宜 + 能把不自洽暴露出来：
    //    门面上还叠了 magic 与 user 自洽两道，三者一起才构成判决。
    //    所以这里不需要密码学强度，也就不引入任何依赖。
    inline uint64_t Checksum(const AllocHeader &h)
    {
        uint64_t x = h.size;
        x ^= static_cast<uint64_t>(h.backend) * 0x9E3779B97F4A7C15ULL;
        x ^= static_cast<uint64_t>(h.user) + 0x165667B19E3779F9ULL;
        x ^= 0xC2B2AE3D27D4EB4FULL; // 常量种子
        // splitmix64 收尾
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBULL;
        x ^= x >> 31;
        return x;
    }

    // ⚠️ 归属判定没有独立的表：判据就是分配头里的字段（见 AllocHeader 与
    //    deallocate 实现）。所以本类不再持有 OwnedSet。

    // ── Memory：门面 ─────────────────────────────────────────────────────────

    class Memory
    {
    public:
        // ── 单例 ──
        // 门面成员都刻意做成"不需要运行期堆分配"的形态，因此
        // 首次取用时不会触发 allocate，不存在自举递归。
        static Memory &Instance()
        {
            static Memory s_Inst;
            return s_Inst;
        }

        // ═════════════════════════════════════════════════════════════════════
        //  配置
        // ═════════════════════════════════════════════════════════════════════
        void setDefaultBackend(BackendType t) { m_DefaultBackend = t; }
        BackendType defaultBackend() const { return m_DefaultBackend; }

        void setMaxBytes(uint64_t bytes) { s_MaxBytes = bytes; }
        uint64_t maxBytes() const { return s_MaxBytes; }

        void setOOMAction(OOMAction a) { s_OOMAction = a; }
        OOMAction oomAction() const { return s_OOMAction; }

        // ── 一键开关 ──
        // 关闭后完全停掉门面介入：new/delete 连统计也不做（纯原生），
        // 显式分配器不受影响（它本来就是显式调的）。
        // ⚠️ 首要用途是【排查】：怀疑"跨 DLL / 多运行时导致堆不匹配"时，
        //    第一件事就是关掉它 —— 关了不崩 = 问题出在门面介入的那条路上。
        void setEnabled(bool on) { s_Enabled.store(on, std::memory_order_relaxed); }
        bool enabled() const { return s_Enabled.load(std::memory_order_relaxed); }

        // ═════════════════════════════════════════════════════════════════════
        //  策略：决定"这次该走哪个后端"
        // ═════════════════════════════════════════════════════════════════════
        // 输入：本次请求的字节数。输出：该走哪个后端。
        // 返回 Default 表示"没意见，用默认后端"。
        //
        // ⚠️ 策略【只影响分配】。释放永远读分配头里的记录，
        //    所以随时换策略都不会出现"分配走 A、释放走 B"的错配。
        //
        // ⚠️⚠️ 为什么是【裸函数指针】而不是 std::function（改这里前必读）：
        //    std::function 有个小缓冲（SBO），捕获物塞不下时它会调
        //    operator new —— 而本模块【重载了全局 operator new】，
        //    于是这条路径会绕回门面的 allocate，造成自举递归：
        //        用户调 setPolicy(捕获较大的 lambda)
        //          → std::function 构造 → operator new（全局重载）
        //          → Memory::allocate() → 而此刻 m_Policy 尚未就绪 → 递归/UB
        //    这在"首次分配之前"调用时尤其致命（静态初始化期）。
        //    手动 malloc + placement new 也【解决不了】——那只安排了
        //    std::function 对象本身在哪，捕获物照样在堆上。
        //    故策略只能是裸函数指针：无捕获、无状态、绝无堆分配可能。
        //    需要"策略读配置"时，把配置做成门面的成员 + 写个无捕获的
        //    静态函数去读它，而不是让策略去捕获。
        //    （这也把错误挡在编译期：捕获 lambda 会直接不匹配这个类型）
        using AllocPolicy = BackendType (*)(uint64_t);

        void setPolicy(AllocPolicy policy)
        {
            // 裸指针，直接原子存即可 —— 无需分配、无需析构。
            m_Policy.store(policy, std::memory_order_release);
        }

        void clearPolicy() { setPolicy(nullptr); }

        bool hasPolicy() const
        {
            return m_Policy.load(std::memory_order_acquire) != nullptr;
        }

        // ── 统计零点校准（"去皮"）──
        // 在 main 开头调一次：之后统计读取都相对此刻。
        // 目的：启动期的分配（全局对象、CRT 自身）不污染泄漏自检。
        // ⚠️ 只影响读取，不影响分配行为。
        void markBaseline() { m_Counter.markBaseline(); }
        void clearBaseline() { m_Counter.clearBaseline(); }
        bool hasBaseline() const { return m_Counter.hasBaseline(); }

        // ═════════════════════════════════════════════════════════════════════
        //  核心分配 / 释放（STL 风格命名）
        // ═════════════════════════════════════════════════════════════════════

        // 分配 size 字节。（实现见 XMemFacade.cpp）
        //   type == Default → 先问策略，策略没意见则用默认后端
        //   type == 具体后端 → 单次覆盖，绕过策略
        // 失败行为由 OOMAction 决定；Abort 之外统一返回 nullptr。
        //
        // ⚠️ 这是【通用分配器】的入口，不是 new 的入口。
        //    全局 operator new 已改为"只统计"（见 XMemGlobalNew.cpp），
        //    它【不会】走到这里。想被门面完整管理（策略/指定后端/header）
        //    就必须显式用本函数、或 AllocFrom / NewFrom。
        void *allocate(uint64_t size, BackendType type = BackendType::Default);

        // 归还。★ 不需要传后端、不需要传大小 —— 全靠 header。
        //
        // ⚠️⚠️ 成对约定（硬规矩，用错就崩，不救）：
        //    allocate / Alloc / AllocFrom / MallocFrom / NewFrom 发的指针，
        //    【只能】用 deallocate / Free / DeleteFrom 释放。
        //    反过来，new 发的指针必须用 delete（见 XMemGlobalNew.cpp）。
        //    混用 = 把内存还给错误的堆 = 崩。没有兜底、没有检测。
        void deallocate(void *ptr, uint64_t size = 0);

        // ═════════════════════════════════════════════════════════════════════
        //  纯记账入口（★ 只给全局 operator new / delete 用）
        // ═════════════════════════════════════════════════════════════════════
        //  背景：全局 new 已改为"只统计" —— 它用 ::malloc 拿内存、
        //  用 ::free 归还，【不写 header、不挑后端、不问策略】。
        //  这两个函数就是那条路径唯一的记账出口。
        //
        //  ⚠️ 它们【只动统计数字】，不碰任何内存、不碰后端、不碰 header。
        //     所以调用它们绝不会递归回 new（自举安全）。
        //
        //  ⚠️ 为什么需要它们而不是直接调 allocate：
        //     因为 new 必须走原生 malloc（跨 DLL 安全，见 XMemGlobalNew.cpp
        //     文件头）。一旦走门面分配，就等于又接管了分配器。
        //
        //  ⚠️ 这两个也不是给用户调的。用户要分配请用 Alloc / AllocFrom / NewFrom。
        void notifyAlloc(uint64_t size);
        void notifyFree(uint64_t size);

        // 归属判定（自检，也可对外用于诊断）
        // 实现见 .cpp：读 ptr 前面的 header 做三连验真（magic + user + checksum）。
        // ⚠️ 它【不在】任何热路径上 —— 全局 delete 已不再调用它
        //    （delete 现在是纯 ::free，见 XMemGlobalNew.cpp）。
        bool owns(void *ptr) const;

        // 后端自检：这个地址落在哪个后端的自管区域里？没有则返回 nullptr。
        // ⚠️ 与 owns() 不同，它【不读任何 header】，只问后端自己的区域表
        //    （Slab 的 chunk 区间查询）。这是 own() 接口的用途所在。
        // ⚠️ 它不在分配/释放的任何路径上，所以后端的 own() 可以做得
        //    "精确但偏慢"（Slab 要拿索引锁 + 二分）而不影响性能。
        IMemoryBackend *backendOwning(void *ptr);

        // ═════════════════════════════════════════════════════════════════════
        //  对象级便捷接口（分配 + 构造 / 析构 + 归还）
        // ═════════════════════════════════════════════════════════════════════

        // ⚠️ 下面两个 Allocate 重载有【重载解析歧义】的坑：
        //    若 T 的某个构造参数恰好是 BackendType，
        //    编译器会选 (BackendType, Args...) 那版，把用户参数当后端。
        //    新代码请优先用 X_Y::New<T> / X_Y::NewFrom<B,T>（无歧义）。
        //    这两个保留是为了兼容既有调用点。
        template <typename T, typename... Args>
        T *Allocate(BackendType type, Args &&...args)
        {
            void *mem = allocate(sizeof(T), type);
            if (!mem)
                return nullptr;
            try
            {
                return new (mem) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                deallocate(mem, sizeof(T)); // 构造抛异常不泄漏
                throw;
            }
        }

        template <typename T, typename... Args>
        T *Allocate(Args &&...args)
        {
            return Allocate<T>(BackendType::Default, std::forward<Args>(args)...);
        }

        template <typename T>
        void Deallocate(T *ptr)
        {
            if (!ptr)
                return;
            ptr->~T();
            deallocate(ptr, sizeof(T));
        }

        // ═════════════════════════════════════════════════════════════════════
        //  旧名兼容层（现有调用点零改动：Buffer 等）
        // ═════════════════════════════════════════════════════════════════════
        void *Alloc(uint64_t size) { return allocate(size); }
        void Free(void *ptr) { deallocate(ptr, 0); }

        template <typename T, typename... Args>
        T *AllocT(Args &&...args)
        {
            return Allocate<T>(BackendType::Default, std::forward<Args>(args)...);
        }

        template <typename T>
        void FreeT(T *ptr) { Deallocate(ptr); }

        // ═════════════════════════════════════════════════════════════════════
        //  统计
        // ═════════════════════════════════════════════════════════════════════
        MemoryStats stats() const { return m_Counter.Snapshot(); }
        MemoryStats GetStats() const { return m_Counter.Snapshot(); } // 旧名兼容

        uint64_t UsedBytes() const { return m_Counter.UsedBytesRelative(); }
        uint64_t UsedCapacity() const { return m_Counter.PeakBytesRelative(); }
        uint64_t TotalAllocs() const { return m_Counter.TotalAllocs(); }
        uint64_t TotalFrees() const { return m_Counter.TotalFrees(); }
        uint64_t OOMCount() const { return m_Counter.OOMCount(); }

        // ★ 泄漏自检：还活着的块数。正常程序收尾时必须是 0。
        //   （调过 markBaseline 后是相对基线的，判据才准）
        uint64_t liveBlocks() const { return m_Counter.LiveBlocksRelative(); }
        bool hasLeaks() const { return liveBlocks() != 0; }

        // 基线之前发生了多少次分配（= 开机成本，不参与泄漏判断）
        uint64_t earlyAllocs() const { return m_Counter.EarlyAllocs(); }

        // 在册分配数（= 尚未释放的块数）。
        // ⚠️ 旧版这里读的是 OwnedSet 的元素个数；表退役后直接问统计器，
        //    语义不变（"在册"就等于"分配了还没释放"），且跨基线也不会跑偏。
        uint64_t ownedCount() const
        {
            return m_Counter.LiveBlocks();
        }

        bool UnderBudget() const
        {
            return s_MaxBytes == 0 || m_Counter.UsedBytes() < s_MaxBytes;
        }

        std::string ToString() const { return m_Counter.ToString(); }

        // ═════════════════════════════════════════════════════════════════════
        //  后端管理
        // ═════════════════════════════════════════════════════════════════════
        // 注册后端（不接管所有权，调用方自己保证生命周期长于使用期）。
        void registerBackend(BackendType t, IMemoryBackend *backend)
        {
            const uint32_t i = static_cast<uint32_t>(t);
            if (i < kBackendSlots)
                m_Backends[i].store(backend, std::memory_order_release);
        }

        bool hasBackend(BackendType t) const
        {
            const uint32_t i = static_cast<uint32_t>(t);
            if (i >= kBackendSlots)
                return false;
            if (t == BackendType::Crt)
                return true; // Crt 永远可用（静态兜底）
            return m_Backends[i].load(std::memory_order_acquire) != nullptr;
        }

        IMemoryBackend *backendFor(BackendType t);

        // 关闭（程序收尾调用；报告未回收块）。实现见 XMemFacade.cpp
        void Shutdown();

    private:
        Memory() = default;
        ~Memory() = default;
        Memory(const Memory &) = delete;
        Memory &operator=(const Memory &) = delete;

        // 解析"这次该走哪个后端"（带 size，让策略能按大小决策）
        BackendType ResolveWithSize(BackendType t, uint64_t size) const;

        static constexpr uint32_t kBackendSlots =
            static_cast<uint32_t>(BackendType::Count);

        // ⚠️ 成员都刻意保持"不需要运行期构造"的形态
        BackendType m_DefaultBackend = BackendType::Crt;
        bool m_Shutdown = false;
        std::atomic<IMemoryBackend *> m_Backends[kBackendSlots] = {};
        MemoryCounter m_Counter; // 自管（固定数组 + 原子），零堆分配

        // 已分配指针表已退役：归属判定改由 header 字段承担（见 AllocHeader）。
        // 这里曾经有个 OwnedSet m_OwnedTable —— 它是 per-pointer 的重复存储，
        // 而 header 里的 magic/user/checksum 本来就能回答同一个问题。

        // 策略：一个裸函数指针（见 AllocPolicy 的说明）。
        // ⚠️ 以前这里是 std::atomic<AllocPolicy*> —— 指向 malloc 出来的
        //    std::function。改成裸指针后不需要任何分配与析构，
        //    门面成员保持"零运行期构造"，静态初始化期绝对安全。
        std::atomic<AllocPolicy> m_Policy{nullptr};

        // 全局配置（inline static，零初始化）
        inline static uint64_t s_MaxBytes = 0; // 0 = 不限预算
        inline static OOMAction s_OOMAction = OOMAction::Abort;
        inline static std::atomic<bool> s_Enabled{true};
    };

    // ═════════════════════════════════════════════════════════════════════════
    //  路 B：显式分配器（命名空间级便捷函数）
    //
    //  ⚠️ 这一整块是"通用分配器"，替代了你原本期望 new/delete 承担的角色。
    //     全局 new/delete 现在只统计（见 XMemGlobalNew.cpp）。
    //
    //  ⚠️⚠️ 成对约定（硬规矩，用错就崩，不救）：
    //     Malloc / MallocFrom / AllocFrom / NewFrom 发的指针，
    //     【只能】用 Free / Deallocate / DeleteFrom 释放。
    //     不要用 delete，也不要用 C 的 free()。
    //     new 发的指针反过来只能用 delete。
    // ═════════════════════════════════════════════════════════════════════════

    // ── 裸内存：替代 C 的 malloc ────────────────────────────────────────────
    // 走策略（Default 后端），失败返回 nullptr（或按 OOMAction 处理）。
    //
    // ⚠️ 这是【裸内存】—— 没有构造/析构。
    //    要构造对象请用 NewFrom（或 placement new）。
    inline void *Malloc(uint64_t size)
    {
        return Memory::Instance().allocate(size);
    }

    // 释放裸内存。
    // ⚠️ 只用于 Malloc / MallocFrom / AllocFrom 返回的指针。
    //    不要用来释放 new 出来的【对象】（那会跳过析构 → 资源泄漏）。
    //    对象请用 DeleteFrom（或 Memory::Deallocate）。
    inline void Free(void *ptr)
    {
        Memory::Instance().deallocate(ptr, 0);
    }

    // ── 显式指定后端（单次，绕过策略）────────────────────────────────────
    inline void *MallocFrom(BackendType backend, uint64_t size)
    {
        return Memory::Instance().allocate(size, backend);
    }

    inline void *AllocFrom(BackendType backend, uint64_t size)
    {
        return Memory::Instance().allocate(size, backend);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  ★ 对象级：X_Y::New / X_Y::Delete —— 这两个就是"路 B 的 new/delete"
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  为什么要有这两个名字：
    //    new/delete 已经退化成"只统计"（走原生堆，不受门面管理）。
    //    想要门面完整能力（策略 / header / 统计）的分配+构造，
    //    以前只能写 Memory::Instance().Allocate<T>(...) —— 又长又不对称。
    //    这两个命名空间级函数就是它的短名字，形状与 new/delete 对齐。
    //
    //  ⚠️ 和 new/delete 的区别（别混）：
    //      new T / delete p          → 原生堆，只统计，无 header
    //      X_Y::New<T> / X_Y::Delete → 门面堆，走策略，有 header
    //    两者【不能混用】（成对约定）。

    // 分配 + 构造（走策略；策略空则走默认后端 = Crt）
    //   用法：auto* w = X_Y::New<Widget>(args...);
    //
    // ⚠️ 这里【不走】Memory::Allocate 的重载解析，而是直接展开成
    //    "allocate + placement new"。原因：Allocate 有两个重载
    //    —— (BackendType, Args...) 和 (Args...) —— 当 T 的某个构造参数
    //    恰好是 BackendType 时，重载解析会选前者，把用户的参数
    //    当成"指定后端"，静默换后端（且少传一个构造参数）。
    //    直接展开就没有这个歧义：本函数 = 无歧义地"走策略"。
    template <typename T, typename... Args>
    T *New(Args &&...args)
    {
        void *mem = Memory::Instance().allocate(sizeof(T), BackendType::Default);
        if (!mem)
            return nullptr;
        try
        {
            return new (mem) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            Memory::Instance().deallocate(mem, sizeof(T)); // 构造抛异常不泄漏
            throw;
        }
    }

    // ── 对象级释放：X_Y::Delete ─────────────────────────────────────────────
    //   用法：X_Y::Delete(w);
    //
    //  ★ 它是【路 B 唯一的对象释放入口】：
    //      New<T> 发的、NewFrom<B,T> 发的 —— 全都用它释放，不需要区分。
    //      因为它读 header 就知道该还给哪个后端、调哪个析构。
    //      （header 存在的意义就是这个：让使用者【不必记住来源】）
    //
    //  ⚠️ 它和 delete 是两条路，不能混：
    //      delete         → 路 A（原生堆，无 header）
    //      X_Y::Delete    → 路 B（门面堆，有 header）
    //      delete 一个路 B 的指针会崩：路 A 不认识 header。
    //
    //  ⚠️ 这个函数只有"路 B"的语义，不做任何"这是不是门面发的"判断 ——
    //     传进来的必然带 header（成对约定的前提）。
    template <typename T>
    void Delete(T *ptr)
    {
        Memory::Instance().Deallocate(ptr);
    }

    // ── 指定后端：分配 + 构造 ──────────────────────────────────────────────
    //   用法：auto* w = X_Y::NewFrom<BackendType::Slab, Widget>(args...);
    //         X_Y::Delete(w);          // ← 释放用同一个入口，不用记来源
    template <BackendType Backend, typename T, typename... Args>
    T *NewFrom(Args &&...args)
    {
        void *mem = Memory::Instance().allocate(sizeof(T), Backend);
        if (!mem)
            return nullptr;
        try
        {
            return new (mem) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            Memory::Instance().deallocate(mem, sizeof(T)); // 不泄漏
            throw;
        }
    }

    // 与 NewFrom 等价的对象释放入口。
    // ⚠️ 现在它【只是 X_Y::Delete 的别名】—— 保留纯粹是为了兼容既有写法，
    //    新代码直接用 X_Y::Delete 即可（两者行为完全一致）。
    template <typename T>
    void DeleteFrom(T *ptr)
    {
        Memory::Instance().Deallocate(ptr);
    }

} // namespace X_Y

namespace X_Y
{

    struct BufferView;
    // ── Buffer ──
    // 二进制数据容器：支持拷贝/移动/读写/自动扩容
    // 所有内存由 Memory 统一管理（Reserve / Release 走 Memory::Alloc / Memory::Free）
    struct Buffer
    {
        uint8_t *Data = nullptr;
        uint64_t Size = 0;
        uint64_t Capacity = 0;

        Buffer() = default;
        explicit Buffer(uint64_t initialCapacity);

        Buffer(const Buffer &other);
        Buffer(Buffer &&other) noexcept;
        ~Buffer();

        Buffer &operator=(const Buffer &other);
        Buffer &operator=(Buffer &&other) noexcept;

        bool Reserve(uint64_t newCapacity);
        bool Ensure(uint64_t neededSize);
        void Allocate(uint64_t size);
        void Release();
        void ZeroInitialize();

        // ── 模板方法留在头文件 ──
        template <typename T>
        T *As(uint64_t offset = 0)
        {
            assert(offset + sizeof(T) <= (Size > 0 ? Size : Capacity));
            return reinterpret_cast<T *>(Data + offset);
        }

        template <typename T>
        const T *As(uint64_t offset = 0) const
        {
            assert(offset + sizeof(T) <= (Size > 0 ? Size : Capacity));
            return reinterpret_cast<const T *>(Data + offset);
        }

        template <typename T>
        T &Read(uint64_t offset = 0) { return *As<T>(offset); }

        template <typename T>
        const T &Read(uint64_t offset = 0) const { return *As<T>(offset); }

        template <typename T>
        void Write(uint64_t offset, const T &value)
        {
            if (offset > UINT64_MAX - sizeof(T))
                return;
            uint64_t needed = offset + sizeof(T);
            if (needed > Size)
            {
                if (!Ensure(needed))
                    return;
            }
            memcpy(Data + offset, &value, sizeof(T));
            if (needed > Size)
                Size = needed;
        }
        void WriteBytes(uint64_t offset, const void *src, uint64_t byteCount)
        {
            if (byteCount == 0)
                return;
            // 防溢出：offset + byteCount 算术溢出
            if (offset > UINT64_MAX - byteCount)
                return;

            uint64_t needed = offset + byteCount;
            if (needed > Size)
            {
                if (!Ensure(needed))
                    return;
            }
            memcpy(Data + offset, src, byteCount);
            if (needed > Size)
                Size = needed;
        }
        void Append(const void *src, uint64_t len);

        template <typename T>
        void Append(const T &value) { Append(&value, sizeof(T)); }

        void Overwrite(const void *src, uint64_t len);
        void Overwrite(const Buffer &other);
        void Overwrite(std::initializer_list<uint8_t> list);
        void Overwrite(const char *str);

        // 只读视图：不拥有内存的观察者，析构不释放。写请用 BufferWriter。

        BufferView View(uint64_t offset, uint64_t size) const;

        uint8_t &operator[](uint64_t index)
        {
            assert(index < Size);
            return Data[index];
        }

        const uint8_t &operator[](uint64_t index) const
        {
            assert(index < Size);
            return Data[index];
        }

        explicit operator bool() const { return Data != nullptr; }
        Buffer Copy() const { return Buffer(*this); }

        std::string toString() const;

        // 添加视图方法
        /// @param offset 字节偏移
        /// @param elementCount 元素个数；传0自动取到buffer末尾（只取完整T，丢弃尾部不足一个T的零碎字节）
        template <typename T>
        std::span<T> Span(uint64_t offset, uint64_t elementCount = 0)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Only trivially‑copyable POD");
            assert(offset <= Size);

            if (elementCount == 0)
            {
                // 剩余字节
                const uint64_t byteRemain = Size - offset;
                // 最多能容纳多少完整 T
                elementCount = byteRemain / sizeof(T);
            }

            const uint64_t totalBytesNeeded = offset + elementCount * sizeof(T);
            assert(totalBytesNeeded <= Size);

            T *ptr = As<T>(offset);
            return std::span<T>{ptr, elementCount};
        }

        template <typename T>
        std::span<const T> Span(uint64_t offset, uint64_t elementCount = 0) const
        {
            static_assert(std::is_trivially_copyable_v<T>, "Only trivially‑copyable POD");
            assert(offset <= Size);

            if (elementCount == 0)
            {
                const uint64_t byteRemain = Size - offset;
                elementCount = byteRemain / sizeof(T);
            }

            const uint64_t totalBytesNeeded = offset + elementCount * sizeof(T);
            assert(totalBytesNeeded <= Size);

            const T *ptr = As<T>(offset);
            return std::span<const T>{ptr, elementCount};
        }
    };

    // @@ BufferView：不拥有内存的只读视图
    struct BufferView
    {
        const uint8_t *Data = nullptr;
        uint64_t Size = 0;

        BufferView() = default;
        BufferView(const Buffer &buffer, uint64_t offset = 0, uint64_t size = 0)
            : Data(buffer.Data + offset), Size(size == 0 ? (buffer.Size - offset) : size)
        {
            // 保护：offset 不能越过 Size，size 不能越界
            assert(offset <= buffer.Size);
            if (size == 0)
                assert(offset <= buffer.Size);
            else
                assert(offset + size <= buffer.Size);
            (void)buffer;
        }

        template <typename T>
        const T &Read(uint64_t offset = 0) const
        {
            assert(offset + sizeof(T) <= Size);
            return *(const T *)(Data + offset);
        }

        const uint8_t &operator[](uint64_t index) const
        {
            assert(index < Size);
            return Data[index];
        }

        explicit operator bool() const { return Data != nullptr; }

        std::string toString() const;
    };

}
// namespace X_Y


namespace X_Y {

// ─────────────────────────────────────────────────────────────────────────────
//  LoopQueue — 固定容量循环队列（FIFO，满则覆盖最旧）
//
//  定位：
//    - 通用基础设施，不只服务于某一场景
//    - 容量在编译期固定（模板参数 N），底层 std::array 连续内存
//    - 满时 Push 会覆盖最旧元素，头部随覆盖前移
//    - 全局序号单调递增，配合 At() 支持“按序号取最新 N 条”的增量消费
//
//  接口贴合 STL 容器习惯：
//    - Push / operator[] / Size / Capacity / Empty / Clear
//    - begin / end（支持范围 for 与 STL 算法）
//    - At(globalSeq) 增量访问
// ─────────────────────────────────────────────────────────────────────────────

template<class T, size_t N>
class LoopQueue {
    static_assert(N > 0, "LoopQueue requires N > 0");
public:
    using value_type      = T;
    using size_type       = size_t;
    using reference       = T&;
    using const_reference = const T&;

    
    // ── 迭代器（顺序 = 最旧 → 最新，正确处理环形绕回） ──
    
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = T*;
        using reference         = T&;

        iterator(LoopQueue* owner, size_type idx)
            : m_Owner(owner), m_Index(idx) {}

        reference operator*()  const { return m_Owner->operator[](m_Index); }
        pointer   operator->() const { return &m_Owner->operator[](m_Index); }

        iterator& operator++() { ++m_Index; return *this; }
        iterator  operator++(int) { iterator tmp = *this; ++(*this); return tmp; }

        bool operator==(const iterator& o) const {
            return m_Owner == o.m_Owner && m_Index == o.m_Index;
        }
        bool operator!=(const iterator& o) const { return !(*this == o); }

    private:
        LoopQueue*  m_Owner;
        size_type   m_Index;
    };

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

        const_iterator(const LoopQueue* owner, size_type idx)
            : m_Owner(owner), m_Index(idx) {}

        reference operator*()  const { return m_Owner->operator[](m_Index); }
        pointer   operator->() const { return &m_Owner->operator[](m_Index); }

        const_iterator& operator++() { ++m_Index; return *this; }
        const_iterator  operator++(int) { const_iterator tmp = *this; ++(*this); return tmp; }

        bool operator==(const const_iterator& o) const {
            return m_Owner == o.m_Owner && m_Index == o.m_Index;
        }
        bool operator!=(const const_iterator& o) const { return !(*this == o); }

    private:
        const LoopQueue* m_Owner;
        size_type        m_Index;
    };

    iterator       begin()       { return iterator(this, 0); }
    iterator       end()         { return iterator(this, m_Count); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, m_Count); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend()   const { return const_iterator(this, m_Count); }

    // ── 容量 ──

    bool     Empty() const { return m_Count == 0; }
    size_type Size() const { return m_Count; }
    size_type Capacity() const { return N; }
    bool     Full() const { return m_Count == N; }

    // ── 入队：满则覆盖最旧，返回落点引用 ──

    reference Push(const T& item) {
        size_t pos = (m_Head + m_Count) % N;
        if (m_Count >= N) {
            m_Data[pos] = item;
            m_Head = (m_Head + 1) % N;   // 覆盖最旧，头部前移
        } else {
            m_Data[pos] = item;
            m_Count++;
        }
        ++m_Total;
        return m_Data[pos];
    }

    reference Push(T&& item) {
        size_t pos = (m_Head + m_Count) % N;
        if (m_Count >= N) {
            m_Data[pos] = std::move(item);
            m_Head = (m_Head + 1) % N;
        } else {
            m_Data[pos] = std::move(item);
            m_Count++;
        }
        ++m_Total;
        return m_Data[pos];
    }

    // ── 相对访问：0 = 最旧，Size()-1 = 最新 ──

    reference operator[](size_type i) {
        return m_Data[(m_Head + i) % N];
    }
    const_reference operator[](size_type i) const {
        return m_Data[(m_Head + i) % N];
    }

    // ── 全局序号访问：i 为单调递增序号（TotalPushed 语义） ──
    //     仅保证“最新的 min(N, 已入队数) 条”有效，更老的下标可能指向已覆盖数据

    reference At(uint64_t globalSeq) {
        return m_Data[globalSeq % N];
    }
    const_reference At(uint64_t globalSeq) const {
        return m_Data[globalSeq % N];
    }

    
    // 最旧 / 最新

    reference       Front()       { return operator[](0); }
    const_reference Front() const { return operator[](0); }
    reference       Back()        { return operator[](m_Count > 0 ? m_Count - 1 : 0); }
    const_reference Back()  const { return operator[](m_Count > 0 ? m_Count - 1 : 0); }

    // ── 清除 ──
   
    void Clear() {
        m_Head = 0;
        m_Count = 0;
        // 注意：不清 m_Total（全局序号保持单调，避免重复消费）
    }

    // 完全重置（连同序号）
    void Reset() {
        m_Head = 0;
        m_Count = 0;
        m_Total = 0;
    }

    uint64_t TotalPushed() const { return m_Total; }

private:
    std::array<T, N> m_Data;
    size_t   m_Head = 0;    // 最旧元素下标
    size_t   m_Count = 0;   // 当前条数 ≤ N
    uint64_t m_Total = 0;   // 全局入队计数（单调递增）
};

} 
// namespace X_Y
