# Memory —— 通用分配器 + Buffer。
#
# 强制写死 STATIC，不受 -DXY_MODULE_LINKAGE 影响：
# src/XMemGlobalNew.cpp 重载了全局 operator new/delete，而全局分配器必须
# 「全进程只有一份」。一旦做成 dll 并被多个模块各自静态链一份，就会回到
# 那个跨堆崩溃（见该文件顶部的实测记录）。
#
# 将来 engine 起来后，模块应当通过 host 服务的 Alloc/Free 使用它，而不是自己链 Memory。
xy_module(Memory STATIC)
