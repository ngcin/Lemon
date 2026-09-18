# 第三方依赖登记（设计文档 07 §0 纪律：新增依赖必须在此登记）

| 依赖 | 版本 | 许可 | 引入方式 | 用途 | 登记时间 |
|---|---|---|---|---|---|
| CPM.cmake | master (vendored `cmake/CPM.cmake`) | MIT | 仓库内 vendored | CMake 包管理 | M0 |
| SDL3 | release-3.2.14 | Zlib | CPM 锁 tag | 窗口/输入/表面 | M0 |
| VulkanMemoryAllocator | v3.4.0 | MIT (版权声明见头文件) | CPM 锁 tag | GPU 内存分配 | M0 |
| EnTT | v3.15.0 | MIT | CPM 锁 tag | ECS（M0-W3 起） | M0 |
| Vulkan headers + loader | 系统（brew / LunarG SDK） | Apache-2.0 / MIT | find_package(Vulkan) | 图形 API | M0 |
| glslangValidator | 系统（brew glslang / SDK） | BSD-ish/GPL 工具链例外 | 构建期外部工具 | GLSL→SPIR-V（产物分发不受其许可影响） | M0 |

## 保留的第三方版权声明

- **VulkanMemoryAllocator (MIT)**：Copyright (c) 2017-2024 Advanced Micro Devices, Inc. — 见
  `vk_mem_alloc.h` 文件头；发布物中需保留该声明（引擎发布时在致谢页聚合）。
- **SDL3 (Zlib)**：Copyright (C) 1993-2025 Sam Lantinga — SDL 以源码/静态库方式链入，
  发布物致谢页保留。
- **EnTT (MIT)**：Copyright (c) 2017-2025 Michele Caini。
- **CPM.cmake (MIT)**：Copyright (c) 2019-2024 Lars Melchior — 构建期工具，不进发布物。

> 设计文档中规划、尚未引入：ImGui（M4）、miniaudio（M5）、glm（M1）、Tracy（M2）、
> nlohmann/json（M2）、steamworks（M7）。引入时在此追加行。
