# Lemon 引擎 — 主工程说明

**Lemon**：纯 2D 高性能游戏引擎（C++20 + Vulkan 内核 + C# 脚本 + Unity 风格 ECS 编辑器），目标品类 ARPG/塔防/吸血鬼幸存者/增量，坚决不做 3D、不做重物理。

**当前阶段（2026-10-10）**：M0–M5、M6a/M6b/M6c、M7a、M7c 全部完成（M7a 收官 2026-10-07；**M7c 段收官 2026-10-10**，批⑫ 收口记录见段尾）。阶段与出口判据的权威登记 = [08 路线图](./docs/EngineDesign/08-Development-Roadmap.md) §0 总览表。**当前活跃段 = M8 光照与打磨（未开工）**；M7c 段内流水（保留至下次瘦身）：**M7c 引擎与编辑器功能段**（批⓪–⑥ 详见 [M7c.md](./docs/Plans/M7c/M7c.md)——⑥ 收口 = SceneMembership/SceneSwitcher/回放扩展，金回放跨版本三档零重录；批⑦ SDK 门面 ✅ 2026-10-08——SceneManager/Scene/LoadSceneMode + DontDestroyOnLoad + sceneLoaded/sceneUnloaded/activeSceneChanged 三事件（同步直推，协议⑤ 时序）+ vtable 尾加 49→56 + SceneSourceHooks 寻址 + ResolveScene 泛化；**批⑧ LoadSceneAsync ✅ 机器面 2026-10-08 / 回归收口 10-09**——分帧状态机（Parse→Build 建进暂存 registry（预备帧主世界逐位不动=加载帧哈希流全等）→Assets 占位+DOM 分帧回收→Gate→激活帧原子集成（按台账复刻同步槽位序列⇒句柄逐位一致，孪生用例机械证明））+ AsyncSceneLoad（progress 单调/门关 0.9 封顶/completed 恰一次/await 域线程同步续跑——恢复点先于当帧 Time.Advance）+ 单槽统一跨同步/异步（新请求 WARN 取代）+ vtable 56→59 + SceneEventKind::AsyncCompleted + 失败契约（批⑦ F1 收口）+ 加载屏样例（模板 LoadingScreen.cs/loading.rml，消费归批⑨）；D1–D3 已追认（用户 2026-10-09）；单测 **34,716**/script-tests **1,818**/ctest 4/4/回归 21/21（噪声红复跑绿 + flaky 压测门已按 09 §9 放宽）/bench fps=88/金回放三档 mismatches=0（worktree@5f7cffb 录→批⑧ 放）/压测 20k@4ms：staged ≤6.6ms/激活 13–16ms/同步对照 373+ms；[批文件](./docs/Plans/M7c/2026-10-08-b8-loadscene-async.md)、[DevLog](./docs/DevLog/2026-10-08-m7c-b8-loadscene-async.md)；**批⑨ 消费者迁移 ✅ 机器面 2026-10-09**——svr-test 拆多场景（MainMenu + Grass/Volcano，RunSweeper 退役 = 清场职责归引擎换场）+ 加载屏消费（LoadingScreen.Begin 进战斗/同步回菜单）+ vs-survivor 模板随迁（MainMenu+Grass 两场景、生成器补批⑧ 手加的 LoadingScreen 件、Flow guid 钉死跨重生成稳定）+ 批⑦ 两敞口收口（编辑器内换场端到端 = template-chain smoke 机器证；UI 点击换场回放 = 键盘位 D3：菜单 R/空格、结算 R/Esc 入 InputState）+ 机器级既有敞口顺手修复（smoke 会话 18ms 帧率下限——帧数窗 60Hz 标定 vs 显示器睡眠无节流 500fps+ 曾致 asset/uirml×2/script 四步确定性红，HEAD 基线同败实证）；跨场壳形态 = code-mount 四屏 + DDOL GameFlow 种子 + 重装自毁守卫（通道 B origin=CSharp 跨场幸存——场景声明式 UIDocument 换场即卸）；D1–D5 已追认（用户 2026-10-09）；引擎内核零改动：单测 34,716/script-tests 1,818 逐位不变/ctest 4/4/回归 21/21/game+scene+template 三 smoke OK/构建零警告；**真人走查 ✅ 2026-10-10 零缺陷实报**（[验收记录](./docs/DevLog/2026-10-10-acceptance-m7c-b9-b10.md)）；[批文件](./docs/Plans/M7c/2026-10-09-b9-svr-test-multiscene.md)、[DevLog](./docs/DevLog/2026-10-09-m7c-b9-svr-test-multiscene.md)；批⑩ 编辑器打磨 ✅ 机器面 2026-10-09（转正拍板同日：批⑧ D1–D3 + 批⑨ D1–D5 八裁决点全追认，[裁决 DevLog](./docs/DevLog/2026-10-09-m7c-b8-b9-ratify-and-b10-go.md)）——Play 态 Hierarchy 场景组显示（DDOL 置顶合成组/已装载档案组记录序/未指派归并活动组/clipper 保形）+ DDOL 行徽 + i18n 四 key 双语（hierarchy.json 20 keys 对齐）+ smoke-template 双 Play 段钉板（replayStage 状态机住 Playing() 守卫外层 + uiLoadsFinal Stop 前快照（gameUi_ 跨 Play 装载保留）+ verdict replay 位；帧预算 3400 维持——三跑全绿帧锚定无竞速）；引擎内核零改动：单测 34,716/script-tests 1,818 逐位不变/回归 21 步全有绿记录（smoke-drag 回归内双红 = M7a §8 注入抖动家族，隔离三连绿 09 §9 归因）/bench fps=82/构建零警告/截图视觉自证（DDOL 组 Flow 带徽 + MainMenu（根 0））；**真人走查 ✅ 2026-10-10**（验收记录同批⑨）；[批文件](./docs/Plans/M7c/2026-10-09-b10-editor-polish.md)、[DevLog](./docs/DevLog/2026-10-09-m7c-b10-editor-polish.md)；**批⑪ 引擎评审修复 ✅ 2026-10-10 全清收批**（review 2026-10-09，65 项四梯队：b11a 高危 3——H1 失效句柄三闸（红字+默认值 D1 口径全族）/H2 BuildClipCache 弱解析器 terminate（解析下沉 ParseClipJson）/H3 UI 容器缓存 UAF（InvalidateContainersUnder 突变前失效含 tpl 形态）；b11b 中危 14——坏数据 abort 面 5（M3 DEVICE_LOST 特判/M13 切片对账/M14 LBF1 头域/M15 非抛色/M21 EntityRef endptr）+ 设备丢失边角 2（M1 离屏 WAR/M2 GeoAlloc 世代）+ 语义纪律 7（M5 memcpy→snprintf ASAN 实报/M9 池化域线程/M10 Events 索引迭代/M18 锁外析构/M20 UI Init 拆净/M22 失焦清键/M6 扫掠防穿透）；b11c 性能 8——M7 DeclareTeams 差量/M8 allGrid_ 双径（**万 Flee 场实测 12.6×**：117–128ms→9.3ms/帧，bench-sim 新增 --flee）/M11 三批量导出+连续段合并（L13 顺带做实）/M12 双层实体索引（销毁序逐位保持）/M19 块级 SPSC refill/M16 guid 哈希/M17 ParsedEntityTree pimpl 预解析/M4 批量 API mac 实测负收益回退（Windows 验证项登记）；b11d 低危卫生——同构 L2/L11/L27/L15（Pcg32 三处 + grep 防线测试本体化）+ 小修 21 项 + 设计债登记 08 M8 段；单测 **34,754**/script-tests **1,834**/ctest 4/4/回归 21/21 两轮/bench fps 81–84/ASAN 双门零报告；四子批各过独立 review 轮（增量全修：H3-1/H1-a/b/c、R1–R3、**R-a1 多槽销毁序反转**、R-b×5）；[批文件](./docs/Plans/M7c/2026-10-09-b11-engine-review-fixes.md)、[b11c DevLog](./docs/DevLog/2026-10-10-m7c-b11c-perf-fixes.md)、[b11d DevLog](./docs/DevLog/2026-10-10-m7c-b11d-lowfixes-and-reg.md)；**批⑫ 段收官 ✅ 2026-10-10**——i18n en 态越窗收口（选帧底行按剩余宽省略截断 + smoke 族钉定 zh-CN + `--smoke-lang` en 态回归锚步，回归 full 21→22 步）+ 真人面②③⑤⑨⑩ 过 + 行动项① 血条素材复验转移交（不设期，同 W5 先例），[批文件](./docs/Plans/M7c/2026-10-10-b12-closeout.md)、[收官 DevLog](./docs/DevLog/2026-10-10-m7c-closeout.md)；下一步 = **M8 光照与打磨**）；执行序 = M7c → M8 → M9 → M7b（发行侧后移，2026-10-07 重排）。用户幸存者游戏与引擎并行开发（`demo/svr-test` 为工作项目，引擎卡点 DevLog 登记）。回归基线：full 22 步（批⑫ 起，含 en 态 i18n 宽度锚）/ ctest 4/4 / 单测 34,754 checks（批⑪）/ script-tests 1,834（批⑪）/ 系统 21 / bench-survivor fps≥76.5。

**待用户（行动项，勿丢）**：① （**移交，不设期**——M7c 已收官 2026-10-10，同 W5 先例）M7c 批① 血条正式素材落地后观感复验（同名替换 `Assets/bar_bg|fg.png`，[批① 批文件](./docs/Plans/M7c/2026-10-07-b1-fx-presentation-upgrade.md) §5）；③ W5 真机 GPU（物理机窗口，用户 2026-10-07 拍板再后移不设期，[批⑦ 批文件](./docs/Plans/M7a/2026-10-05-b7-windows-closure.md)）；④ M4.8 走查其余 UX 优化清单待用户细化（资产引用拖放已入 M7c 批④ 候选池，[走查记录](./docs/DevLog/2026-10-07-acceptance-m4-8-editor-walkthrough.md)）。已过（2026-10-07）：M4.8 零文档走查初步过、svr-test 真人总成 V1–V3（M7a 全闭，[验收记录](./docs/DevLog/2026-10-07-acceptance-m7a-b8-v1-v3.md)）、M7c 批① Fx 真人走查；已过（2026-10-10）：M7c 批⑨ svr-test 多场景全流程 + 批⑩ Play 态 Hierarchy 场景组真人走查（两项零缺陷实报，[验收记录](./docs/DevLog/2026-10-10-acceptance-m7c-b9-b10.md)）；批② 音频覆写听感 + 批③ i18n 全面板走查（[验收记录](./docs/DevLog/2026-10-10-acceptance-m7c-b2-b3.md)；en 态选帧对话框「替换为」越窗已知项留 M7c ⑤+ 候选池）。

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
