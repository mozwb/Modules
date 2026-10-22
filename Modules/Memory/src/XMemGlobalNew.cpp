// ═════════════════════════════════════════════════════════════════════════════
//  XMemGlobalNew.cpp — 全局 operator new / delete 重载（★ 现在【只统计】）
//
//  ── 这个文件在干什么（一句话）──
//    把所有 new / delete 都记进统计，但【不接管分配器】：
//      new    →  ::malloc  然后记一笔
//      delete →  销一笔    然后 ::free
//    没有分配头、不挑后端、不问策略。
//
//  ══════════════════════════════════════════════════════════════════════════
//  ⚠️⚠️ 为什么从"接管分配器"退回到"只统计"（改这里前必读）
//  ══════════════════════════════════════════════════════════════════════════
//
//  原设计是让 new 走门面 allocate（加分配头、按策略选后端），期望
//  "所有分配没人能绕过"。在 MinGW 上这【不成立】，实测崩溃：
//
//      msvcrt.dll!free()
//      libstdc++-6.dll!std::filesystem::path 析构
//      X_Y::XPath::operator/ ...
//
//  根因：Windows 上 EXE 与各 DLL 各自绑定符号，而 MinGW 的
//  libstdc++-6.dll 是【独立的一个运行时】。于是出现这类错配：
//    · 内存在 libstdc++ 的堆上分配（它内部的 operator new）
//    · 却通过 EXE 的 operator delete 释放 → 门面没找到分配头
//      → 兜底 std::free(ptr) → 而那是 msvcrt 的 free
//    · 两个堆不同 → 崩
//
//  ★ 关键认识：重载全局 operator new/delete 隐含假设"全进程只有一个堆"。
//    只要存在第二个运行时/DLL 边界，假设就破。这不是某行代码写错了，
//    是这条路本身在 Windows 多运行时环境下走不通。
//
//  所以职责重新划分（砚台拍板）：
//    ① 全局 new/delete  → 只统计（本文件），走原生 malloc/free，跨 DLL 安全
//    ② X_Y::Alloc / Free / AllocFrom / NewFrom / DeleteFrom
//       → 通用分配器，完整门面能力（策略 / 指定后端 / 分配头 / 统计）
//        声明在 XMemFacade.h
//
//  ⚠️⚠️ 成对约定（硬规矩，用错就崩，不救）：
//    · new  发的 → 只能 delete
//    · NewFrom / AllocFrom / Malloc / Alloc 发的 → 只能用
//      DeleteFrom / Free / deallocate
//    两边混用 = 把内存还给错误的堆 = 崩。没有兜底、没有检测、不打算做。
//
//  ══════════════════════════════════════════════════════════════════════════
//  ⚠️ 自举铁律（仍然适用）
//  ══════════════════════════════════════════════════════════════════════════
//    1. 门面（Memory::Instance）必须是平凡对象，否则本文件第一行 new
//       就可能在它构造前触发 → UB。详见 XMemFacade.h 设计要点 3。
//    2. 本文件【不要】在重载内部使用任何会分配的东西
//       （std::string / std::vector / 打印格式化等都要小心）。
//       fprintf(stderr, ...) 本身不分配，可用。
//    3. notifyAlloc / notifyFree 只动统计数字，不会回调 new —— 安全。
//
//  ── 开关 ──
//    Memory::Instance().setEnabled(false) → 连统计也停（分配照常走原生）。
//    出问题时的第一条排查路径：关了不崩 = 问题在门面接管的指针上。
//
//  ── 覆盖范围与边界 ──
//    ✅ new / new[] / delete / delete[] / nothrow 版本 / sized delete
//    ✅ 所有 STL 容器、make_unique、make_shared（都被统计到）
//    ❌ 裸 malloc / calloc / realloc（需要系统钩子，风险大，不做）
//    ⚠️ 只统计【次数与字节】，不控制【内存从哪来】—— 后端/策略请用显式 API
// ═════════════════════════════════════════════════════════════════════════════

#include "Memory/Memory.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
    // 门面是否可用 / 是否启用。
    // ⚠️ 这里刻意【不用】门面的任何成员，只读一个原子开关，
    //    保证即使门面尚未就绪（静态初始化期）也不会崩。
    inline bool FacadeActive()
    {
        return X_Y::Memory::Instance().enabled();
    }

    // 分配失败：标准行为是抛 bad_alloc
    [[noreturn]] inline void ThrowBadAlloc(std::size_t size)
    {
        std::fprintf(stderr,
                     "[XMem] allocation failed: %llu bytes\n",
                     (unsigned long long)size);
        throw std::bad_alloc();
    }

    // 记一笔分配（门面未启用时静默跳过）
    inline void NotifyAlloc(std::size_t size)
    {
        if (FacadeActive())
            X_Y::Memory::Instance().notifyAlloc(size);
    }

    // 销一笔。size == 0 表示调用方（不带 size 的 delete）没给出大小，
    // 此时只减次数（见 Memory::notifyFree 的说明）。
    inline void NotifyFree(std::size_t size)
    {
        if (FacadeActive())
            X_Y::Memory::Instance().notifyFree(size);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  new / new[]
//
//  ★ 注意：这里【不再】调用门面的 allocate。就是原生 malloc + 记账。
//    原因见文件头的"为什么退回只统计"。
// ═════════════════════════════════════════════════════════════════════════════

void *operator new(std::size_t size)
{
    // size 可能为 0（标准允许），标准库应返回一个可 free 的唯一指针
    const std::size_t ask = size ? size : 1;

    void *p = std::malloc(ask);
    if (!p)
        ThrowBadAlloc(size);

    NotifyAlloc(size);
    return p;
}

void *operator new[](std::size_t size)
{
    const std::size_t ask = size ? size : 1;

    void *p = std::malloc(ask);
    if (!p)
        ThrowBadAlloc(size);

    NotifyAlloc(size);
    return p;
}

// ── nothrow 版本：失败返回 nullptr，不抛异常 ──
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    const std::size_t ask = size ? size : 1;

    void *p = std::malloc(ask);
    if (p)
        NotifyAlloc(size);
    return p;
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    const std::size_t ask = size ? size : 1;

    void *p = std::malloc(ask);
    if (p)
        NotifyAlloc(size);
    return p;
}

// ═════════════════════════════════════════════════════════════════════════════
//  delete / delete[]
//
//  ★ 无条件 ::free —— 因为 new 那边就是 ::malloc，同一个 CRT 堆，天然配对。
//
//  ⚠️ 这里【不再】判断"这个指针是不是门面发的"。原因：
//     需要判断归属的那类指针（NewFrom / AllocFrom 发的）现在由
//     显式 API 的调用方负责用 Free / DeleteFrom 释放 —— 见成对约定。
//     混用是编程错误，直接崩，不做兜底（砚台拍板：把路堵死）。
// ═════════════════════════════════════════════════════════════════════════════

void operator delete(void *ptr) noexcept
{
    if (!ptr)
        return;
    NotifyFree(0); // 无 size：只减次数
    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    if (!ptr)
        return;
    NotifyFree(0);
    std::free(ptr);
}

// ── sized delete（C++14）──
//   编译器通常会在【大小可知】时优先调这个版本，于是 size 是准的，
//   统计里的字节数也就准。这不需要分配头、不需要额外内存。
//   ⚠️ 但不保证一定被调用（尤其是跨 DLL 的代码），所以上面那两个
//     不带 size 的版本必须保留。
void operator delete(void *ptr, std::size_t size) noexcept
{
    if (!ptr)
        return;
    NotifyFree(size);
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t size) noexcept
{
    if (!ptr)
        return;
    NotifyFree(size);
    std::free(ptr);
}

// ── nothrow delete ──
void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    operator delete(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    operator delete[](ptr);
}
