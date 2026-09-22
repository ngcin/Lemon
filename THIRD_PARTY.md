# 第三方依赖登记（设计文档 07 §0 纪律：新增依赖必须在此登记）

| 依赖 | 版本 | 许可 | 引入方式 | 用途 | 登记时间 |
|---|---|---|---|---|---|
| CPM.cmake | master (vendored `cmake/CPM.cmake`) | MIT | 仓库内 vendored | CMake 包管理 | M0 |
| SDL3 | release-3.2.14 | Zlib | CPM 锁 tag | 窗口/输入/表面 | M0 |
| VulkanMemoryAllocator | v3.4.0 | MIT (版权声明见头文件) | CPM 锁 tag | GPU 内存分配 | M0 |
| EnTT | v3.15.0 | MIT | CPM 锁 tag | ECS（M0-W3 起） | M0 |
| Vulkan headers + loader | 系统（brew / LunarG SDK） | Apache-2.0 / MIT | find_package(Vulkan) | 图形 API | M0 |
| glslangValidator | 系统（brew glslang / SDK glslang） | BSD-ish/GPL 工具链例外 | 构建期外部工具 | GLSL→SPIR-V（产物分发不受其许可影响） | M0 |
| nlohmann/json | v3.11.3 | MIT | CPM 锁 tag | JSON（.scene 场景/配置/存档格式，M2 起） | M2 |
| Luma（参照移植源） | 源树对照 | MIT | 结构移植重写（非逐行拷贝） | JobSystem 工作窃取结构（M2）；M1 已参照 RenderableManager | M2 |
| .NET hosting 头（hostfxr.h / coreclr_delegates.h） | 10.0.12 | MIT (.NET Foundation) | vendored（`Engine/Scripting/host/`，源自 dotnet/runtime） | hostfxr 引导（M3；CoreCLRHost.cpp 专用，不出 Scripting 目录） | M3 |
| Dear ImGui（docking 分支） | v1.92.9b-docking | MIT | CPM 锁 tag（`cmake/Dependencies.cmake`；包装目标 `lemon-imgui` 只在 Editor/） | 编辑器 UI（M4；ADR-005；ImGui 头不出 Editor/，`tests/imgui_isolation.cmake` 断言） | M4.0 |
| stb（stb_image / stb_image_write） | master@2c980bb | 公有领域 | CPM 锁 commit | PNG 解码（M4.4 导入器）+ 截屏写盘（编辑器冒烟） | M4.0 |
| RmlUi | 6.3 | MIT | CPM 锁 tag（spike/04-rmlui 消费；**spike 期**——验收过 ADR-008 才转正式依赖） | 运行时 UI（M5 ADR-008 spike：官方 SDL_VK 后端整体验证） | M5 |
| FreeType | 2.14.3（系统 brew） | FreeType License (MIT 兼容) | find_package(Freetype)（RmlUi 依赖） | 字体光栅（RmlUi FreeType 引擎） | M5 |

## 保留的第三方版权声明

- **VulkanMemoryAllocator (MIT)**：Copyright (c) 2017-2024 Advanced Micro Devices, Inc. — 见
  `vk_mem_alloc.h` 文件头；发布物中需保留该声明（引擎发布时在致谢页聚合）。
- **SDL3 (Zlib)**：Copyright (C) 1993-2025 Sam Lantinga — SDL 以源码/静态库方式链入，
  发布物致谢页保留。
- **EnTT (MIT)**：Copyright (c) 2017-2025 Michele Caini。
- **Dear ImGui (MIT)**：Copyright (c) 2014-2025 Omar Cornut — 以源码链入（lemon-imgui
  静态库，仅编辑器目标）；发布物致谢页保留。
- **stb (Public Domain)**：Sean Barrett — 无署名义务，致谢页列出以示尊重。
- **RmlUi (MIT)**：Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd;
  Copyright (c) 2019-2026 The RmlUi Team——spike/04-rmlui 消费其源码（含
  Backends）；正式引入后发布物致谢页保留。
- **CPM.cmake (MIT)**：Copyright (c) 2019-2024 Lars Melchior — 构建期工具，不进发布物。
- **nlohmann/json (MIT)**：Copyright (c) 2013-2024 Niels Lohmann — 头文件库链入，
  发布物致谢页保留。
- **Luma (MIT)**：JobSystem 队列/窃取结构移植自 `Event/JobSystem.{h,cpp}`，源文件头
  保留来源标注；发布物致谢页保留。

> 设计文档中规划、尚未引入：ImGui（M4）、miniaudio（M5）、Tracy（视调优需要）、
> steamworks（M7）。引入时在此追加行。
