# Lemon 引擎 — 主工程说明

**Lemon**：纯 2D 高性能游戏引擎（C++20 + Vulkan 内核 + C# 脚本 + Unity 风格 ECS 编辑器），目标品类 ARPG/塔防/吸血鬼幸存者/增量，坚决不做 3D、不做重物理。

**当前阶段（2026-10-08）**：M0–M5、M6a/M6b/M6c、M7a 全部完成——M7a 代码面收官 2026-10-06（独立运行时 `lemon-game` + mac/Win 目录包出包；CI = 手动档门禁）。阶段与出口判据的权威登记 = [08 路线图](./docs/EngineDesign/08-Development-Roadmap.md) §0 总览表。当前活跃段 = **M7c 引擎与编辑器功能段**（批⓪–⑥ 详见 [M7c.md](./docs/Plans/M7c/M7c.md)——⑥ 收口 = SceneMembership/SceneSwitcher/回放扩展，金回放跨版本三档零重录；**批⑦ SDK 门面 ✅ 机器面 2026-10-08**——SceneManager/Scene/LoadSceneMode + LemonBehaviour.DontDestroyOnLoad + sceneLoaded/sceneUnloaded/activeSceneChanged 三事件（同步直推，协议⑤ 时序）+ vtable 尾加 49→56 + SceneSourceHooks 寻址 + ResolveScene 泛化；四项裁决用户拍板"按推荐"：D1 DDOL 根位式（位只标根/清场祖先链——后挂随根幸存、移出死亡） / D2 清场=除 DDOL 系外全清+组 0 泄漏自愈 WARN / D3 事件走 IScriptBackend::SceneEventNotify 非纯虚 + Entry 导出 / D4 script-tests 承 C# 语义（TestSceneSdk 四跳全链含时序断言）+ smoke-scene 四跳承宿主端到端；单测 **34,583**/script-tests **1,802**/ctest 4/4/回归 20/21（唯一红 = bench 负载噪声，stash 基线对照定性 + 复跑 80/81 双绿）/金回放三档 mismatches=0/零警告；实现期真缺口 = F2 初始档案 isLoaded 从未置位（三处补齐）；敞口：编辑器内换场端到端归批⑨ 首批消费、UI 点击换场不可回放（既有，批⑨ 处理），[批文件](./docs/Plans/M7c/2026-10-08-b7-sdk-scene-facade.md)、[DevLog](./docs/DevLog/2026-10-08-m7c-b7-sdk-scene-facade.md)；下一批 = 批⑧ LoadSceneAsync）；执行序 = M7c → M8 → M9 → M7b（发行侧后移，2026-10-07 重排）。用户幸存者游戏与引擎并行开发（`demo/svr-test` 为工作项目，引擎卡点 DevLog 登记）。回归基线：full 21 步（批⑥b 起）/ ctest 4/4 / 单测 34,583 checks / script-tests 1,802 / 系统 21 / bench-survivor fps≥76.5。

**待用户（行动项，勿丢）**：① M7c 批① 血条正式素材落地后观感复验（同名替换 `Assets/bar_bg|fg.png`，[批① 批文件](./docs/Plans/M7c/2026-10-07-b1-fx-presentation-upgrade.md) §5）；② **M7c 批② 音频覆写真人听感**（svr-test 浏览器右键任一音效「音频参数…」设覆写——建议 pickup 节流 150ms/微扰关——进 Play 对比，[批② 批文件](./docs/Plans/M7c/2026-10-07-b2-per-asset-audio-fx.md) §3）；③ W5 真机 GPU（物理机窗口，用户 2026-10-07 拍板再后移不设期，[批⑦ 批文件](./docs/Plans/M7a/2026-10-05-b7-windows-closure.md)）；④ M4.8 走查其余 UX 优化清单待用户细化（资产引用拖放已入 M7c 批④ 候选池，[走查记录](./docs/DevLog/2026-10-07-acceptance-m4-8-editor-walkthrough.md)）；⑤ **M7c 批③ 编辑器 i18n 真人走查**（Window→语言 菜单切中文/English 即时生效，两语言各过一遍主要面板；文案微调直接改 `Editor/Strings/` 下 JSON 免编译，[批③ 批文件](./docs/Plans/M7c/2026-10-08-b3-editor-i18n.md) §4；走查已知项：en 态选帧对话框底行「替换为」按钮越窗——已定位登记 M7c ⑤+ 候选池，非阻断）。已过（2026-10-07）：M4.8 零文档走查初步过、svr-test 真人总成 V1–V3（M7a 全闭，[验收记录](./docs/DevLog/2026-10-07-acceptance-m7a-b8-v1-v3.md)）、M7c 批① Fx 真人走查。

> M5–M7a 各批明细 = 各 `docs/Plans/<里程碑>/` 总览页与 `docs/DevLog/` 条目；本段瘦身前的完整内联历史见 git 历史与[批⓪ 对账表](./docs/Plans/M7c/2026-10-07-b0-engineering-hygiene.md)（2026-10-07 瘦身）。

> 工作区根目录的目录地图与参考目录只读红线见根 [`../AGENTS.md`](../AGENTS.md)；本文件是 Lemon 工程内的权威指令。

## 构建与运行（macOS/MoltenVK）

```bash
# 在仓库根（Lemon/）执行
cmake --preset mac && cmake --build --preset mac     # Release；Debug 用 mac-debug
./build/mac/spike/01-triangle/lemon-spike-triangle --frames 120 --validate   # 冒烟+验证层
./build/mac/spike/02-sprites/lemon-spike-sprites --n 100000 --frames 300 --immediate
./build/mac/spike/03-csharp/lemon-spike-csharp
```

工具链（brew，已装）：molten-vk / vulkan-loader / vulkan-headers / vulkan-tools / vulkan-validationlayers / glslang / ninja / dotnet-sdk(10)。CPM 依赖（SDL3 3.2.14、VMA 3.4.0、EnTT 3.15.0、FreeType VER-2-14-3）已缓存于 `~/.cache/Lemon-CPM`。

编译缓存（M7c 批⓪ T3）：mac 侧 ccache（`brew install ccache`；win 侧 sccache `winget install Mozilla.sccache`）——configure 自动探测挂载，探测不到只 WARNING 不阻塞；win-ci preset 显式旁路。首次建议 `ccache -M 10G`。

Windows 真机（批⑦ 首编）：`cmake --preset win && cmake --build --preset win`（VS2022 x64 多配置；前置 = Vulkan SDK + .NET SDK 10，spikes 已 OFF，静态 CRT 免 VC redist）；ctest 用 `-C Release`；出包 `lemon-packager --project <proj> --runtime build/win --out <pkg>`；**真机清单 W1–W7 见 [DevLog 2026-10-05 批⑦](./docs/DevLog/2026-10-05-m7a-b7-windows-closure.md) §4**。

## 硬性纪律（改代码前先对齐）

- **Vulkan 零泄漏**：Vulkan/VMA 类型只准出现在 `Engine/Renderer` 的 `.cpp`；头文件用自有句柄。
- **依赖向下**：编辑器→内核→平台层；C++/C# 边界只有"批量 API + 事件队列"两条通道。
- **新增第三方库**必须登记 `THIRD_PARTY.md` 并在 `docs/EngineDesign/07-Porting-Matrix.md` 加行；禁止 DI 容器。
- **vendored 第三方大文件禁整读**：`Engine/Audio/thirdparty/`（miniaudio.h ≈4MB、stb_vorbis.c ≈190KB）等巨型单文件**绝不整文件读取**——一轮就烧掉大量 token；需要查符号时先 grep 定位行号再局部读，音频行为看 `MiniAudioImpl.c` 封装层与 [ADR-015](./docs/ADR/ADR-015-Audio-System-And-Baked-Format.md)。`spike/` 内 vendored 大文件（vk_mem_alloc.h、vulkan.h 等）同规。
- **移植红线**（07 文档 §0）：yami/2DGameEngine 禁拷代码（yami 的 MIT 仅意味着素材与 schema 可用，JS 代码无拷贝价值）；Looper 未核实许可只可对照；Luma/MoteurJV/duality 拷代码须保留版权声明。
- 命名：C++ `lemon::`、C# `Lemon.*`；扩展名与 Unity 一致——场景 `.scene`、Prefab `.prefab`、数据资产 `.asset`、烘焙 `.baked`（2026-09-19 定名）；项目清单 `project.lemon`（Godot `project.godot` 同款）；项目状态目录 `.lemon/`（生成物 `manifest.json` 落于此）。

## 本机坑（都踩过，别再踩）

- **网络**：git 全局代理指向 `socks5://127.0.0.1:1080`，代理时开时关；git 操作失败时用 `git -c http.proxy= -c https.proxy=` 绕过，或反向挂 `ALL_PROXY`。raw.githubusercontent / codeload 时通时断。
- **CMake 4.x**：`while()` 条件不接受 `<` 等符号比较（会静默跳过循环），必须用 `LESS` 关键字（见 `cmake/SpvToCpp.cmake`）。
- **SpvToCpp 字节序**：`file(READ HEX)` 是字节序列，拼小端 uint32 每 8 位 hex 需按字节倒序。
- **SDL3 3.2.14**：Vulkan 函数在 `<SDL3/SDL_vulkan.h>`；`SDL_Vulkan_GetInstanceExtensions(Uint32*)` 单次调用直接返回数组；`SDL_Vulkan_CreateSurface` 返回 bool。
- **交换链同步**：present 信号量按交换链图像持有 + acquire 走 fence-only（spike-01 已验证层归零，M1 RHI 沿用）。
- **hostfxr/C# 宿主**：本机 libhostfxr 不导出 `hostfxr_close_handle`（按可选处理）；类库工程不生成 runtimeconfig.json（用模板 `spike/03-csharp/LemonSpike.runtimeconfig.json`）；`load_assembly_and_get_function_pointer` 用托管方法名而非 EntryPoint 名；`MethodHandle.GetFunctionPointer()` 会 pin 可回收 ALC（批量入口用托管委托）；跨 UnmanagedCallersOnly 调用的 ALC 卸载有 pin 遗留（M3 用 DomainManager 常驻托管线程解决，见 `docs/Reports/2026-09-18-m0-go-no-go.md` 教训 7/8）。
- **验证层第一天就开**：Vulkan 改动默认带 `--validate` 自测；两条实测教训（信号量竞态、UNORM/UINT 与 shader `in uint` 匹配）都靠它抓的。
- **lemon-game --validate 的层解析坑（2026-10-04，机器级）**：brew 验证层清单 `library_path` 是裸文件名（`/usr/local/share/vulkan/explicit_layer.d/`），本机新版 dyld 默认回退表已不含 `/usr/local/lib` → lemon-game 的层 dlopen 稳定失败（`VK_ERROR_LAYER_NOT_PRESENT`，`RHI.cpp:273` 断言；lemon-editor 同机不受影响、机制未完全定位）。绕行：`DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib ./build/mac/Engine/Entry/lemon-game … --validate`（实测验证层干净）。另：宿主装配序**设备创建须先于 ScriptHost**（CoreCLR 装载扰 dyld 回退搜索）。

## 流程约定

- 里程碑出口判据在 `docs/EngineDesign/08-Development-Roadmap.md`，每步必须有**可运行/可量化验收**（"单测全绿≠可用"是 Prowl2D 的教训）；实测数据写入 `docs/EngineDesign/` 对应分册，事件流水在 `docs/DevLog/` **新增条目文件**（`YYYY-MM-DD-<slug>.md`，一条目一文件，不追加旧文件）。
- **文档组织**：五区制（`EngineDesign/` 设计基准 · `Plans/<里程碑>/` 批次计划（一批一文件 + `Status:` 头）· `DevLog/` 事件流水 · `Reports/` 一次性快照 · `Archive/` 冻结历史）；命名与生命周期规约见 [`docs/README.md`](./docs/README.md)，单文件超 ~30KB 即按该规约拆分。
- 推翻既定设计的决策需在 `docs/ADR/`（ADR-009/010 已有）或设计文档内标注修订。
- 本仓库（`Lemon/`）是工作区唯一活跃 git 仓库，`docs/` 与本文件已随仓库版本管理；`Prowl2D/` 等参考目录是独立仓库。未经用户明确要求不要 commit。
