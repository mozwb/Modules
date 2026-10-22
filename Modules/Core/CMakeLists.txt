# Core —— 引擎内建（builtin）：编译进 X_Y.exe，模块通过导出符号或 host 服务使用。
# 不跟随 -DXY_MODULE_LINKAGE，内建就是内建。
xy_module(Core STATIC)
