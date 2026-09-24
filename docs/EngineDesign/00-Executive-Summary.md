# Lemon 引擎设计 — 00 执行总纲

> **引擎名称：Lemon**（2026-09-18 用户正式定名；此前工作代号 Harvest，已全文替换）。
> 仓库：`GameEngine/Lemon/`；C++ 命名空间 `lemon::`；C# 命名空间 `Lemon.*`；编辑器 `LemonEditor`。
> 文档状态：设计阶段 v1.2（2026-09-18 ADR-009 修订：Prowl2D 升格为理念/API 蓝本 + C# 门面 Unity 命名级对齐）。本文是整套设计文档的入口与结论页。
> 前序路线（Prowl2D，纯 C# fork）已归档，定位为**理念与 C# API 风格蓝本**（[ADR-009](../ADR/ADR-009-Prowl2D-Blueprint-and-Unity-API-Alignment.md)），见 §9。

---

## 1. 一句话定位

**Lemon 是一款开箱即用的纯 2D 游戏引擎：C++20 + Vulkan 渲染内核提供"万级怪物同屏割草"的性能底座，C# 脚本（混合模型）提供 Unity 风格的上手体验，ImGui 编辑器提供 Unity 式 Hierarchy/Inspector/Play-Stop 工作流；坚决不做 3D。**

对标组合：

| 对标对象 | 我们取什么 | 我们改什么 |
|---|---|---|
| yami-rpg-editor | 开箱即用范式（模板即游戏、默认素材、一键出包上 Steam） | 渲染与逻辑内核：Electron/WebGL/JS → C++/Vulkan/C# |
| Unity | 编辑器工作流（Hierarchy/Inspector/Play 沙盒）与组件心智模型 | 去掉 3D 与重物理；数据导向内核；轻量编辑器（秒开、无工程化负担） |
| Luma | C++ 高性能架构（提取-插值-合批渲染、JobSystem、CoreCLR 宿主 C#） | 聚焦 2D 割草品类；Vulkan 直写而非 Dawn/WebGPU；更薄的体量 |
| MoteurJV | 编辑器交互设计（Play 快照沙盒、行为组件目录） | 自研 ECS（EnTT）替代教学级 Registry；Vulkan 替代 raylib |
| Editor-RPG2D | 编辑器交互工程小件（模式栈、GUI 焦点仲裁、放置状态机） | 补齐它缺失的运行时/脚本/发布，成为完整引擎 |
| Prowl2D | **理念与 C# API 风格蓝本**（ADR-009）：Unity 无缝过渡目标、GameObject 命名级 API、SceneDispatcher/PrefabLink/Undo 双轨等已验证设计 | C#+OpenGL 底层 → C++/Vulkan；3D 遗留全砍；OOP 内核 → ECS 内核 + 门面 |

## 2. 设计原则（按优先级排序，冲突时靠前优先）

1. **性能是产品的一部分**：目标场景（万怪割草）性能不达标的功能不算完成。每个系统设计时先回答"10,000 实体下它的成本是多少"。
2. **开箱即用压倒一切灵活性**：新建项目 → 5 分钟内看到一只可玩 demo 在跑。默认值即最佳实践，配置项宁少勿多。
3. **热路径 C++，玩法 C#，边界最少化**：逐实体逐帧的代码留在 C++（数据驱动 + 配置），跨语言调用只发生在"批量 API + 事件队列"两个通道上。
4. **坚决不碰 3D**：没有 3D 数学、没有 Z 轴语义（2D = XY 平面 + SortingLayer，Unity 2D 同款）、没有骨骼/光照探针等 3D 遗产。API 表面永远干净。
5. **不做重物理**：只要 overlap/raycast/触发器查询层 + 运动学积分 + 分离力。没有刚体求解器、没有关节堆叠。跳跃平台类不是目标品类。
6. **不做网络/联机**（2026-09-24 显式化）：目标品类全部单机（ARPG/塔防/吸血鬼幸存者/增量）；确定性回放只做开发者回放（03 §12），不做联机回滚/快照同步/网络实体协议。
7. **数据驱动 + 可 diff**：场景/组件/配置全部是带 schema 的 JSON（地形等大数据用行程编码压缩），git 友好，编辑器与运行时读同一份格式（单一事实源，杜绝 yami 的"双实现漂移"）。
8. **单人可维护**：所有自研模块必须"一个人写得完、一个人改得动"。引入第三方库优先于自研（EnTT/VMA/SDL3/ImGui/miniaudio 等，见 01 文档选型表）。

> **基调（ADR-009）**：API 表面像 Unity/Prowl2D（上手体验），底层实现永远性能优先（C++ 内核、批量边界、热路径零托管分配）。表面相似绝不意味着机器相似。

## 3. 目标品类与引擎能力映射

| 品类 | 核心压力点 | 引擎支撑系统 | 优先级 |
|---|---|---|---|
| **吸血鬼幸存者类（首发验证品类）** | 万级怪物 + 弹幕 + 特效同屏；刷怪导演；大量命中判定 | 实例化合批渲染、空间哈希 broadphase、对象池、命中盒系统、刷怪导演、粒子池、经验/掉落物系统 | P0 |
| **塔防** | 寻路（怪群）、格子放置交互、波次编排 | Tilemap + chunk 烘焙、FlowField 流场寻路、放置交互状态机（编辑器/游戏同源）、波次导演 | P1 |
| **ARPG** | 技能/状态机/势力敌对/装备数值/剧情对话 | 技能系统（弹幕/命中盒/冷却）、状态效果、Team 势力表、背包/装备数据组件、（后期）可视化事件树 | P1 |
| **增量放置** | 超长时间运行稳定性（数值溢出、存档体积）、大量 UI | 大数（double + 后期 BigInteger 桥）、离线结算钩子、存档分通道、UI 层 | P2 |

**共性风格约束**（用户明确）：怪物特别多、割草爽感优先；不深入依赖物理跳跃的玩法。因此：渲染与模拟的吞吐 > 物理精度；分离力/软碰撞 > 刚体碰撞；位图数字飘血 > 动态文本栅格化。

## 4. 性能预算（验收红线）

**基准机型**：中端独显（GTX 1660 / RX 5600 级别），1080p，Windows 10/11 原生 Vulkan。
**开发机**：macOS + MoltenVK 只要求"跑通与调试"，不背性能指标。

| 场景 | 组成 | 目标 |
|---|---|---|
| **压测 A：割草极限** | 10,000 活跃怪物（带动画+移动+分离力）+ 50,000 投射物 + 100,000 粒子 + 全屏特效 | ≥ 60 FPS；模拟 ≤ 10 ms/帧，渲染提取+提交 ≤ 4 ms/帧 |
| **压测 B：编辑器开发流** | 上述场景在编辑器 Play 模式下 | ≥ 45 FPS；进入 Play < 0.5 s；停止恢复 < 0.3 s |
| **压测 C：常态游玩** | 1,000 怪 + 2,000 弹幕 + 5,000 粒子（实际游戏典型峰值的 3 倍余量） | ≥ 144 FPS（高刷屏顺滑） |
| 冷启动 | 引擎 + 模板项目加载到可操作 | 编辑器 < 2 s；打包后游戏 < 1.5 s |
| 内存 | 压测 A 场景常驻 | < 1.5 GB；连续运行 2 h 无增长（无泄漏） |
| GC（C# 侧） | 压测 A 场景 | Gen0/Gen2 GC 单帧中断 < 1 ms；热路径零分配（用统计面板验证） |

预算分解到各子系统（渲染/模拟/脚本桥的具体毫秒数）见 [02](./02-Rendering-Vulkan.md) 与 [03](./03-ECS-Runtime.md)。

## 5. 六引擎对照结论（各取什么，一句话版）

| 引擎 | 一句话结论 | 对 Lemon 的最大贡献 |
|---|---|---|
| **Luma**（C++20 + Dawn/WebGPU + CoreCLR，MIT，17.8 万行 C++） | 商业级体量的数据驱动 2D 引擎，架构与我们的需求几乎完全对口，但其 Dawn/WebGPU 抽象层不是我们想要的 Vulkan 直写 | ①"提取-双缓冲-插值-合批"渲染流水线（`Application/RenderableManager.h`，仅 93 行头，直接移植）；②POD 合批键（`SceneRenderer.h` FastSpriteBatchKey）；③CoreCLRHost C# 宿主全套（`Scripting/CoreCLRHost.*` + Luma.SDK）；④粒子 GPU 布局（`Data/ParticleData.h` 4×vec4）；⑤JobSystem 工作窃取（`Event/JobSystem.h`）；⑥编辑器面板框架与 ComponentRegistry 反射 Inspector |
| **yami-rpg-editor**（Electron+WebGL+TS，**MIT**，编辑器 10.7 万行 JS + 运行时 4.3 万行 TS） | 完整度最好、上过 Steam 的 RPG 引擎；开箱即用范式最完整，但性能天花板在 JS 逻辑层与 Electron 壳 | 数据 schema 与设计思想移植（代码虽为 MIT，但 JS/TS 对 C++ 引擎无拷贝价值）：GUID+manifest 资产管线、场景 JSON+行程编码格式、Team 势力表 schema、事件指令树的数据格式（后期）、F3 性能面板统计项、Deployment 产物清单、双通道存档接口形状；**默认素材（MIT）可直接用作我们的模板素材底包**（发布物保留版权声明） |
| **MoteurJV**（C++17 + raylib + ImGui，MIT，引擎仅 4606 行） | 教学级小引擎，但编辑器交互设计（单文件 2035 行）是 Unity 风格编辑器的最佳最小样板 | ①Play/Stop 整场景 JSON 快照沙盒；②Undo 快照器（ADR-009 后主方案换 Prowl2D 属性级双轨，快照器保留 Play 沙盒与退化路径）；③**行为组件目录**（Spawner/Chase/Shooter/Projectile/Hazard/Collectible…）——这就是割草品类的预制组件面，直接采纳；④"后端零泄漏"门面纪律 |
| **Editor-RPG2D**（C++23 + SFML3，开放署名，2.7 万行） | 纯编辑器无运行时，但编辑器交互工程是高质量参考实现（30 控件 + 4 编辑器） | **代码可拷（注明出处）**：①编辑器模式栈（`EditorsManager.cpp` ~40 行）；②GUI 双指针焦点仲裁（`GUIManager.cpp`）；③"GUI 未占用才轮到场景"放行约定；④放置交互状态机（`CursorOnMap.cpp`，塔防摆塔直接用）；⑤自动瓦片 Group+friends 数据结构；⑥一 chunk 一 VertexArray 烘焙 + 相机剔除 |
| **Looper**（C++20 + 原生 Vulkan + ImGui，约 1.77 万行，本地副本缺 LICENSE） | 唯一的"裸 Vulkan 2D 精灵 + 编辑器"实操样本 | 实现期对照代码（**暂只借鉴不拷贝**，待核实上游许可）：per-instance SSBO 海量精灵（`engine/renderer/renderer.cpp:854-915`）、脏更新、VMA 用法、编辑器 gizmo |
| **duality**（C# + OpenTK，MIT，官方停更） | 老牌 C# 2D 框架，Tilemaps 插件成熟 | MIT 可拷：Tilemap dirty-rect 更新协议、gzip 版本化序列化、32×32 扇区碰撞思路、SpriteAnimator 的 LoopMode/帧序列设计 |
| **Prowl2D**（我们自己的前序路线，C# fork，M0 已完成） | **理念/API 蓝本（ADR-009）**。已验证的经验：图集/合批/排序层是 2D 性能地基；macOS 旧 GL 驱动有逐 draw 着色器重编译坑（Vulkan 天然绕开） | ①品类优先级排序与"图集合批先行"的结论；②性能验收场景设计（500 怪压测模板）；③"单测全绿 ≠ 编辑器可用"的教训 → 每里程碑必须有 GUI 级冒烟验收；④理念与 C# API 风格蓝本（Unity 命名级对齐 + SceneDispatcher/PrefabLink/Undo 双轨/Camera2D/UGUI 候选等六项吸收，07 §1.7） |

完整移植矩阵（模块 × 路径 × 合规 × 里程碑）见 [07-Porting-Matrix.md](./07-Porting-Matrix.md)。

## 6. 总体技术形态速览

（详细论证在 [01-Architecture-Overview.md](./01-Architecture-Overview.md)）

- **语言与运行时**：内核 C++20；脚本 C#（.NET 10，CoreCLR 经 hostfxr 宿主进引擎进程）；编辑器 C++（ImGui docking）。
- **图形**：Vulkan 1.3 + VMA，Windows 原生优先；macOS 开发期走 MoltenVK；RHI 薄层留 Metal 后门（不实现，只保证不写死）。
- **ECS**：EnTT（C++ 侧唯一实体模型）；C# 门面以 **GameObject 为正名**（同一 EntityHandle 的别名，Unity 命名级对齐，ADR-009），不重复造 ECS。
- **脚本混合模型**：①脚本组件（`LemonBehaviour` 挂实体，Unity 命名生命周期）；②批量系统（C# `IForEachSystem` 在 C++ 遍历中按块回调）；③原生系统（C++ 数据驱动，C# 只配置/订阅事件）。
- **平台层**：SDL3（窗口/输入/手柄/音频经 miniaudio 或 SDL_audio 混合）。
- **数据**：JSON（带 schema 版本）+ 行程编码大数组；GUID 资产引用 + manifest 索引。
- **编辑器与运行时**：同源代码库双入口（`EditorEntry` / `GameEntry`，Luma 模式），编辑器内嵌运行时做 Play 沙盒。

## 7. 文档导读

| 文档 | 内容 | 读者时机 |
|---|---|---|
| [00 本文档](./00-Executive-Summary.md) | 定位、原则、性能预算、对照结论 | 所有人首先读 |
| [01 架构总览](./01-Architecture-Overview.md) | 分层、帧循环、线程模型、技术选型、目录结构、构建 | 动工前必读 |
| [02 渲染内核](./02-Rendering-Vulkan.md) | Vulkan RHI、合批、粒子、文本、光照、后处理 | M0–M1 |
| [03 ECS 运行时](./03-ECS-Runtime.md) | 组件目录、系统管线、空间分区、物理查询层、寻路、导演 | M1–M2 |
| [04 C# 脚本层](./04-CSharp-Scripting.md) | CoreCLR 宿主、SDK API、混合模型、热重载、调试 | M0 spike + M3 |
| [05 编辑器](./05-Editor.md) | 面板框架、Play 沙盒、Undo、Inspector、各编辑器 | M4–M6 |
| [06 资产管线与开箱即用](./06-Asset-Pipeline-Out-of-Box.md) | GUID/manifest、导入、图集、模板项目、发布、Steam | M4–M7 |
| [07 移植矩阵](./07-Porting-Matrix.md) | 从哪个引擎移什么、怎么移、合规红线 | 各里程碑动工前查表 |
| [08 开发路线图](./08-Development-Roadmap.md) | M0–M8 里程碑、验收标准、风险台账、砍单顺序 | 排期与每周对照 |
| [09 测试方法与实测](./09-Testing.md) | 测试纪律、bench/冒烟程序用法与判读、专项开关、验收基线 | 动工与验收时 |

## 8. 命名记录

**正式名称：Lemon**（2026-09-18 定名）。设计期曾用工作代号 Harvest，定名后已在全部文档、路径、命名空间中替换：

| 项 | 值 |
|---|---|
| 仓库 / 根目录 | `GameEngine/Lemon/` |
| C++ 命名空间 | `lemon::`（RHI 子空间 `lemon::rhi`） |
| C# 命名空间 / SDK | `Lemon.*` / `Lemon.SDK` |
| 编辑器可执行 | `LemonEditor` |
| 项目清单 | `project.lemon`（Godot `project.godot` 同款模式；2026-09-19 定，原 `Game.project` 更名） |
| 项目/编辑器状态目录 | `.lemon/`（gitignore；资产索引 `manifest.json` 等生成物落于此）；扩展名与 Unity 惯例一致：场景 `.scene`、Prefab `.prefab`、数据资产 `.asset`、烘焙产物 `.baked`（2026-09-19 定；`.lscene`/`.lbaked` 仅扩展名更名、格式 schema 不变。`.lemon` 一名两用——`project.lemon` 的扩展名与点前缀状态目录，文件系统上无冲突） |
| 脚本域程序集 | `Lemon.ScriptLib`（引擎内置 C# 库） |

## 9. 与 Prowl2D 前序路线的关系（归档声明 + 理念/API 蓝本，ADR-009）

- Prowl2D（`GameEngine/Prowl2D/`，纯 C# fork Prowl + Silk.NET/OpenGL，M0 渲染地基与编辑器 2D 化已完成，22 个提交）**归档为参考实现**：不再新增功能，保持可编译；其仓库、文档、驱动坑记录全部保留。
- **2026-09-18 升格（ADR-009）**：归档状态不变，但 Prowl2D 升格为 Lemon 的**理念、工作流与 C# API 风格蓝本**——目标与其一致（Unity 开发者无缝过渡、KISS、小而可定制），操作理念以"高度类似 Unity 的 2D 引擎"为准；已验证设计按 07 §1.7 清单逐项吸收。代码级供体仍为 Luma 等（Prowl2D 的 C# 代码自有可拷，但语言不同复用价值低）。
- **决策理由**：新路线（C++/Vulkan/C# 混合）在性能上限（万怪割草）、内存控制、渲染掌控力上与目标品类的匹配度显著更高；Prowl2D 路线的性能天花板受限于单 C# 语言与 OpenGL 驱动质量（本机 macOS 已实测旧 AMD GL 驱动的逐 draw 着色器重编译问题；Vulkan/MoltenVK 天然绕开该类 legacy 路径）。
- **回退保险**：M0 是 3 周技术验证 spike（见 [08](./08-Development-Roadmap.md)），设有明确 Go/No-Go 判据；若 Vulkan 内核或 C# 桥任一验证失败，正式回退 Prowl2D 路线并重启其 M1。
- **继承资产**：Prowl2D 的品类优先级结论（图集+合批+排序层先行）、性能压测场景设计、"每里程碑 GUI 级验收"纪律、AGENTS.md 中的环境坑记录，全部作为输入并入本套文档。

---

*本文档由主模型（GLM-5.3）综合三个分析子代理（GLM-5.3-Flash）对 Luma / yami-rpg-editor / MoteurJV / Editor-RPG2D / Looper / duality / Prowl2D 的源码级调研结果撰写。分析原文要点已并入 07 移植矩阵。*
