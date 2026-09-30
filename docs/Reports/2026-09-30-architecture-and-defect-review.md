# Lemon 引擎架构分析与缺陷评审报告

- **出具日期**：2026-09-30
- **审查基线**：`3597032` + 工作树未提交改动（39 个文件，M6b ③d-2 进行中）。行号以该基线为准。
- **审查范围**：`Engine/`（14.5k 行）、`Editor/`（21.4k 行）、`Engine/Scripting/dotnet/`（101 个 C# 文件）、`Templates/vs-survivor/`、CMake 构建与 `tools/editor-regression.sh`、`docs/EngineDesign/` 设计基准
- **环境**：macOS 24.6.0 x64、MoltenVK、.NET 10.0.12、Ninja、Release
- **方法**：静态通读 + 交叉行号实证 + 五个子系统并行深度评审。**未运行引擎、未做动态验证**；凡标注 ✅ 的条目为主审人沿代码路径亲手追到底（含 RmlUi 上游源码核对）。
- **变更声明**：本报告为只读评审，**未修改任何源码**。审查期间仓库工作树继续被用户推进，行号可能随后续提交漂移。
- **结论一句话**：Lemon 的内核设计是**高水位的小引擎架构**（EnTT 薄壳、声明式系统管线、函数指针表 C# 桥、零漂移纪律），但存在 **1 条让整个 IME 功能静默失效的 Pimpl 缺陷、1 条窗口 resize 即可触发的 present 越界、以及"编辑器即运行时"导致的结构性缺口**；修复工程量集中在 3～5 人日，结构性缺口（运行时资产层）才是 M7 的真实阻塞项。

> 本报告是架构 + 缺陷的合并快照。既有 [Engine-only Review](2026-09-23-engine-review.md) 与 [全栈审查](2026-09-24-code-review-546a755.md) 仍有效；本报告不替代它们，本报告与它们重叠的条目不再重复，仅补充 2026-09-24 之后新增代码面（M6a 批②动画工作台/Tween、M6b ③a–③d RmlUi UI 栈）。

---

## 1. 执行摘要

### 1.1 架构一句话

**一个 EnTT sparse-set 薄壳内核 + 20 步注册序系统管线 + 64 实体块的 C# 批量边界 + 编辑器中置的资产库。**

| 维度 | 数据 |
|---|---|
| 内核规模 | Engine 14.5k 行；其中 Renderer 4.3k、Systems 1.3k、ScriptHost 883、UiSubsystem 1146 |
| 编辑器规模 | Editor 21.4k 行；最大单文件 AnimationPanel 2090 行 |
| 组件 | 35 个登记组件（id 0..30，只追加不重排），全部 POD，`static_assert` 冻结布局 |
| 系统 | 20 步固定管线（Essential / FixedTick / Extract 三阶段），11 处手写 `ParallelFor` |
| 第三方 | 12 项全部登记 `THIRD_PARTY.md`；无 DI 容器（红线守住） |
| 验收纪律 | 16 步 grep 断言回归链 + "孪生世界同 seed → StateHash 相等"的属性式确定性自证 |

### 1.2 缺陷统计

| 严重度 | 条数 | 其中主审人亲手实证 ✅ | 当前即可触发（活跃） | 需特定条件（潜伏） |
|---|---:|---:|---:|---:|
| 高 | 11 | 7 | 5 | 6 |
| 中 | 23 | 5 | 12 | 11 |
| 低 | 14 | 0 | 6 | 8 |
| **合计** | **48** | **12** | **23** | **25** |

> "活跃"指在现有代码路径 + 常规用户操作下会发生的；"潜伏"指需要设备丢失 / 特定资产规模 / 多线程档等条件。潜伏项不应被当作"不会发生"——其中设备丢失链已被 `bench --device-loss` 显式压测。
>
> **实证口径**：初版 51 条经逐行复核后**证伪 1 条**（D12，见 §6 高 severity 末尾勘误），并新证 6 条（D11 + M6/M8/M9/M13/M14 及 M8 线格式口径），最终 48 条 / 实证 12 条。

### 1.3 成熟度定位

代码功能约完成 2/3（M0–M5 + M6a 批② + M6b ③a–③d）；**可作为高级原型 / 垂直切片底座**。距离"生产发行引擎"的阻塞项不是性能而是三件事：运行时无独立资产加载能力（§5.1）、无 CI 与无 Windows preset（§7.4）、以及一批数据完整性缺陷（§6 高 severity 后 7 条）。

---

## 2. 分层架构与模块职责

### 2.1 实际分层（注意：与设计文档有分叉，见 §5）

```
┌──────────────────────────────────────────────────────────────────────┐
│ lemon-editor —— 全仓唯一可执行体（ImGui docking）                      │
│  EditorApp 肥壳 1060 行 / 12 个外迁 TU / ~80 个成员未拆                 │
│  EditorContext：Play/Edit 双 World · EnterPlay JSON 快照 · Undo 双轨   │
│  AssetDatabase · AssetGpuCache · FileWatcher · ProjectWizard           │
│  ↑ 资产库实际住在这一层——Engine 内没有 Assets/                        │
└───────────┬──────────────────────────────────┬───────────────────────┘
            │ lemon-editor-core                │ lemon-csharp
┌───────────▼──────────────────────────────────▼───────────────────────┐
│ lemon-engine（14.5k 行 STATIC）                                       │
│  ECS/    Scene(entt::registry 薄壳) · SystemPipeline(Kahn) · 7 个     │
│          World 级非 ECS 通道（Clip/Controller/Team/Tween/Fx/Save/Table）│
│  Systems/ 20 步纯函数系统 + 11 处 ParallelFor(JobSystem 工作窃取)       │
│  Renderer/ Renderable(提取-双缓冲-插值) → SpriteBatcher(实例化) → RHI  │
│  Ui/       RmlUiBackend(RenderInterface over RHI) · UiSubsystem       │
│  Scripting/ ScriptHost(桥 883 行) · CoreCLRHost(hostfxr) · ScriptBox   │
└───────────┬──────────────┬──────────────┬──────────────┬─────────────┘
        Vulkan/VMA      SDL3          RmlUi 6.3    libhostfxr
        (仅 .cpp 可见)                            (DomainManager.cs 域线程)
```

### 2.2 一帧的数据流

```
Window::PollEvents (SDL)
  ├─ TEXT_INPUT → gameUi_->ProcessTextInput        （提交制文本：可用）
  ├─ TEXT_EDITING → SdlTextInputHandler            （IME 预编辑：已死，见 D1）
  └─ 键鼠 → ImGui 优先，未占用才给视口/游戏 UI
ImGui BeginFrame → BuildUI（定 gameRT/sceneRT 尺寸）
World::Step(1/60)  ← 单主线程串行；accumulator + 追帧上限 5 步 + alpha
  ├ #1 输入快照 → #2 导演 → #3 出生 → #4 AI → #6 分离力 → #7 移动
  ├ #8 空间哈希重建 → #9 拾取 → #10 命中 → #11 触发器 → #12 数值
  ├ #13 动画 → #14 回收 → #15 C# 批量（ScriptHost::TickBatch → 域线程 → C# ForEach）
  ├ #16 动画状态机 → #16.5 Tween → #17 事件派发（UI 事件 + 游戏事件）
  └ Essential：#17 两阶段销毁提交 + ApplyStructural(SceneOps)
渲染：ExtractScene(剔除+插值+排序，帧内缓存跨双视口共享) → Bake(段→批→ring[ringFrame])
      → BeginOffscreenPass → per 批 push constant + DrawInstances
      → [Play] UiSubsystem::Render → RmlUi backend → ctx->Render()
      → BeginPass(swapchain) → ImGui Render(GameView 采样 RT) → Present
后：AdvanceFrame(ringFrame = (+1)%3) + QualityManager EMA 降级
```

**关键点**：模拟与渲染**同在一条主线程上串行**，无独立模拟线程/渲染线程（设计文档 §2/§3 规划了三线程，未落地）。这是当前性能架构与设计的最大分叉，见 §5.2 与 §7.1。

---

## 3. 五个子系统的设计分析

### 3.1 ECS 内核：刻意"薄"，但薄得有分寸

`World.h` 249 行、`Scene.h` 140 行、`TableStore.h` 35 行——引擎自己几乎不写 ECS，EnTT 3.15 sparse-set 就是存储模型。`Scene` 只是 `entt::registry` 的包装（`Scene.h:131`），业务只见 `Entity{uint64_t}`（raw+1，0 恒 null）。

**实体生命周期**用 tombstone + 两阶段销毁：`Destroy()` 只入队打 `DestroyQueueTag`（`Scene.cpp:23-45`），`DestroyCommitSystem` 在 Essential 阶段统一提交。这让"脚本里删实体"不可能在遍历中途改存储。

**"Table"一词在代码里被复用成三层完全不同的东西**，是新人阅读的最大陷阱：

| 层 | 实体 | 说明 |
|---|---|---|
| 真表 | 每组件类型一个 EnTT storage 池 | `scene.Pool<T>()` 可直访 |
| World 级通道表 | `ClipTable`(69行) / `ControllerTable`(80行) / `TeamTable`(32×32=1KB) / `TweenTable`(94行) / `FxChannel`(101行) / `SaveChannel`(53行) | 非 ECS、键 = 资产 GUID 低 32 位、只 `Find` 不遍历 |
| 数据表 | `TableStore` = `.tab` 资产全字符串网格 | 无模式无类型，数值解释归 C# |

第 2 层是刻意的架构决策，见 §4.1。

**组件反射是显式目录表**：`ComponentCatalog.cpp` 35 个 `REGISTER*` 宏填 16 成员 `ComponentMeta`（offset + FieldType + flags + FieldHint），序列化 codec 与编辑器 Inspector 共用同一张表，不玩静态 registrar（规避跨 CU 初始化顺序坑）。代价是 31 个镜像 struct 手写 + 双向 `static_assert` 对齐。

### 3.2 系统调度：拓扑能力在，但内置系统零使用

`SystemPipeline` 有 Kahn 拓扑排序（`SystemPipeline.cpp:24-65`）与 `after("Movement")` 声明式依赖，**但全仓没有任何内置系统覆写 `After()`**（唯一使用者在 `Samples/anim-smoke/main.cpp:123`）。所以实际执行序 = 注册序，而注册序同时承担第二个职责：**RNG 子流 id 分配**（`Systems.cpp:1316-1318`）。改顺序 = 改确定性。

20 步管线（`Systems.h:89-275`）设计克制，两个顺序细节值得肯定：

- **#8 空间哈希重建放在移动之后、命中之前**——命中判定用新位置，AI 用旧位置；
- **#16 动画状态机读当 tick 脚本写的参数、由下一 tick #13 消费**——明确接受一帧延迟以换取无环。

并行靠**手写 `ParallelFor`（11 处）**，契约 = 只读帧内快照 + 只写自身组件 + 按池序归并，无自动读写依赖推导。凡不满足"无依赖纯函数"或"分块读写分离"的系统一律单线程。

### 3.3 渲染：实例化精灵 + bindless 3 段环形 SSBO

- **RHI 是薄到只剩 Pimpl 的一层**（`RHI.cpp` 1641 行），头文件零 Vulkan 类型，句柄 = 资源表下标+1（**无代际校验**）。唯一后端 Vulkan，动态渲染（`vkCmdBeginRendering`），2 帧在途，present 信号量按图像持有 + acquire 走 fence-only。
- **Shader 编译期嵌入 SPIR-V**（glslang → uint32 数组），`CreateShader` 按词哈希去重，管线缓存落盘可自恢复。
- **合批**：24B POD 批键（图集槽+blend+filter+wrap+layer），**连续同键成批、无逐批排序**——排序在提取期做进 64 位 `sortKey`。共享单位四边形 + 每实例 48B 写 3 段环形常驻映射 SSBO，每批 push constant 带 `baseInstance/atlasIndex/ringIndex`，一次 draw。M0 spike 实测 50 万精灵 137fps。
- 粒子 = **CPU 模拟 + swap-and-pop**，提取后复用同一合批器，预算随质量分档。
- **RmlUiBackend 绕开 rhi:: 层自建 pipeline layout / 描述符池 / 8MB 几何子分配池**——这是渲染层内唯一不受 RHI 纪律约束的区域，也是 §6 中数条缺陷的来源。

### 3.4 C# 桥：两条通道 + 域线程常驻，纪律执行得最干净

- **C++→C# 不是 reverse P/Invoke**，而是 hostfxr 一次取全 ~25 个 `[UnmanagedCallersOnly]` 函数指针；纪律是禁热路径 `GetExport`，且**所有导出必须 try/catch**（M4.6 教训：未捕获异常 = coreclr abort 整个编辑器）。
- **C#→C++ = `delegate* unmanaged` 函数指针表**，`lemon_api_register2` 做尺寸握手（min 拷贝 + 尾零）——表尾追加零扰动、SDK 逐槽判空降级。这是跨 36k 行 C++/C# 的 ABI 演化问题的优雅解。
- 组件数据三态：热路径 **SoA 原生指针 + C# `Span<T>` `ref *(T*)ptr` 原地零拷贝**（64 实体一块）；低频语法糖整 struct memcpy；结构变更只入命令缓冲帧首统一应用。
- **域线程常驻**：`DomainManager` 起常驻托管线程 + `BlockingCollection` 命令泵，native 投命令后等栅栏。这是 .NET 10.0.12 实测下的务实解——任何触碰过 collectible ALC 的活线程会永久 pin 它，连裸 load→unload 都 TIMEOUT，故 Unload 必须在 UCO 线程就地执行。代价 = 每次热重载泄漏 ~百 KB（已登记为已知 runtime 限制）。
- 每帧一次往返实测 ~66µs。Update = 60Hz 固定步长（≈ Unity FixedUpdate，**无变步长 Update 区分**，ADR-009 D2 明示）。

### 3.5 UI：两条路径并存，正在收敛

| 路径 | 用途 | 状态 |
|---|---|---|
| ImGui | 编辑器外壳 + 遗留 RtUi HUD | 外壳正字；RtUi 兼容层去留推 M8 |
| RmlUi 6.3 | 游戏屏（一屏一文档） | M6a③a 转正式依赖，ADR-014 |

`RmlUiBackend` 自研实现 `Rml::RenderInterface` over RHI；`<img>` 经 `UiTextureResolver` 桥到图集页；`UiSubsystem` 持 SystemInterface + SDL IME。C# 侧是 `UI.Apply(ops)` 单一提交口（32B blittable op + arena）+ `UiEvent` 队列——与 ECS 事件队列同一设计。ADR-008 的"通用模态卡片通道"已退役改文档化。

**这一层的质量明显低于其它子系统**：RmlUiBackend 自建资源管理、无统一失效路径、多处"静默降级"，§6 中高 severity 有 4 条落在此处。

### 3.6 编辑器：Play = JSON 快照两份世界

- `EnterPlay`：`editSnapshot_ = Save(editScene)` JSON 字符串 → 建同 seed 新 World + 16 系统 + `Load(snapshot)` + 一次性建 prefab/clip/controller/table 缓存 + 载入三档存档；
- `ExitPlay`：弃 playWorld → editScene 整体 Load 快照 → **"再序列化 == 快照"逐字节比对作为验收判据**（`EditorContext.cpp:1061`）。
- ADR-011 结论：**Play 中调参显式不回灌**（结构变化使回灌失去良定义，且逐字节等价验收不可破坏），Inspector 橙横幅提示。理由是自洽的——但 §6 的 D6 表明这个"Play = 只读沙盒"不变量有一条被旁路的路径。
- Undo 双轨：属性轨 = 组件字节快照按 guid 定位；结构轨 = 整场景 JSON。Play 中清禁。
- `EditorApp.cpp` 已从 ~4000 行砍到 1060 行，手法统一为"Run 挂点原位不动、状态收敛进单 TU"，外迁 12 个 TU。但 `EditorApp.h` 仍囤 ~80 个成员——**搬了函数没拆状态**。
- 无头自动化是真本事：`TestHooks` 把控件屏幕矩形 Stash 进全局 map，注入链在 NewFrame 排水前合成键鼠；截图 = DebugRecordCapture 回读。16 步回归链全靠它。

---

## 4. 设计得最好的六个决策（值得保留）

1. **"零漂移"作为可执行纪律，而非口号**。任何新通道一律"World 持有 + 非 ECS + 不入 StateHash"（`World.h:29-33`）。Tween/Fx/表格/存档全部走这条路，所以加功能不动哈希流 = 金回放零重录。把回归成本从"重录基线"降到"零"。
2. **组件登记表只追加不重排**（id 0..30），配合 `StateHash` 逐字段**含字段名**哈希——schema 漂移直接表现为回放分歧，fail-fast 而非静默错位。
3. **确定性验收用"孪生世界同 seed → StateHash 相等"的属性式自证**（`tests/engine_tests.cpp:2371,2911`），而不是存外部金样文件。不需要维护基线资产，也不会因平台/编译器差异假阳性。
4. **函数指针表尺寸握手**（`lemon_api_register2`）：表尾追加零扰动 + SDK 判空降级，用一个 min 拷贝解决 ABI 演化。
5. **统一 64 实体块**做 C# 批量边界：摊薄回调 <1ns/实体，热路径零托管分配（实测硬 0）。
6. **两阶段销毁 + 帧末事件 `TakeAll` 快照**：遍历期不可变存储、回调内再入队留到下帧。这两个组合拳让最常见的两类脚本错误不可能发生。

---

## 5. 设计文档与代码的分叉（读文档前必读）

`docs/EngineDesign/01-Architecture-Overview.md` 描述的是**目标架构**，代码已实质分叉。以下每条均已核实：

| # | 设计文档说 | 代码实际 |
|---|---|---|
| 5.1 | `Engine/Assets`、`Engine/Audio`、`Engine/Navigation`、`Engine/Batch`、`CSharp/` 五个目录 | **全不存在**；资产库在 `Editor/Assets`，C# 在 `Engine/Scripting/dotnet`，Renderer 已扁平化 |
| 5.2 | 三线程：主线程 / 模拟线程 60Hz / 渲染线程 | **单主线程跑完一切**（`EditorApp.cpp:555-600`），JobSystem 只做系统内 `ParallelFor`；accumulator/追帧上限/alpha 插值都在，但没有独立线程 |
| 5.3 | 同源双入口 `EditorEntry` / `GameEntry` | 只有 `lemon-editor` 一个可执行体；运行时入口靠 `--project --play` / `--final` 旗标模拟（`EditorEntry.cpp:36`） |
| 5.4 | 引擎有独立资产加载（GUID+manifest、导入器运行时侧） | **运行时无 PNG 解码能力**——`STB_IMAGE_IMPLEMENTATION` 只存在于 `Editor/Tooling/StbImpl.cpp`。纹样数据全靠编辑器进程内 `AssetGpuCache` 上传 |
| 5.5 | 系统声明式排序 `after("Movement")` | 能力在，内置系统零使用；实际序 = 注册序 = RNG 子流 id |
| 5.6 | 固定 60Hz 模拟 + 双缓冲插值渲染 | 双缓冲真实存在（`Renderable::BeginSimTick()` 翻转），但**所有自动化链（smoke/bench/final）强制每帧一步 + alpha=1** 以保证像素断言确定性——插值只在交互式 Play 生效 |
| 5.7 | `Tools/packager` 一键出包 | 不存在（M7 未开工）；且因 5.4，缺的不只是打包脚本 |
| 5.8 | ImGui HUD 为 v1 运行时 UI 方案 | 已退役（ADR-008 D2 → RmlUi）；RtUi 兼容层仍留在 `ViewportPanels.cpp` |

5.1/5.4 合起来是最重要的一条：**当前"运行时"不能独立加载任何图像资产**，所以 M7 packager 的真实前置是一整层 `Engine/Assets`（PNG 解码 + 图集烘焙 + `.baked` 读写），工作量远超"写个打包脚本"。

---

## 6. 缺陷清单

标注说明：✅ = 主审人亲手沿代码路径实证（含上游库核对）；**触发** = 复现所需条件；**潜伏** = 现有代码路径下暂无触发者，但护栏缺失。

### 6.1 高 severity（11 条）

#### D1 ✅ IME 预编辑通道整体静默失效（Pimpl 从未分配）

- **位置**：`Engine/Ui/SdlTextInputHandler.cpp:63`（`SdlTextInputHandler() = default`）、`.h:29`（`std::unique_ptr<Impl> impl_`）、`.cpp:66`（`HandlerPtr()` 取 `&impl_->handler`）、`.cpp:69`（`if (impl_)` 守卫）、`Engine/Ui/UiSubsystem.cpp:614`（注册给 `Rml::CreateContext`）、`:1022`（转发）
- **缺陷**：`SdlTextInputHandler` 的 `impl_` 是 `unique_ptr`，而构造函数是 `= default`，**从不存在任何分配点**。于是 ①`HandlerPtr()` 返回 `nullptr` 并作为 `Rml::TextInputHandler*` 注册；②`HandleTextEditing()` 的 `if (impl_)` 恒假，**永远 no-op**。
- **失败场景**：Play 中点开任何含 `<input>` 的文档，输入中文 → 预编辑串/候选窗完全不出现，只有 ASCII 提交制输入（走 `SDL_EVENT_TEXT_INPUT` → `ProcessTextInput`，该路径正常）能进框。DevLog 记载的"IME 零乱码/候选窗贴光标/事件不串"三判据是在 spike-04（自带可用的官方后端拷贝）验证的，**引擎落位通道从未真正工作过**。真人验收"文本输入已过"应理解为只覆盖了提交制路径。
- **为何不是崩溃**：已核对 RmlUi 6.3 上游，`WidgetTextInput.cpp:670/685` 与 Destroy 路径对 handler 均做 null 守卫，故 `nullptr` 被容忍——这让缺陷从"必崩"退化为"静默失效"，反而更难发现。
- **修复方向**（仅建议）：构造函数 `impl_(std::make_unique<Impl>())`；`HandlerPtr()` 加断言；补一条"IME 预编辑可达"的冒烟断言。

#### D2 ✅ 窗口 resize 竞态 → present 越界读 semaphore（UB / 可崩）

- **位置**：`Engine/Renderer/RHI.cpp:1202`（OUT_OF_DATE 早退，`m->imageIndex` 不更新）、`:555-556`（`presentSemaphores.clear()`）、`:661`（`resize(n)`）、`:1261/1274/1277`（按 `m->imageIndex` 取 semaphore / 填 `pImageIndices`）；`Editor/App/EditorApp.cpp:711-716`
- **缺陷**：`AcquireNextImage()` 报 OUT_OF_DATE 时直接 return，`m->imageIndex` 保持上一帧值；调用方见 `needsRecreate` 且 `RecreateSwapchain()` 成功时**不重新 acquire、不 continue**，直接进 `BeginFrame`/`EndFrameAndPresent`。**6 处调用点同款**：`EditorApp.cpp:711` + `Samples/{anim-smoke,bench-mow,bench-sprites,bench-particles,rhi-smoke}` 各一处，六段代码逐字相同（`bench-mow` 只多一个 `++recreateEvents` 计数）。
- **失败场景**：SDL poll（`:486`）与 acquire（`:711`）之间发生窗口 resize → 新交换链建成、`presentSemaphores` 被 clear+resize 成新图像数，而 `imageIndex` 是旧链的旧下标 → `std::vector::operator[]` 越界读出一个垃圾 `VkSemaphore` 交给 `vkQueueSubmit`/`vkQueuePresentKHR`，并用一个从未 acquire 过的图像 present。新链图像数 ≤ 旧下标时必越界；否则 present 一个未获所有权的图像。表现为黑帧/撕裂/验证层 VUID 报错，严重时驱动异常。
- **修复方向**：`needsRecreate` 且重建成功时应 `continue` 或重新 acquire；或让 `RecreateSwapchain()` 复位 `m->imageIndex` 并在 `AcquireNextImage` 的早退分支显式置哨兵值。

#### D3 ✅ `SpriteBatcher::Init()` 重复登记设备重建回调（泄漏 + 悬垂 this）

- **位置**：`Engine/Renderer/SpriteBatcher.cpp:27`（`Init()` 无条件 `AddRecreateCallback`）、`Engine/Interaction/ViewportRenderer.cpp:359-360`（`OnDeviceRecreated` 对两个 batcher 各再调一次 `Init()`）、`Engine/Renderer/SpriteBatcher.h:49`（析构只摘最后一个 id）
- **缺陷**：`Init()` 对回调注册不幂等；`recreateCbId_` 只记最后一个 id，析构摘不掉前 N-1 个。
- **失败场景**：设备丢失 1 次 → 设备回调表里出现 2 个 SpriteBatcher 回调；再丢 → 4 个。每个多余回调跑一遍 `CreateGeometry()`，把 `cornerVB_`/`indexIB_`/`pipelines_` 直接覆盖而**不 `DestroyBuffer`/`DestroyPipeline`**，实例环整个泄漏（`capacity_` 保留故单份可达数 MB）。更严重的是：`~SpriteBatcher` 只摘最后一个，前 N-1 个回调继续持有已析构对象的裸 `this`，下次设备丢失即 UAF。`SpriteBatcher.h:45-46` 的注释已预见过这个模式，但只覆盖"Init 一次"的情形。
- **触发**：`bench --device-loss`、真实 GPU reset。
- **修复方向**：`Init()` 开头 `RemoveRecreateCallback(recreateCbId_)`（幂等化），或给 `SpriteBatcher` 一个独立的 `OnDeviceRecreated()` 入口，与 `Init()` 分开。

#### D4 ✅ UI 事件订阅者在 native #16 线程就地执行，绕过域线程（违反 ADR-010 D1）

- **位置**：`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:258-260`（`lemon_ui_events_dispatch` 直接 `Lemon.UI.DispatchEvents(src, n)`）、对照 `:158`（`lemon_events_dispatch` → `DomainManager.PostBatchEvents`）；`Engine/Scripting/ScriptHost.cpp:873-877`（在 `DispatchEvents` 内、native 窗口下调用）
- **缺陷**：两条本应对称的事件派发路径，一条走域线程、一条不走。UI 事件的用户句柄在 **native 管线线程**上执行。
- **失败场景**：玩家点 UI 按钮 → 用户 C# 句柄在 native 线程运行 → ①该帧用户代码根住 collectible `ScriptAlc`，之后 Stop-Play / 热重载 `Unload` 收不净（`leakCount` 涨，内存随换装累积）——这正是 ADR-010 D1/M3-2b 用域线程模型要规避的问题；②`DomainManager` 的域线程此刻阻塞在 `BlockingCollection` 上，故当前**无真实数据竞争**，但这依赖"管线顺序执行"这一未文档化的前提，一旦 §7.1 的模拟/渲染分线程落地即成读写撕裂。
- **修复方向**：改为 `DomainManager.PostUiEvents(...)`，与 `PostBatchEvents` 对称。

#### D5 ✅ 批量帧预收集指针在托管 tick 期间可被 Spawn/Write 失效（护栏缺失）

- **位置**：`Engine/Scripting/ScriptHost.cpp:395-416`（`GatherEntity` 把组件池指针收进 `ptrBuf_`，**全部在 native 侧、进入托管之前**完成）、`:540-552`（按 `countFn` 预留）；`Engine/Scripting/dotnet/Lemon.SDK/Assets.cs:21-25`（`Instantiate.Spawn` 就地建实体）
- **缺陷**：所有块的 `comps` 指针在 `lemon_scripts_tick` 之前一次性收好，其生命周期跨越**整个托管 tick**（`Time.Advance → TickStartUpdate → Batch.Tick → TickLateUpdate`）。任何在原地建实体 / get-or-create 组件的调用（`NativeSpawnSprite`、`NativeWrite` 的 `emplaceFn` 分支）都可能让 EnTT packed 数组增长重分配，使已收集指针全部悬垂。
- **失败场景**：一个 `IForEachSystem`（或某个 `Update`）在遍历中 `Instantiate.Spawn(...)`，池增长越过容量阈值后，后续块的 `Chunk.Span<Transform2D>()` 读写已释放内存 → AV 或静默损坏其它实体。
- **触发**：**潜伏**——仓内脚本（模板 Behaviour + `RunSweeper`）没有"批量系统内生成实体"的用法，故当前不炸。`Assets.cs:3` 的注释（"当帧已构造的批量块不受影响"）论述的是**实体可见性**（下帧才对系统可见），与**指针稳定性**是两件事，不该被当作安全论证。
- **修复方向**：在 `NativeSpawn`/`NativeWrite` 的 emplace 分支命中时置一个 `batchStale_` 标志，托管侧下一次取块时整体重建；或干脆禁止在 tick 窗口内走同步生成通道（只留 SceneOps）。

#### D6 ✅ Play 中可修改磁盘源资产（"Play = 只读沙盒"不变量被旁路）

- **位置**：`Editor/Panels/AnimationPanel.cpp:1249`（`if (ro) ImGui::BeginDisabled()` 只包住图标工具条）、`:1243`（集名 `InputTextWithHint` 在禁用块之前）、`:1320`（左列 inline 改名）、`:1345-1350`（双击入口）；`:1498`（`CommitSegRename` 不判 `ctx.Playing()`）、`:1528`（`db.Rename` 段 `.anim`）、`:1532`（`TrySaveSet` 覆写 `.override`）
- **失败场景**：进 Play → 在动画工作台左列把段 `run` 改名为 `run2` 回车 → `.anim` 在盘上被重命名、`.override` 被覆写，而 playWorld 的 clip 快照缓存与正在跑的一局被扰动。这不只违反 ADR-011 的沙盒承诺，还会让 `ExitPlay` 的逐字节比对基准与磁盘状态脱钩。
- **修复方向**：把 `ro` 提升为面板级守卫（入口 + commit 双判），与 Inspector 的橙色横幅同一套语义。

#### D7 ✅ 段/集名未做 JSON 转义且落 128B 定长缓冲 → 资产永久损坏

- **位置**：`Editor/Assets/ClipEdit.cpp:100-101,178-186`（`ClipToJson`/`AnimSetToJson` 原样拼接 `name`、`snprintf(sg,128,…"%s"…)`）、`Editor/Panels/AnimationPanel.cpp:1504-1517`（`CommitSegRename` 校验只拒空/`/`/`\`/`..`/重名，**不限长、不拒 `"`**）
- **失败场景**：把某段改名为 `we"ird`（或 ≥~55 字符）→ `.override` 写成非法 JSON（或被 `snprintf` 截断成残缺 JSON）→ 下次 `ParseAnimSetJson` 失败 → 整个动画集工作台红字"解析失败"且**永久无法再编辑**，只能手改 JSON 救。这是"用户一次误操作 → 资产永久报废"的类别。
- **修复方向**：写侧统一走 JSON 字符串转义（nlohmann 已可用）；改名校验加 `"` 拒绝与长度上限；`TrySaveSet` 失败时恢复内存态。

#### D8 ✅ UI 文档装载/重载失败不回滚 → 游戏输入永久让出

- **位置**：`Engine/Ui/UiSubsystem.cpp:706-713`（`LoadDocumentFromFile` 先 `UnloadDoc` 旧实例再 `LoadDocument`，失败即 `return false`，`docs` 条目残留 `doc=nullptr`、`shown/modal` 不变）、`:679-685`、`:736-738`（`ReloadDocument` 的 `shown` 回写无视成败）
- **失败场景**：编辑态双击预览 `.rml` → 把该文件改坏保存 → watcher 触发重载失败 → 该条目 `modal` 仍为真而 `doc==null` → `AnyModalShown()` 恒真 → **WASD/空格等全部游戏输入永久让出**，且 `ShowDocument` 再也无法清除 modal。需重启才能恢复。
- **修复方向**：装载失败回滚到上一个已知良好的 `doc`/`shown`/`modal` 三元组；或失败即强制 `shown=false` 并红字。

#### D9 ✅ RmlUi 外部纹理 `VkImageView` 一次性缓存、无失效路径

- **位置**：`Engine/Renderer/RmlUiBackend.cpp:152`（外部纹理把 `GetVulkanTextureViewInterop` 的 view 缓存进 `TextureRes`）、`:414-418`；`Editor/App/EditorAppActions.cpp:141-167`（资产热重导分支只处理 `.rcss`/`.rml`）
- **失败场景**：Play/预览含 `<img>` 的文档 → 修改对应 `.png` → `AtlasRegistry::UpdateAtlasPage` 换掉 page texture → RmlUi 仍持旧 `VkImageView` → 提交已销毁 imageView（验证层报错，驱动层风险），直到手动重载文档。
- **修复方向**：把 RmlUi 外部纹理解析从"一次性缓存"改为"每帧按纹理 id 重取"，或在 `UpdateAtlasPage` 路径上挂一个 RmlUi 纹理失效钩子。

#### D10 ✅ `uAtlases[64]` vs `kMaxTextureSlots=256`：第 65 个资产起静默不可采样

- **位置**：`Engine/Renderer/Shaders/sprite.frag:4`（`uniform texture2D uAtlases[64]`）、`Engine/Renderer/RHI.h:21`（`kMaxTextureSlots = 256`）、`RHI.cpp:462`（descriptorCount 256）、`Editor/Assets/AssetGpuCache.cpp:113`（满槽判定按 256）
- **缺陷**：两处常量无单一来源。槽位 64..255 **可以绑定成功**，但 sprite 着色器声明的数组只到 63。
- **失败场景**：一个项目导入 ≥65 个独立 sprite 资产（M4 起"一页一资产"、同层混合/过滤组合不同）→ 第 65 个起的图集页绑得上却采不到 → 对应实体在视口里直接消失，无红字无日志；而满槽判定仍按 256，导入第 200 个资产时才会报"满"。
- **修复方向**：把常量生成进同一份 shader 头（构建期 `glslang -D` 或与 `EmbeddedShaders` 同机制）；`AssetGpuCache` 满槽判定改用 shader 侧真上限；补一条"导入 65 个资产仍可采样"的冒烟断言。

#### D11 ✅ `DestroyTexture` 不清 bindless 描述符槽 → 悬垂描述符

- **位置**：`Engine/Renderer/RHI.cpp:1038-1046`（`DestroyTexture` 全函数体）、`Engine/Renderer/RHI.h:21-22`（`kMaxTextureSlots = 256`、`kMaxSamplerSlots = 8`）；触发侧 `Editor/Assets/AssetGpuCache.cpp:86`、`:143-144`（"刚上传的纹理无登记号：回滚整页"）、`:179`
- **缺陷**：`DestroyTexture` 十行函数体只做四件事——`vkDestroyImageView`、`vmaDestroyImage`、`r = {}` 清记录、`textureFree.push_back(id)` 进空闲表，**全程不碰 bindless 描述符集**。槽位 `t.id - 1` 的描述符仍指向已销毁的 `VkImageView`。
- **失败场景**：某资源 `spriteId` 登记冲突 / 无登记号 → `AssetGpuCache` 走回滚 → `DestroyTexture` 后槽位描述符悬垂；在下一个导入复用同槽并重绑之前，任何 `atlasIndex` 命中该槽的绘制都会采样已释放内存（验证层 use-after-free + 画面花噪）。与 D3/D10 同属"无代际句柄 + 槽位生命周期不完整"这一类。
- **修复方向**：`DestroyTexture` 同步把槽位描述符清为无效绑定（`PARTIALLY_BOUND` 已开，正好支持）；或给句柄加代际校验。

> **勘误（2026-09-30 同日复核）**：初版本节列有一条「D12 —— `Scene::Destroy` 在 `ParallelFor` worker 内无锁并发写 EnTT storage」，经逐行复核为**证伪**：`Scene.cpp:29-35` 的 `lock_guard<std::mutex> lock(destroyMutex_)` 作用域确实覆盖了 `registry_.valid/all_of/emplace<DestroyQueueTag>` 与 `destroyQueue_.push_back` 两者，多 worker 并发调用是安全的。原失实表述（"mutex 只保护 Scene 自己的 vector"）源于只读到 `:30-33` 而漏看了 `:29` 的加锁与 `:35` 的右花括号。该条已删除，其真实残留 —— 单个全局互斥量作为 `ProjectileLifetimeSystem` 在 `ParallelFor` 内的唯一串行化点 —— 属性能而非正确性，已移入 §7.1 第 7 项。保留本勘误以说明本报告条目均经过实证筛选而非照单全收。

### 6.2 中 severity（23 条）

**渲染层**

| # | 位置 | 缺陷与失败场景 |
|---|---|---|
| M1 | `Renderable.cpp:158-160`、`Particles.cpp:132-134` | 键桶满 = `continue` 静默丢弃，`droppedSprites`/`droppedParticles` 全仓**只写不读**（已 grep 确认）。≥65 个资产组合时实体凭空消失，零反馈 |
| M2 | `RHI.cpp:519-529` | `LoadPipelineCache` 只 `exists()` 不校验流打开成功；`tellg()` 失败返回 -1 → `(size_t)-1` 进 `resize`。`.lemon/editor/pipeline-cache.bin` 存在但不可读 → 二次启动抛未捕获 `std::length_error`，编辑器无信息栈退出 |
| M3 | `RmlUiBackend.cpp:394-399,557` | 描述符集耗尽（`kMaxTextureSets=128`）只 `LEMON_ERROR` 并把 `set` 置 null，`RenderGeometry` 的 textured 分支**回落无贴图 pipeline**。一份 RML 引用 >128 个不同纹理 → 第 129 起的 `<img>` 全部画成一片平色，"图鉴 500 条"文档必中 |
| M4 | `RmlUiBackend.cpp:525-540` | 8MB 几何子分配池耗尽返回句柄 0，RmlUi 当"编译失败"，该几何在下次变脏/重载前不再绘制 → 元素整棵子树消失，仅一行 ERROR |
| M5 | `RHI.cpp:1376-1386` | `GetInternalBridge()` 首次调用即缓存 device/allocator 快照，`HandleDeviceLost`（`:760`）从不复位 `bridgeFilled`。当前唯一消费者恰好在每次丢失后重读才未爆，但任何"启动时取一次"的新消费者拿到的是已销毁的野指针 |
| M6 ✅ | `Editor/Interaction/ViewportRenderer.cpp:478` | `SetViewport(cam.center, halfW, halfH, margin)`（`Renderable.h:102` 语义为半宽/半高）被传入 `view.max.x-view.min.x`、`view.max.y-view.min.y`——是**全宽/全高**，即实际值的 2 倍 → 剔除矩形放大 2×2、剔除面积 4 倍，反向膨胀 `SpriteBatcher` 实例需求与环容量。**既是 bug 也是优化点** |
| M7 | `RHI.cpp:487` | `VkDescriptorPoolSize` 里 SSBO 计数硬编码 `4`，与 `kRingSsboSlots`、`sprite.vert:20` 的 `rings[4]` 三处字面量重复。当前值一致，改常量即静默不一致 |

**C# 桥**

| # | 位置 | 缺陷与失败场景 |
|---|---|---|
| M8 ✅ | `GameUI.cs:270,271,273,274` + `UiSubsystem.cpp:481-498` | 行块 key/字段名长度前缀写的是 `Math.Min(it.Key.Length, 255)`——C# `string.Length` 是 **UTF-16 字符数**；而 `PutBytes` 写的是 `Encoding.UTF8.GetBytes` 的**字节数**，C++ 侧 `const uint8_t keyLen = *p++; std::string key((const char*)p, keyLen)` 也按**字节**读。两侧口径不一致。图鉴条目 key 含中文（10 汉字 = UTF-8 30B，前缀记 10）→ 解码只取 10B → key 被截断且 `p` 少进 20B → 后续 `fieldCount` 全错位 → 字段乱码 / 假"行块越界"ContractFail。**有界**（C++ `need()` 不越界）但**静默损坏**；ASCII key 永远正确，故此前未见 |
| M9 ✅ | `Exports.cs:137-138`→`Lemon.Entry/Batch.cs:49`→`Lemon.SDK/Scripting.cs:102`；`Exports.cs:158`→`Lemon.SDK/Events.cs:64` | 两个 UCO 导出均无下标检查、且体外无 try/catch（均为表达式体）。①`lemon_batch_query` → `CopyQuery` → `GetQuery(i) => s_queries[i]`（`List` 索引器）：编辑器线程并发换域使 `Reset()` 清表后索引即越界 → `IndexOutOfRangeException` 逃出 UCO → coreclr abort 整个编辑器。②`lemon_events_received(type)` → `s_received[(int)type]`：数组恒 `new int[16]` 而**无 clamp**——同文件的 `DispatchPackets`（`Events.cs:72`）却写了 `t < 16 ? t : 15`，**两侧不对称**即本条的硬证据。**两条均已复核，成立** |
| M10 | `ScriptHost.cpp:540-552` | 预留量以 `countFn(scene)` 为准且**隐含其 ≥ 实际匹配数**，无断言。某组件 `countFn` 少报或注册表该帧变化 → 构造期 `push_back` realloc → 先前系统的 `fr.blocks` 悬垂 → C# 线性步进野读（正是注释里 M3-7 那类崩溃，仅靠假设兜底） |
| M11 | `GameUI.cs:103-108` | `DedupSkip` 记的是"已入列"而非"已生效"。引擎侧失败一次（文档未装载 → ContractFail）后，同值 `SetText/SetAttr` 被永久跳过，元素停在旧值，除非恰好有 `DocumentReloaded` 清表 |
| M12 | `Exports.cs:186,191,197` | attach/destroy/detach 每调 `new` 闭包（tick 路径已池化，此路径未池）→ 刷怪/频繁消亡场景每帧 GC 压力 |

**ECS / 运行时**

| # | 位置 | 缺陷与失败场景 |
|---|---|---|
| M13 ✅ | `Scene.cpp:23-35` | 无效句柄仍入队：`if (registry_.valid(ent) && …) registry_.emplace<DestroyQueueTag>(ent);` 的守卫只管打 tag，紧随其后的 `destroyQueue_.push_back(ent)` 在 `lock_guard` 内**无条件执行**。C# 持已毁句柄每帧 `Destroy(e)`（`Scene.h:8` 承诺"重复入队天然幂等"）→ 队列无界增长、`PendingDestroyCount()` 虚高、每帧空转扫垃圾；tag 打了但实体不合法时 `CommitDestroys` 还会对 tombstone 走一次无效遍历 |
| M14 ✅ | `Scene.h:94-97` + `SceneArchive.cpp:294` | `Each()` 只判 `registry_.valid(e)`，**不过滤 `DestroyQueueTag`**（`Scene.cpp:31-32` 的注释明说"提取/查询层按 DestroyQueueTag 过滤当帧待删实体"，但 `Each` 这条查询层没做）。编辑器删实体后同帧保存，或脚本 `Destroy` + `Save` → 存档含当帧待删实体，重载复活 |
| M15 | `JobSystem.cpp:136` + `JobSystem.h:53-55` | `Complete()` 只 `wait()` 不 `get()`，出 `ParallelFor` 时 `handles` 析构 → worker 抛出的异常被 `packaged_task` **静默吞掉**。`Grid::Build`/`chunkTeams_.push_back` bad_alloc → 该块剩余实体无声跳过、帧模拟半截、回放哈希漂移且零日志。另 worker 内 `LEMON_ASSERT` 走 `std::abort()`，可恢复错误升级为整进程死 |
| M16 | `SceneArchive.cpp:139` | Blob24 写出 `std::string(comp + f.offset)` 假定 NUL 结尾；`Meta.tag` 是 24 裸字节（C# `fixed byte Tag[24]`，脚本可写满 24B）。写满后 Save → 构造越过 Meta 组件扫到下一个 NUL（读邻组件，池尾即越界读），并把垃圾字节落进存档 |
| M17 | `SpatialHash.cpp:23-24`（另 `87-90/119-122/169-170`）+ `SpatialHash.h:48` | ①`(int32_t)std::floor(coord*invCell_)` 对 NaN/inf/超 int32 是 UB（x86 落 INT_MIN），实体落入任何有限查询都到不了的格子；`MovementSystem` 的 `Clamp(NaN,…)` 仍返回 NaN（`Math.h:19`）→ 位置永不修复。C# `Transform.Pos` 可写任意 float 且桥侧无校验 → 静默变幽灵。②`Configure()` 只改 `invCell_` 不失效 `items_` → 运行期改 cell 尺寸后查询错位 |
| M18 | `JobSystem.cpp:128` + `FunctionRef.h:22-27` | `ParallelFor` 以 `[&fn]` 捕获栈上 `FunctionRef`，若 `fn` 体抛异常（`M15` 同源），已入队块在栈帧销毁后仍执行 → 悬垂调用。`FunctionRef` 对 prvalue lambda 只存址不延寿，仅靠注释约束 |

**编辑器**

| # | 位置 | 缺陷与失败场景 |
|---|---|---|
| M19 | `EditorAppUiBridge.cpp:73-77` | 仅 `inside` 时转发 down/up。在 UI 按钮上按下 → 拖出 GameView 画布 → 松开 → up 丢失 → RmlUi 元素停在 active 态，之后一次无关点击即误触发拖放/连点语义 |
| M20 | `EditorAppUiBridge.cpp:78-108` vs `EditorApp.cpp:531` | UI 键盘喂入只以 `gameViewFocused_` 为门，与 `ApplyInput` 的 `!ImGui::GetIO().WantTextInput` 门不一致。焦点在编辑器 Inspector 输入框且 GameView 仍 focused → 按键同时进 RmlUi `<input>` 与编辑器控件 |
| M21 | `AssetDatabase.cpp:37-56` | manifest 原子写只 `flush` 不 `fsync`、无 `.bak`（存档路径 `EditorContext.cpp:936-937` 另有 .bak）。掉电/崩溃可得长度 0 的新文件；rename 在 Windows 覆盖失败（注释自认归 M7）；`.tmp` 与资产同目录 |
| M22 | `AnimationPanel.cpp:181-194` + `ClipEdit.cpp:74,120-132` | 删帧后不校验 events 越界：`TrySave` 只校验 frames，`edit_.events` 面板从不读，而 `ParseClipJson` 重载时硬拒 `frame≥frames.size()`。开一个带帧事件的 clip，删到事件帧号越界后保存 → 落盘 → 哈希变触发 `LoadFrom` → `ok=false` → 该 clip 保存链自锁 |
| M23 | `AnimationPanel.cpp:1022-1031,1312,1346-1349` | `ResetEditingState` 未清 inline 改名态（`segEditIdx_/segEditBuf_/segFilter_`），`SetTarget/OpenSet` 一行零重置。在集 A 第 3 行进入改名 → 右键新建集 B → B 的第 3 段命中 `segEditIdx_==3` **预填 A 的名字** → 回车把 B 的 `.anim` 改名/写盘 |
| M24 | `AnimationPanel.cpp:1284,1389-1403,1532` | `TrySaveSet` 返回值被多处丢弃。`:1389-1403` 最重：`db.Remove` 删盘上 `.anim` → 从 `segments` 抹行 → `TrySaveSet` 失败被吞 → `.override` 仍列一个已墓碑的 clip，下次重载才以"（悬空）"暴露 |
| M25 | `AnimationPanel.cpp:1243,1276-1284` | 集面无独立 dirty 标志：`dirty_` 只由 clip 帧编辑置位，集改名/加段/删段不置位。改集名不存 → 切项目/退出（`ctx_.dirty` 未置 = 无未保存确认）→ 或该 `.override` 被外部触碰触发重载 → 改名丢失，全程无提示 |

**UI**

| # | 位置 | 缺陷与失败场景 |
|---|---|---|
| M26 | `Log.cpp:34-39` | `fprintf(stderr,…)` 在 `gLogMutex` **之外**，仅计数与 sink 在锁内。JobSystem worker 与主线程同时打印（本仓确有 worker 侧 `LogMsg`）→ 日志行交错撕裂 |

> 本节两条去重说明：①`SetItems` 的 `UiOpC.strCount` 未校验与 L5 是同一处代码，已并入 L5，不单列；②"D1（IME 死）+ M20（键盘门不一致）叠加使文本输入链无任何冒烟断言覆盖"是**组合观察而非独立缺陷**，已并入 D1 的"修复方向"（补 IME 冒烟断言）。

### 6.3 低 severity（14 条）

| # | 位置 | 缺陷 |
|---|---|---|
| L1 | `TeamTable.h:22-27` vs `:30` | `SetRelation` 只 `CheckTeam` 不钳，越界即 abort；读路径 `Relation()` 返回 Neutral。**写致命、读宽容**不对称——手改 prefab 的 team=40 → 进 Play 直接 abort |
| L2 | `ClipTable.cpp:31-51` | `RegisterSet` 同 setId 不先清旧名单：重复登记时旧段名仍可解析、新段名被 `emplace` 失败而指回旧 clipId。现唯一调用方每局新建 World 故未触发，属潜伏契约 |
| L3 | `SceneArchive.cpp:401-424` | `Migrate` 无条件 `doc["schemaVersion"] = kSchemaVersion`：后续加 v2→v3 时，从 v1 来的档会**跳过中间级**，迁移链断点 |
| L4 | `SceneArchive.cpp:71-89`、`:82-83` | `ReadArraySeg` 对元素字段传 `remap=nullptr` → 段内 `EntityRef` 恒判越界返回 false 被静默丢弃。当前 4 个 seg 均无 EntityRef，但 `kWaveDefElem`/`kItemStack` 是既定扩展点，加字段即隐性失效 |
| L5 | `UiSubsystem.cpp:936-942` | `SetItems` 的行块解码边界只由 `op.i0` 与 arena 剩余长度决定，`UiOpC.strCount` **全程未被解码侧校验**——线格式两侧漂移时无法被发现，只能靠契约错误计数间接触发。与 M8 同属"C#/C++ 双方对同一线格式口径不一致"这一类 |
| L6 | `Renderable.cpp:174` | `((uint64_t)e.seq & 0xFFFFFFull)` 在 2^24 次 `Create` 后回绕，同桶内 `sortKey` 可逆序 |
| L7 | `Particles.cpp:149` | 桶内排序键取池下标，`Simulate` 的 swap-and-pop 使下标帧间漂移 → 同批 alpha 粒子叠序逐帧变化（Multiply 混色下可见闪动） |
| L8 | `Systems.cpp:370-374` | 工厂失败回写用 `dw->waves[dw->waveIndex-1]`，若同 tick 已推进 waveIndex 则写到新波：旧波条目不废止、下 tick 重试多生，新波 `waveSpawned` 被清零 |
| L9 | `Hierarchy.cpp:120-136` vs `Hierarchy.h:32` | `SceneDestroyEntityTree` 实调 `s.Destroy(d)`（两阶段入队），与注释"直接提交销毁，不经 destroyQueue_"相反；子树 walk 无深度/环 guard（`IsDescendantOf`/`SubtreeHeight`/`CollectSubtree` 均有 guard，唯独这里没有）→ 脏链无限循环 OOM |
| L10 | `RingQueue.h:18`、`Systems.cpp:1149` | 注释"EventPacket 32B → 32MB 上限"实为 48B（真上限 48MB）；`an.curFrame=(uint16_t)f` 在 clip 帧数 >65536 时帧号截断，AnimFrame 重发（理论路径） |
| L11 | `Anim.cs:56,168`、`Table.cs:47` | `s_missed`/`s_paramMissed`/`s_warned` 不在 `DomainManager` 复位集内 → 长期局内大量解析失败时 HashSet 无界慢增 |
| L12 | `Exports.cs:326` | `lemon_blit_copy` 的 `LayoutTables.Comps[compId]` 无界；`:36,39` `GetLoadContext(...)!.Name` null-forgiving。诊断/测试通道，坏 compId 即 abort |
| L13 | `NativeApi.cs:100,111` | `ComponentTable.Id<T>()` 用索引器，未绑定镜像类型抛 `KeyNotFound`，仅靠 `SafeCall` 逐行为兜（拼错类型会触发 60 帧禁用）。宜改 `TryId` |
| L14 | `DomainManager.cs:104,143,193` | `SceneOps.Reset()` 丢弃未应用的 pending op（已入队未生效的 Destroy/AddComponent 跨换装丢失）——AGENTS.md 已登记债务 |

---

## 7. 优化空间

### 7.1 性能

1. **模拟/渲染分线程（最大杠杆，设计已规划未落地）**。当前单主线程串行：`World::Step` 与整帧渲染在同一线程（`EditorApp.cpp:555-600`）。割草品类压测 A 的模拟预算是 ≤10ms/帧，一旦接近就会与渲染的 ~4ms/帧直接叠加。JobSystem 已就绪（`N = hw_concurrency - 2` 工作窃取池），缺的是**帧数据所有权划分**：把 `Renderable` 双缓冲真的交给渲染线程消费、模拟线程只写 `activeBufferIndex`。这也是 §6 D4 从"纪律违反"升级为"数据竞争"的前置条件——**分线程之前必须先修 D4**。
2. **修 M6（剔除区 4× 虚胖）= 免费的合批收益**。`ViewportRenderer.cpp:478` 把全宽当半宽传给 `SetViewport`，剔除面积放大 4 倍，直接膨胀 `SpriteBatcher` 实例需求与环容量。改一行，既消 bug 又降压。
3. **`EnsureCapacity` 扩容 `WaitIdle` 全停顿**（`SpriteBatcher.cpp:66-79`）。建议改为按帧预算渐进扩容，或把 `kInitialCapacity` 按上次运行峰值持久化（与 pipeline cache 同目录）。
4. **ring 常驻显存 = 3 × 容量 × 2 视口**。两个视口各持一份 ring（`ViewportRenderer.cpp:337-338` 注释"实例环各占一槽"），实际可用一个共享 ring + 两个 slot 前缀。
5. **C# 边界已接近最优**（64/块、~66µs/帧往返、热路径零托管分配）。剩余可捡的是 M12（attach/destroy 闭包池化）与 L11（诊断 HashSet 复位）。
6. **ECS 热路径**：`Pool<T>()` 直访 + `EnttIndex/EnttVersion` 直下标 + 纪元差集释放都已做。下一档优化是把 `(Transform2D, Velocity)` 这类超高频组合的手工 packed 遍历固化，避开 `registry.view()` 的通用开销——但收益需 profiler 数据支撑，**不建议预设**（与 01 文档 §3.1 的"不预设 SIMD"纪律一致）。
7. **`destroyMutex_` 是 `ParallelFor` 内的全局串行点**（`Scene.cpp:29-35`）。`ProjectileLifetimeSystem` 在每个 worker 内逐个调 `Scene::Destroy`，每次都抢同一把全局锁。正确性无恙（见 §6 高 severity 末尾的勘误），但 10 万投射物同 tick 到寿时锁竞争会成为并行收益的天花板。改为 per-worker 本地 vector 累积 + parallel 归并后单线程统一打 tag，即可把这部分从"串行"变回"并行"。**应与 §7.1.1 分线程同批做**。

### 7.2 结构

1. **补 `Engine/Assets` 运行时资产层（M7 的真实阻塞项）**。当前运行时零 PNG 解码能力（§5.4），纹样全靠编辑器进程上传。最小可用面 = stb 解码 + 图集烘焙 + `.baked` 读写 + GUID/manifest 运行时读取。**在这层补齐前，M7 packager 无法开工**，"同源双入口"（ADR-005）也无法兑现。
2. **编辑器/运行时加载代码共源**。现状 `AssetDatabase`/`AssetGpuCache` 住在 `lemon-editor-core`，运行时另有 AtlasRegistry 路径，连 `HexToGuid` 都在桥内自带一份 mini 解析（`ScriptHost.cpp:209` 注释自陈）。违反设计原则 4"单一数据事实源"。
3. **`EditorApp` ~80 成员按所有权划界（最后一次低成本减脂窗口）**。已外迁 12 个 TU 但状态没拆。建议按"会话态 / 构建态 / Play 态 / 冒烟态 / 终验计量"划 4-5 个 struct，让 TU 边界与状态所有权对齐。再搬两批 TU 就到收益递减点。
4. **冒烟注入链从帧号锚定改为语义句柄**。`if (frame == N)` 散落 9 个 TU，加一步就要改主循环 + 帧预算。`TestHooks` 已有"控件矩形 Stash"机制，扩成"语义句柄表"即可把断言改成事件驱动，消除负载抖动导致的"首跑红复跑绿"。
5. **常量单一来源**。`kMaxTextureSlots`/`uAtlases[64]`/`kMaxSamplerSlots`/`kMaxTextureSets`/`kRingSsboSlots`/`presentSemaphores` 尺寸六处字面量各自为政（D10/M3/M7）。建议构建期生成一份 shader 侧常量头，CPU/GPU 同源。
6. **组件镜像 struct 用 Source Generator**。31 个手写 `[StructLayout]` + 双向 `static_assert` 是重复劳动，AGENTS.md 已登记为 v1.1 项，宜早不宜晚。
7. **CI 落地（M7 Gate C 前置，至今为零）**。仓库无 `.github/`、无任何 CI 配置；`CMakePresets.json` 只有 `mac`/`mac-debug`，README 里"预设 `win` 待 M1 添加"至今未兑现。16 步回归链已有，缺的只是把它挂到 runner 上。
8. **`demo/`、`ats/`、`etest/` 均在 `.gitignore`**。M6a 的验收①（"用户幸存者项目全流程零 C++ 可玩"）的**被测对象本身不在版本控制内**，其回归基线归属模糊。建议至少把 `demo/svr-test` 纳入，或明确三者定位为纯本地手测、不承载验收判据。
9. **把 01 文档的目录树改成"现状 / 目标"两栏**。§5 的 8 条分叉会持续误导新人，尤其是 5.1/5.4（不存在的目录 + 不存在的运行时资产层）。

---

## 8. 建议的修复优先级

**第一批（1～2 人日，纯收益无风险）**

| 项 | 内容 |
|---|---|
| D1 | `SdlTextInputHandler` 构造函数分配 `impl_` + `HandlerPtr()` 断言 + 补 IME 冒烟断言 |
| D10 | 常量同源（shader 侧数组改 256 或生成常量头）+ `AssetGpuCache` 满槽判定对齐 + 65 资产冒烟断言 |
| M6 | `SetViewport` 半宽/全宽传参修正（顺带拿 4× 剔除收益） |
| D3 | `SpriteBatcher::Init()` 幂等化（开头摘旧回调） |
| M1 | `droppedSprites`/`droppedParticles` 接一个红字告警或 ProfilerPanel 展示（计数器已存在，只是无人读） |
| M26 | `Log.cpp` 的 `fprintf` 移入锁内 |

**第二批（3～5 人日，数据完整性）**

| 项 | 内容 |
|---|---|
| D2 | acquire/recreate 路径的 `imageIndex` 复位或 `continue`（EditorApp + 5 个 Sample，共 6 处调用点同款） |
| D6 / D7 / M23 / M24 / M25 | AnimationPanel 的 ro 守卫 + JSON 转义 + 改名态复位 + 返回值检查 + 集 dirty |
| D8 | UI 文档装载失败回滚 |
| M21 | manifest 原子写补 `fsync` + `.bak` |
| M13 / M14 | 无效句柄不入队 + `Each()` 过滤 `DestroyQueueTag` |
| M15 / M18 | `JobSystem::Complete()` 改 `get()`（异常显式化） |

**第三批（1～2 周，需要设计决策）**

| 项 | 内容 |
|---|---|
| D4 | UI 事件改走域线程（与 `PostBatchEvents` 对称）——**必须在 §7.1.1 分线程之前完成** |
| M12 | 借 §7.1.1 分线程之势，把 `destroyMutex_` 换成 per-worker 本地 vector + 归并（现为全局串行点，见 §7.1 第 7 项） |
| D5 / M10 | 批量帧指针失效护栏（`batchStale_` 标志或禁用同步生成通道） |
| M3 / M4 / D9 | RmlUiBackend 资源管理：描述符集耗尽显式失败、几何池降级、外部纹理失效钩子 |
| M9 | UCO 导出全量 try/catch + 下标检查（对齐 M4.6 纪律） |

**第四批（结构性，M7 前置）**

`Engine/Assets` 运行时资产层 → 编辑器/运行时共源 → CI + Windows preset → 01 文档"现状/目标"两栏化。

---

## 9. 评审范围、方法与局限

- **已全覆盖**：`Engine/` 全部 45 个源文件、`Editor/` 全部 46 个源文件、`Engine/Scripting/dotnet/` 的 Entry/SDK 两侧、`docs/EngineDesign/00-09`、CMake 与 `tools/editor-regression.sh`。
- **深度评审分工**：渲染/RHI、ECS/运行时、C# 桥、UI/序列化、编辑器五路并行通读 + 交叉实证；标注 ✅ 的 **7** 条由主审人沿代码路径亲手追到底（含 RmlUi 6.3 上游 `WidgetTextInput.cpp` 的 null 守卫核对）。
- **实证标准**：每条 ✅ 均满足"读到该行的原始文本 + 沿调用链走通 + 有可指认的失败机制"。复核中以此标准**证伪了 1 条初版缺陷**（D12，见 §6 高 severity 末尾勘误）——即初版 51 条中：证伪 1 条（D12）、去重合并 2 条（原 M26 并入 L5、原 M28 为组合观察并入 D1）、新证 6 条，最终保留 48 条。凡未标 ✅ 的条目来自子系统深挖所得的交叉行号证据，未经主审人逐行复核。
- **局限 1**：**纯静态评审，未运行引擎**。所有"失败场景"均为代码路径推演，未经动态复现。申报缺陷前建议先按 §8 第一批写一条最小复现（多数只需一条冒烟断言）。
- **局限 2**：编辑器那一路因 API 并发中断，最终只收到 AnimationPanel/ClipEdit 深挖的补充清单。InspectorPanel / AssetBrowserPanel / HierarchyPanel / UndoStack / ProjectWizard / ViewportPanels 未获得同深度覆盖，§6.2 编辑器部分（M19–M25）**不构成完整清单**。
- **局限 3**：M9 的两条（`lemon_batch_query`/`lemon_events_received` 无下标检查）已复核到 `Lemon.SDK/Scripting.cs:102`（`GetQuery(i) => s_queries[i]`，`List` 索引器）与 `Lemon.SDK/Events.cs:35,64`（`s_received` 恒 `new int[16]`，`ReceivedCount` 无 clamp 而 `DispatchPackets:72` 有 clamp，两者不对称），确认成立。但"UCO 缺 try/catch"是**抽样结论**：45 个 `[UnmanagedCallersOnly]` 导出中只有约 5 个带 `try`，未逐一枚举，§8 第三批的"全量 try/catch"工作量按此估算。
- **局限 4**：`ats/`、`etest/`、`demo/` 三个用户工程目录被 `.gitignore`，未纳入评审。若它们承载真实验收判据（见 §7.2.8），其暴露的问题不会出现在本报告中。
- **局限 5**：行号以基线 `3597032` + 未提交工作树为准；审查期间工作树仍被推进，后续提交可能使行号漂移。
- **未评级但值得留意**：热重载每次泄漏 ~百 KB（ALC pin，.NET runtime 限制，已登记）；`dotnet build` 阻塞主线程 1-2s；`GameUI`/`RtUi` 两条 UI 兼容层并存至 M8。

---

*本报告由主审模型综合五个并行子系统评审产出，评审过程中未修改任何源码；报告本身按 `docs/README.md` 的 Reports 区体例撰写（一次性快照，产出后不更新），勘误以「追记」小节追加。*
