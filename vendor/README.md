# vendor

第三方依赖放这里。构建器只认两种接入方式，根 CMake 不需要为每个依赖改代码。

## 1. 第三方自带 CMakeLists.txt

```
vendor/<name>/CMakeLists.txt
```

`cmake/XYVendor.cmake` 会扫描 `vendor/*/CMakeLists.txt`，发现就：

```cmake
add_subdirectory(vendor/<name> ... EXCLUDE_FROM_ALL)
```

例子：`vendor/freetype/CMakeLists.txt`（上游自带 CMake）。

## 2. 仓库自己写接线脚本

子模块（imgui / stb / FFmpeg）不能往里面塞文件，否则父仓库追踪不到。
这类依赖的接线放在：

```
vendor/cmake/<name>.cmake
```

`XYVendor.cmake` 会扫描 `vendor/cmake/*.cmake` 并 `include()`。
脚本自己判断源码在不在、目标要不要建。

当前：

- `vendor/cmake/glad.cmake`   → `xy_vendor_glad`
- `vendor/cmake/glm.cmake`    → `xy_vendor_glm`
- `vendor/cmake/stb.cmake`    → `xy_vendor_stb`
- `vendor/cmake/FFmpeg.cmake` → `xy_vendor_ffmpeg`（预编译静态库 / pkg-config）
- `vendor/cmake/imgui.cmake`  → `xy_vendor_imgui`（Win32 + OpenGL3 backends）

## 加一个新依赖

1. 源码放进 `vendor/<name>/`
2. 有上游 CMake → 什么都不用做，自动 `add_subdirectory`
3. 没上游 CMake → 写 `vendor/cmake/<name>.cmake`，创建 `xy_vendor_<name>` 目标
4. 模块侧只判断 `if(TARGET xy_vendor_<name>)`，不直接猜文件路径
