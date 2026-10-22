// ═════════════════════════════════════════════════════════════════════════════
//  XMemStats.cpp — 内存统计实现
//
//  ⚠️ 本文件【只记账】。不认识后端、不认识分配、不认识对象。
//  ⚠️ 不得使用任何会堆分配的东西（std::string / vector / snprintf 的
//     某些实现等），因为本模块可能在自举期被调用。
//     ToString 里用 ostringstream 是安全的：【它只在用户主动打印时调用】，
//     那时门面早已就绪。
// ═════════════════════════════════════════════════════════════════════════════

#include "Memory/Memory.h"

#include <sstream>

namespace X_Y
{

    // ═════════════════════════════════════════════════════════════════════════
    //  内部工具
    // ═════════════════════════════════════════════════════════════════════════

    uint32_t MemoryCounter::Idx(BackendType t)
    {
        const uint32_t i = static_cast<uint32_t>(t);
        return i < kSlots ? i : 0;
    }

    uint32_t MemoryCounter::SizeIdx(SizeClass c)
    {
        const uint32_t i = static_cast<uint32_t>(c);
        return i < kSizeSlots ? i : 0;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  记账
    // ═════════════════════════════════════════════════════════════════════════

    void MemoryCounter::onAllocate(uint64_t size, BackendType backend)
    {
        slotOf(backend).UsedBytes.fetch_add(size, std::memory_order_relaxed);
        slotOf(backend).AllocCount.fetch_add(1, std::memory_order_relaxed);
        m_TotalAllocs.fetch_add(1, std::memory_order_relaxed);
        m_UsedTotal.fetch_add(size, std::memory_order_relaxed);

        // 大小档维度（定策略的数据依据）
        SizeSlot &sc = sizeSlotOf(ClassifySize(size));
        sc.AllocCount.fetch_add(1, std::memory_order_relaxed);
        const uint64_t curBytes =
            sc.CurrentBytes.fetch_add(size, std::memory_order_relaxed) + size;
        uint64_t scPeak = sc.PeakBytes.load(std::memory_order_relaxed);
        while (curBytes > scPeak &&
               !sc.PeakBytes.compare_exchange_weak(scPeak, curBytes,
                                                   std::memory_order_relaxed))
            ;

        // 总量峰值：比较交换，容忍竞态下的轻微低估
        uint64_t cur = m_UsedTotal.load(std::memory_order_relaxed);
        uint64_t peak = m_PeakTotal.load(std::memory_order_relaxed);
        while (cur > peak &&
               !m_PeakTotal.compare_exchange_weak(peak, cur,
                                                  std::memory_order_relaxed))
            ;
    }

    void MemoryCounter::onDeallocate(uint64_t size, BackendType backend)
    {
        slotOf(backend).UsedBytes.fetch_sub(size, std::memory_order_relaxed);
        slotOf(backend).FreeCount.fetch_add(1, std::memory_order_relaxed);
        m_TotalFrees.fetch_add(1, std::memory_order_relaxed);
        m_UsedTotal.fetch_sub(size, std::memory_order_relaxed);

        SizeSlot &sc = sizeSlotOf(ClassifySize(size));
        sc.FreeCount.fetch_add(1, std::memory_order_relaxed);
        // 钳制：跨基线/异常情况下不让它下溢成天文数字
        const uint64_t cur = sc.CurrentBytes.load(std::memory_order_relaxed);
        sc.CurrentBytes.store(cur > size ? cur - size : 0,
                              std::memory_order_relaxed);
    }

    void MemoryCounter::onCapacityChange(uint64_t capacity, BackendType backend)
    {
        slotOf(backend).CapacityBytes.store(capacity, std::memory_order_relaxed);
    }

    void MemoryCounter::onOverhead(uint64_t bytes)
    {
        m_Overhead.fetch_add(bytes, std::memory_order_relaxed);
    }

    void MemoryCounter::onOOM()
    {
        m_OOMCount.fetch_add(1, std::memory_order_relaxed);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  读取
    // ═════════════════════════════════════════════════════════════════════════

    uint64_t MemoryCounter::UsedBytes() const
    {
        return m_UsedTotal.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::PeakBytes() const
    {
        return m_PeakTotal.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::OverheadBytes() const
    {
        return m_Overhead.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::TotalAllocs() const
    {
        return m_TotalAllocs.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::TotalFrees() const
    {
        return m_TotalFrees.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::OOMCount() const
    {
        return m_OOMCount.load(std::memory_order_relaxed);
    }

    BackendStats MemoryCounter::Get(BackendType t) const
    {
        BackendStats s;
        const Slot &slot = slotOf(t);
        s.UsedBytes = slot.UsedBytes.load(std::memory_order_relaxed);
        s.CapacityBytes = slot.CapacityBytes.load(std::memory_order_relaxed);
        s.AllocCount = slot.AllocCount.load(std::memory_order_relaxed);
        s.FreeCount = slot.FreeCount.load(std::memory_order_relaxed);
        s.LiveCount = s.AllocCount - s.FreeCount;
        return s;
    }

    SizeClassStats MemoryCounter::Get(SizeClass c) const
    {
        SizeClassStats s;
        const SizeSlot &slot = sizeSlotOf(c);
        s.Class = c;
        s.AllocCount = slot.AllocCount.load(std::memory_order_relaxed);
        s.FreeCount = slot.FreeCount.load(std::memory_order_relaxed);
        s.CurrentBytes = slot.CurrentBytes.load(std::memory_order_relaxed);
        s.PeakBytes = slot.PeakBytes.load(std::memory_order_relaxed);
        return s;
    }

    uint64_t MemoryCounter::LiveBlocks() const
    {
        uint64_t blocks = 0;
        for (uint32_t i = 0; i < kSlots; ++i)
        {
            const Slot &slot = m_Slots[i];
            blocks += slot.AllocCount.load(std::memory_order_relaxed) -
                      slot.FreeCount.load(std::memory_order_relaxed);
        }
        return blocks;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  基线
    // ═════════════════════════════════════════════════════════════════════════

    void MemoryCounter::markBaseline()
    {
        m_BaselineUsed = m_UsedTotal.load(std::memory_order_relaxed);
        m_BaselinePeak = m_PeakTotal.load(std::memory_order_relaxed);
        m_BaselineAllocs = m_TotalAllocs.load(std::memory_order_relaxed);
        m_BaselineFrees = m_TotalFrees.load(std::memory_order_relaxed);
        m_EarlyAllocs = m_BaselineAllocs;
        m_HasBaseline.store(true, std::memory_order_relaxed);
    }

    void MemoryCounter::clearBaseline()
    {
        m_HasBaseline.store(false, std::memory_order_relaxed);
    }

    bool MemoryCounter::hasBaseline() const
    {
        return m_HasBaseline.load(std::memory_order_relaxed);
    }

    uint64_t MemoryCounter::UsedBytesRelative() const
    {
        const uint64_t now = m_UsedTotal.load(std::memory_order_relaxed);
        if (!hasBaseline())
            return now;
        return now > m_BaselineUsed ? now - m_BaselineUsed : 0; // 钳到下溢
    }

    uint64_t MemoryCounter::PeakBytesRelative() const
    {
        const uint64_t now = m_PeakTotal.load(std::memory_order_relaxed);
        if (!hasBaseline())
            return now;
        return now > m_BaselinePeak ? now - m_BaselinePeak : 0;
    }

    uint64_t MemoryCounter::LiveBlocksRelative() const
    {
        const uint64_t allocs = m_TotalAllocs.load(std::memory_order_relaxed);
        const uint64_t frees = m_TotalFrees.load(std::memory_order_relaxed);
        if (!hasBaseline())
            return allocs - frees;

        // 跨基线释放会让 (dAllocs) < (dFrees)，此时按 0 处理，
        // 避免出现天文数字（无符号下溢）。
        const uint64_t dAllocs = allocs - m_BaselineAllocs;
        const uint64_t dFrees = frees - m_BaselineFrees;
        return dAllocs > dFrees ? dAllocs - dFrees : 0;
    }

    uint64_t MemoryCounter::EarlyAllocs() const
    {
        return m_EarlyAllocs;
    }

    uint64_t MemoryCounter::PreBaselineFrees() const
    {
        if (!hasBaseline())
            return 0;
        const uint64_t allocs = m_TotalAllocs.load(std::memory_order_relaxed);
        const uint64_t frees = m_TotalFrees.load(std::memory_order_relaxed);
        const uint64_t dAllocs = allocs - m_BaselineAllocs;
        const uint64_t dFrees = frees - m_BaselineFrees;
        return dFrees > dAllocs ? dFrees - dAllocs : 0;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  快照 / 重置 / 打印
    // ═════════════════════════════════════════════════════════════════════════

    MemoryStats MemoryCounter::Snapshot() const
    {
        MemoryStats s;
        s.UsedBytes = UsedBytesRelative();
        s.PeakBytes = PeakBytesRelative();
        s.OverheadBytes = OverheadBytes();
        s.TotalAllocs = TotalAllocs();
        s.TotalFrees = TotalFrees();
        s.LiveBlocks = LiveBlocksRelative();
        s.OOMCount = OOMCount();

        s.HasBaseline = hasBaseline();
        s.EarlyAllocs = m_EarlyAllocs;
        s.PreBaselineFrees = PreBaselineFrees();
        s.BaselineUsedBytes = m_BaselineUsed;

        for (uint32_t i = 0; i < kSlots; ++i)
            s.Backends[i] = Get(static_cast<BackendType>(i));
        for (uint32_t i = 0; i < kSizeSlots; ++i)
            s.SizeClasses[i] = Get(static_cast<SizeClass>(i));
        return s;
    }

    void MemoryCounter::Reset()
    {
        for (uint32_t i = 0; i < kSlots; ++i)
        {
            Slot &slot = m_Slots[i];
            slot.UsedBytes.store(0, std::memory_order_relaxed);
            slot.CapacityBytes.store(0, std::memory_order_relaxed);
            slot.AllocCount.store(0, std::memory_order_relaxed);
            slot.FreeCount.store(0, std::memory_order_relaxed);
        }
        for (uint32_t i = 0; i < kSizeSlots; ++i)
        {
            SizeSlot &sc = m_SizeSlots[i];
            sc.AllocCount.store(0, std::memory_order_relaxed);
            sc.FreeCount.store(0, std::memory_order_relaxed);
            sc.CurrentBytes.store(0, std::memory_order_relaxed);
            sc.PeakBytes.store(0, std::memory_order_relaxed);
        }
        m_UsedTotal.store(0, std::memory_order_relaxed);
        m_PeakTotal.store(0, std::memory_order_relaxed);
        m_Overhead.store(0, std::memory_order_relaxed);
        m_TotalAllocs.store(0, std::memory_order_relaxed);
        m_TotalFrees.store(0, std::memory_order_relaxed);
        m_OOMCount.store(0, std::memory_order_relaxed);
        m_HasBaseline.store(false, std::memory_order_relaxed);
        m_EarlyAllocs = 0;
        m_BaselineUsed = 0;
        m_BaselinePeak = 0;
        m_BaselineAllocs = 0;
        m_BaselineFrees = 0;
    }

    std::string MemoryCounter::ToString() const
    {
        std::ostringstream oss;
        oss << "=== Memory Stats ===\n";
        if (hasBaseline())
        {
            oss << "(baseline applied; early allocs before baseline = "
                << m_EarlyAllocs << ")\n";
        }
        oss << "Used:     " << UsedBytesRelative() << " bytes\n";
        oss << "Peak:     " << PeakBytesRelative() << " bytes\n";
        oss << "Overhead: " << OverheadBytes() << " bytes\n";
        oss << "Allocs:   " << TotalAllocs() << "  Frees: " << TotalFrees() << "\n";
        oss << "Live:     " << LiveBlocksRelative() << " blocks\n";
        if (hasBaseline() && PreBaselineFrees() > 0)
            oss << "PreBaselineFrees: " << PreBaselineFrees() << "\n";
        oss << "OOM:      " << OOMCount() << "\n";

        oss << "-- per backend --\n";
        const BackendType kinds[] = {BackendType::Default, BackendType::Crt,
                                     BackendType::Slab};
        for (BackendType t : kinds)
        {
            const BackendStats s = Get(t);
            if (s.AllocCount == 0 && s.CapacityBytes == 0)
                continue;
            oss << "  " << BackendName(t) << ": used=" << s.UsedBytes
                << " cap=" << s.CapacityBytes << " alloc=" << s.AllocCount
                << " free=" << s.FreeCount << " live=" << s.LiveCount << "\n";
        }

        // 大小档：定策略的数据依据（看程序到底在分配什么尺寸）
        oss << "-- by size class (alloc count) --\n";
        for (uint32_t i = 0; i < kSizeSlots; ++i)
        {
            const SizeClassStats sc = Get(static_cast<SizeClass>(i));
            if (sc.AllocCount == 0)
                continue;
            oss << "  " << SizeClassName(sc.Class) << ": alloc=" << sc.AllocCount
                << " free=" << sc.FreeCount << " cur=" << sc.CurrentBytes
                << " peak=" << sc.PeakBytes << "\n";
        }
        oss << "=====================\n";
        return oss.str();
    }

} // namespace X_Y
