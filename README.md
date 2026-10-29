# Modules

我的代码库。

---
## 项目结构

```
Modules
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
├── GraphicsContext/   #   OpenGL 上下文封装（从 UnSureEnable 恢复的独立模块）
├── APP/               #   应用层内核：Loop + EventBus + Application + DLL 微程序
├── Widget/                   # 🏢 上层应用基础设施（独立 lib）
│   ├── Movement/          #   具体事件类型（窗口 / 键盘 / 鼠标）
│   ├── EventHelpers.h     #   Connect / connect / disConnect 便利函数
│   ├── WindowHost.h       #   窗口生命周期（主窗口 / 托管 / 退出清理）
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
├── vendor/                #   第三方库（自带 CMake 自动接入，接线脚本在 vendor/cmake/）

```
---
