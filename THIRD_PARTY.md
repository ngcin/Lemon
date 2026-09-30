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
| RmlUi | 6.3 | MIT | CPM 锁 tag（`lemon-engine` 经 `Engine/Renderer/RmlUiBackend.cpp` + `Engine/Ui/UiSubsystem.cpp` 正式消费；spike/04-rmlui 保留为验收壳） | 运行时 UI（M5 ADR-008 spike 三判据验收 → **M6a 批③a 转正式依赖**，ADR-014；自研 RenderInterface over RHI） | M5→M6a③a |
| FreeType | 2.14.3（系统 brew） | FreeType License (MIT 兼容) | find_package(Freetype)（RmlUi 依赖） | 字体光栅（RmlUi FreeType 引擎） | M5 |
| yami-rpg-editor 默认素材（第一批） | arpg-ts-chinese 模板（源树拷贝） | MIT（资产随模板再分发） | `Samples/Assets/yami-dungeon/`（5 精灵表 + 3 clip；06 §7） | 素材包底包（M5 批③起；模板/压测共用） | M5 |
| yami-rpg-editor 音频素材（第二批） | 同上（`Assets/音频/正在使用的音频` + `Assets/UI/标题画面`；文件名去 yami 哈希缀） | MIT（同上） | `demo/svr-test/Assets/Audio/` 8 件（bgm.mp3=watery_cave + 6 wav SE + 1 ogg SE） | M6c 竖切批实测素材（注意：同仓 `音乐/Royalty Free Music Loops OGG` 为 **CC BY 4.0** 不在 MIT 面内，未取用） | M6c 批⓪.5 |
| Noto Sans SC（Regular，SubsetOTF 8.3MB） | notofonts/noto-cjk main（2026-09-28 取） | SIL OFL 1.1 | `Engine/Ui/Fonts/NotoSansSC-Regular.otf` + 同目录 `OFL.txt`（随仓库版本管理） | 引擎 UI 正字（M6a 批③b，ADR-014：RmlUi 主/fallback 字体，系统字体链降级兜底） | M6a③b |
| miniaudio（含 stb_vorbis v1.22） | 0.11.25 | 公有领域（Unlicense）或 MIT-0 双许可择一（全文 `Engine/Audio/thirdparty/LICENSE`；stb_vorbis 亦公有领域） | vendored 四件（`Engine/Audio/thirdparty/`：miniaudio.h 4.1MB + miniaudio.c + stb_vorbis.c（Vorbis 外供件，同包 extras）+ LICENSE；源自 GitHub mackron/miniaudio tag 0.11.25 zip——网络持续阻断 CPM 不可行，用户手备包，ADR-015 M1） | 音频后端：设备/混音/解码（WAV/MP3/FLAC 内建 + Vorbis 经外供 stb_vorbis；烤制期消费，运行时零解码） | M6c 批⓪ |

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
  Copyright (c) 2019-2026 The RmlUi Team——`Engine/Renderer/RmlUiBackend.cpp`（自研
  Vulkan 呈现后端，接入形态参考其官方后端）与 `spike/04-rmlui`（验收壳，含 Backends
  拷贝）消费其源码；发布物致谢页保留。
- **CPM.cmake (MIT)**：Copyright (c) 2019-2024 Lars Melchior — 构建期工具，不进发布物。
- **nlohmann/json (MIT)**：Copyright (c) 2013-2024 Niels Lohmann — 头文件库链入，
  发布物致谢页保留。
- **yami-rpg-editor 素材（MIT）**：Copyright (c) 2025 Yami & Xuran & Contributors ——
  `Samples/Assets/yami-dungeon/`（arpg-ts-chinese 模板 Dungeon Assets 拷贝，文件名去
  yami 哈希缀）；发布物致谢页保留。
- **Noto Sans SC（SIL OFL 1.1）**：Copyright 2014-2021 Adobe，Noto 是 Google Inc. 商标 ——
  `Engine/Ui/Fonts/`（OFL 全文同目录 `OFL.txt`；OFL 要求随字体再分发许可证文本，
  打包线归 M8 落）。
- **miniaudio（Public Domain / MIT-0）**：David Reid — 无署名义务；`Engine/Audio/`
  消费（miniaudio 类型不出 `AudioEngine.cpp`，Pimpl 纪律同 Window/RHI）；致谢页列出
  以示尊重（其内嵌 stb_vorbis 亦为公有领域，Sean Barrett）。
- **Luma (MIT)**：JobSystem 队列/窃取结构移植自 `Event/JobSystem.{h,cpp}`，源文件头
  保留来源标注；发布物致谢页保留。

> 设计文档中规划、尚未引入：Tracy（视调优需要）、
> steamworks（M7）。引入时在此追加行。（miniaudio 已于 M6c 批⓪ 转正式，见上表。）
