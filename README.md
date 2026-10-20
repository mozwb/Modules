# X_Y

我的代码库。一个 Windows 独占的 C++20 图形引擎 / 工具包，从零搭建（架构思路受 Hazel 启发，架构已重构）。

---
## 项目结构

```
X_Y/Modules
├── Core/                  # 🔩 核心基础设施（独立 lib）
│   ├── FilesSystem/       #   文件读写 + Win32 对话框
│   ├── Input/             #   键盘/鼠标按键映射
│   ├── Log/               # 🟢 自研彩色日志系统（最成熟）+ DataStoreDevice 后端
│   ├── Memory/            #   Buffer 内存池 + RingBuffer + LoopQueue 环形队列
│   ├── Timer/             #   计时 / 性能分析（含 Ticker 周期线程定时器）
│   ├── XCore/             #   通用核心、文件/内存和 image/audio/video 解码
│   └── XMath/             #   数学库
│
│
├── DataStore/         # 🅱️ 全局数据系统（以 OS 文件系统为库，Buffer=存储格式）
├── Image/             #   图片解析（自研，封装 pngdec）
├── Middle/            #   中间数据结构（MeshData）
│
│
├── GraphicsContext/   #   OpenGL 上下文封装
├── Movement/          #   事件系统（信号槽 + 队列 + 分发器）
├── Widget/                   # 🏢 上层应用基础设施（独立 lib）
│   ├── Application/       #   应用生命周期、消息循环
│   └── Widget/            #   窗口系统（Win32 封装 + XWidget）+ Canvas/Font/DPI 平台抽象
├── UI/                #   自研原生 UI（Container/Component 两层）+ 原生组件
│
│
├── Render/                # 🎨 OpenGL 渲染抽象层
│   └── Render/            #   VAO/VBO/IBO/Shader/Texture/Camera/
├── Model/             #   OBJ 模型加载 + 立方体生成 + GPU 转换
│                           #   FrameBuffer/UniformBuffer + OpenGL 实现
├── Physical/                    # 轻量物理引擎
│
├── MD/                    # 📖 模块文档（Log、Window 等）
├── vendor/                #   第三方库（含 stb_image 和 FFmpeg 源码）

```
---
## 构建

- C++20
- `git clone --recurse-submodules`（已有工作区运行 `git submodule update --init --recursive`）
- cmake -S . -B build -G "MinGW Makefiles"
- cmake --build build

### 图片解码工具

仓库内的 `XYImageTool` 可用于运行解码自检，或检查图片文件的尺寸、通道数、像素数据大小和 FNV-1a 校验值：

- `test/build/XYImageTool.exe --self-test`
- `test/build/XYImageTool.exe path/to/image.png [path/to/another-image.jpg ...]`

自检也已注册到 CTest：配置并构建后运行 `ctest --test-dir build --output-on-failure`。
图片格式由 `vendor/stb/stb_image.h` 提供。

### 目录树导出

`XYToTree` 把目录导出成树形文本，可直接打印到终端或写入文件：

- `test/build/XYToTree.exe`：导出 `Modules` 目录
- `test/build/XYToTree.exe path/to/dir --out tree.txt`：导出指定目录到文件

仓库根目录的 `ToTree.bat` 会自动配置、增量构建并运行该工具：

- `ToTree.bat`
- `ToTree.bat Modules --out tree.txt`

### 音视频解码

`X_Y::Decode::MediaDecoder` 基于 `vendor/FFmpeg` 中的 FFmpeg 源码，可读取文件的音视频流信息并逐帧解码：

- 视频帧以紧密排列的 RGBA8 像素返回。
- 音频帧以交错排列的 float32 PCM 返回，保留源采样率和声道数。
- `MediaFrame::TimestampSeconds` 提供帧时间戳；该 API 不负责播放、同步或 UI。
- `XYMediaTool --self-test` 和 CTest 中的 `XYMediaDecoderSelfTest` 覆盖 WAV 音频解码。

构建会把 FFmpeg 静态编译到 `build/_deps`，因此当前 Windows 构建需要 x64 MinGW 和 Git for Windows（提供 Bash）。为避免依赖 NASM，源码构建关闭了独立 x86 汇编优化。FFmpeg 按 LGPL 2.1-or-later 配置；上游源码及其许可文件位于 `vendor/FFmpeg`。

### UI 可视化测试窗口

- `test/build/XYVisualTest.exe`：打开五 Dock 可视化测试窗口。
- `test/build/XYVisualTest.exe path/to/image.png`：在中心 Dock 预览图片。
