# Lemon 引擎 — 主工程说明

**Lemon**：纯 2D 高性能游戏引擎（C++20 + Vulkan 内核 + C# 脚本 + Unity 风格 ECS 编辑器），目标品类 ARPG/塔防/吸血鬼幸存者/增量，坚决不做 3D、不做重物理。当前阶段：**M0–M3 已完成（含 M3.5 anim-smoke 全链基线，2026-09-19）；M4 编辑器 M4.0–M4.8 代码完成（2026-09-22，回归 11/11 + smoke-drag ×10 全绿，收官批见 [docs/EngineDesign/M4.8-Editor-Closeout-Plan.md](./docs/EngineDesign/M4.8-Editor-Closeout-Plan.md)），余 30 分钟零文档走查（用户执行）；下一步 M5**（[08-Development-Roadmap.md](./docs/EngineDesign/08-Development-Roadmap.md)）。

> 工作区根目录的目录地图与参考目录只读红线见根 [`../AGENTS.md`](../AGENTS.md)；本文件是 Lemon 工程内的权威指令。

## 构建与运行（macOS/MoltenVK）

```bash
# 在仓库根（Lemon/）执行
cmake --preset mac && cmake --build --preset mac     # Release；Debug 用 mac-debug
./build/mac/spike/01-triangle/lemon-spike-triangle --frames 120 --validate   # 冒烟+验证层
./build/mac/spike/02-sprites/lemon-spike-sprites --n 100000 --frames 300 --immediate
./build/mac/spike/03-csharp/lemon-spike-csharp
```

工具链（brew，已装）：molten-vk / vulkan-loader / vulkan-headers / vulkan-tools / vulkan-validationlayers / glslang / ninja / dotnet-sdk(10)。CPM 依赖（SDL3 3.2.14、VMA 3.4.0、EnTT 3.15.0）已缓存于 `~/.cache/Lemon-CPM`。

## 硬性纪律（改代码前先对齐）

- **Vulkan 零泄漏**：Vulkan/VMA 类型只准出现在 `Engine/Renderer` 的 `.cpp`；头文件用自有句柄。
- **依赖向下**：编辑器→内核→平台层；C++/C# 边界只有"批量 API + 事件队列"两条通道。
- **新增第三方库**必须登记 `THIRD_PARTY.md` 并在 `docs/EngineDesign/07-Porting-Matrix.md` 加行；禁止 DI 容器。
- **移植红线**（07 文档 §0）：yami/2DGameEngine 禁拷代码（yami 的 MIT 仅意味着素材与 schema 可用，JS 代码无拷贝价值）；Looper 未核实许可只可对照；Luma/MoteurJV/duality 拷代码须保留版权声明。
- 命名：C++ `lemon::`、C# `Lemon.*`；扩展名与 Unity 一致——场景 `.scene`、Prefab `.prefab`、数据资产 `.asset`、烘焙 `.baked`（2026-09-19 定名）；项目清单 `project.lemon`（Godot `project.godot` 同款）；项目状态目录 `.lemon/`（生成物 `manifest.json` 落于此）。

## 本机坑（都踩过，别再踩）

- **网络**：git 全局代理指向 `socks5://127.0.0.1:1080`，代理时开时关；git 操作失败时用 `git -c http.proxy= -c https.proxy=` 绕过，或反向挂 `ALL_PROXY`。raw.githubusercontent / codeload 时通时断。
- **CMake 4.x**：`while()` 条件不接受 `<` 等符号比较（会静默跳过循环），必须用 `LESS` 关键字（见 `cmake/SpvToCpp.cmake`）。
- **SpvToCpp 字节序**：`file(READ HEX)` 是字节序列，拼小端 uint32 每 8 位 hex 需按字节倒序。
- **SDL3 3.2.14**：Vulkan 函数在 `<SDL3/SDL_vulkan.h>`；`SDL_Vulkan_GetInstanceExtensions(Uint32*)` 单次调用直接返回数组；`SDL_Vulkan_CreateSurface` 返回 bool。
- **交换链同步**：present 信号量按交换链图像持有 + acquire 走 fence-only（spike-01 已验证层归零，M1 RHI 沿用）。
- **hostfxr/C# 宿主**：本机 libhostfxr 不导出 `hostfxr_close_handle`（按可选处理）；类库工程不生成 runtimeconfig.json（用模板 `spike/03-csharp/LemonSpike.runtimeconfig.json`）；`load_assembly_and_get_function_pointer` 用托管方法名而非 EntryPoint 名；`MethodHandle.GetFunctionPointer()` 会 pin 可回收 ALC（批量入口用托管委托）；跨 UnmanagedCallersOnly 调用的 ALC 卸载有 pin 遗留（M3 用 DomainManager 常驻托管线程解决，见 `docs/EngineDesign/M0-Go-NoGo.md` 教训 7/8）。
- **验证层第一天就开**：Vulkan 改动默认带 `--validate` 自测；两条实测教训（信号量竞态、UNORM/UINT 与 shader `in uint` 匹配）都靠它抓的。

## 流程约定

- 里程碑出口判据在 `docs/EngineDesign/08-Development-Roadmap.md`，每步必须有**可运行/可量化验收**（"单测全绿≠可用"是 Prowl2D 的教训）；实测数据写入 `docs/EngineDesign/` 对应文档与 `docs/DevLog.md`。
- 推翻既定设计的决策需在 `docs/ADR/`（ADR-009/010 已有）或设计文档内标注修订。
- 本仓库（`Lemon/`）是工作区唯一活跃 git 仓库，`docs/` 与本文件已随仓库版本管理；`Prowl2D/` 等参考目录是独立仓库。未经用户明确要求不要 commit。
