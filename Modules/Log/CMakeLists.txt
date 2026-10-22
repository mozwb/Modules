# Log —— 目前没有 src/，全部 inline 在 Log.h 里（header-only）。
# 换成 dll 没有意义，先固定 INTERFACE；等真要动态装/卸日志后端时再说。
xy_module(Log HEADER_ONLY DEPS Core Timer)
