# Lemon 引擎设计 — 07 移植矩阵

> 回答"从哪个引擎移什么、怎么移、何时移、合不合规"。每次动工前查本表；新移植项必须先在此登记。
> 移植方式分级：**A 直接拷贝**（改命名空间即用）｜**B 改造移植**（结构保留、实现替换/裁剪）｜**C 思想/schema 移植**（重写，源仅作设计依据）｜**D 对照参考**（实现期对着看，不拷）。

## 0. 合规红线（先读这个）

| 引擎 | 许可 | 结论 |
|---|---|---|
| Luma | MIT | 可拷代码，**保留版权声明**（第三方目录 LICENSE 汇总 + 源文件头短注） |
| MoteurJV | MIT | 同上 |
| duality | MIT | 同上（归属注释） |
| Editor-RPG2D | 开放署名 | 可拷代码，**注明出处与作者** |
| yami-rpg-editor | **MIT**（仓库根 `LICENSE`，© 2025 Yami & Xuran & Contributors；经用户确认） | 代码法律上可拷，但 JS/TS 对 C++ 内核无拷贝价值 → 维持 C 级（schema/思想移植）；**默认素材可直接采用**（含随模板再分发），在 `THIRD_PARTY.md` 与发布物致谢页保留 MIT 声明 |
| 2DGameEngine | 无 LICENSE | 禁止拷贝；仅算法思想（已被 Prowl2D 规划定性，沿用） |
| Looper | 本地副本缺 LICENSE 文件 | **暂按 D 级对照**；动工前向上游仓库核实许可后再升级 |
| rbfx（Urho3D 分支） | MIT（仓库根 `LICENSE`） | 可拷代码（保留版权声明）；当前定位 **D 级对照**——RmlUi↔引擎渲染层适配与多实例 UI 子系统结构的长期维护范例（ADR-008 接入形态依据） |
| Prowl2D（自有） | 自有 | **理念/API 蓝本（ADR-009）**：设计思路、工作流与 API 风格任意复用；经验/结论任意复用；代码自有可拷但语言不同复用价值低 |

> 通用纪律：所有 A/B 级移植在 `Lemon/THIRD_PARTY.md` 登记（来源仓库/commit/许可/改动摘要）。

## 1. 移植总矩阵

### 1.1 Luma（MIT，性能与 C# 宿主主来源）

| 模块 | 源路径 | 级 | 去处 | 里程碑 | 估工 |
|---|---|---|---|---|---|
| 提取-双缓冲-插值渲染管理器 | `Application/RenderableManager.h/.cpp`（93 行头） | A→B | `Renderer/Batch/RenderableManager` | M1 | 3 天 |
| POD 批键 + 预计算哈希 | `Application/SceneRenderer.h`（FastSpriteBatchKey/TextBatchKey） | A | `Renderer/Batch/BatchKeys` | M1 | 1 天 |
| 粒子数据布局 + 池 + 发射器 | `Data/ParticleData.h`、`Particles/Emitter.*`、`ParticleRenderer.*` | A→B | `Renderer/Particles/`（裁剪 3D 项；WGSL→GLSL） | M1 | 5 天 |
| CoreCLR 宿主全套 | `Scripting/CoreCLRHost.h/.cpp` + `Scripting/binding/Luma.SDK/`（ScriptLoadContext/DomainManager） | A→B → **实为 D**（ADR-010：以 M0 spike-03 本机实测代码为基底自研，Luma 降为对照） | `Scripting/`（M3-2a ✅ CoreCLRHost 已落地；vendored .NET hosting 头已登记 THIRD_PARTY） | M0 spike + M3 | 4+10 天 |
| JobSystem 工作窃取 | `Event/JobSystem.h/.cpp` | A→B（Schedule 改值语义） | `Core/JobSystem` | M2 ✅ 已移植（+ParallelFor/单线程诊断档；版权注记见文件头） | 2 天 |
| 系统三类调度 | `Systems/SystemsManager.h`、`Systems/ISystem.h` | C | `ECS/SystemPipeline` | M2 ✅（Essential/FixedTick + 声明式 After 拓扑 + 每系统计时） | 1 天 |
| 编辑器面板框架 + 面板集 | `Application/Editor/`（IEditorPanel + 20 面板 + ComponentCatalog + InspectorUI） | B（裁剪至 2D 面板集） | `Editor/Panels/` | M4 | 持续 |
| 资产管线骨架 | `Resources/`（AssetMetadata/Importers/FileWatcher/AssetPacker） | B | `Assets/` + `Tools/packager` | M4/M7 | 8 天 |
| RHI 切面（Context/ShaderCache/MSAA/设备丢失） | `Renderer/Nut/`、`Renderer/GraphicsBackend.h` | C（接口蓝本） | `Renderer/RHI/`（Vulkan 重实现） | M0-M1 | — |
| 2D 延迟光照/SDF 阴影 | `Renderer/DeferredRenderer.*`、`Systems/ShadowRenderer.*`、`Shaders/GBuffer|DeferredLighting|SDFShadow.wgsl` | B（裁剪：去光探针/LightmapBaker/间接光） | `Renderer/Passes/Light/` | M8 | 8 天 |
| 构建体系（CPM 锁 commit + vcpkg 混合、平台宏） | 根 `CMakeLists.txt`、`External/CMakeLists.txt` | C | `Lemon/CMakeLists.txt` | M0 | 2 天 |
| **不移植** | Skia Graphite 路径（UI 太重）、llama.cpp/astc-encoder、Luma_CAPI 平铺 C API、MonoHost、TaskSystem（Box2D 适配）、WorldStreaming/SubScene（v1 用不上）、AIPanel | — | — | — | — |

### 1.2 yami-rpg-editor（MIT；代码 C 级——语言不同无拷贝价值；素材直接可用）

| 项 | 源 | 级 | 去处 | 里程碑 |
|---|---|---|---|---|
| GUID+manifest 资产 schema | `Data/manifest.json` + `file.js parseGUID` | C | 06 §2 资产库 | M4 |
| 场景 JSON + RLE codec 算法 | `.scene` 文件 + `Script/codec.ts`（339 行） | C | `Serialization/SceneCodec`（~100 行 C++） | M2 |
| Team 势力关系 schema | `Data/teams.json` | C | `Data/teams.json`（同构 schema） | M2 |
| 组件生命周期约定（add/call/update 挂载） | `Script/event.ts:775 ScriptManager` | C | 04 §2.1 脚本组件接口形状 | M3 |
| 双通道存档接口形状 | `Script/data.ts:745-1091` | C | 06 §10 Save API | M5 |
| F3 面板统计项清单 | `Script/main.ts:255-290` | C | 05 §9 性能面板 | M2 |
| 事件指令树数据格式 + 预编译思想 | `.event` 格式、`command.ts:118 CommandCompiler` | C（冻结 schema） | 06 §11 可视化事件预留 | M8 后 |
| Deployment 三平台产物清单 | `title.js:973` | C | 06 §6 packager 产物定义 | M7 |
| 模板默认素材 | `Templates/arpg-ts-chinese/Assets/`（角色/怪物 Lv1-9/场景/技能/粒子/UI/音频） | **素材直接采用**（MIT 声明 + `THIRD_PARTY.md` 登记），缺口自补 | 06 §7 | M5–M6 |
| 分区网格粒度自适应 | `scene.ts:3407 optimize()` | C | 03 §5 空间哈希 | M2 |
| **明确不移植** | `command.js` 22.5k 行指令编辑器、ui.ts 即时 UI、Electron 壳、Monaco/tsc 集成、双实现渲染 | — | — | — |

### 1.3 MoteurJV（MIT，编辑器交互 + 组件面）

| 项 | 源 | 级 | 去处 | 里程碑 |
|---|---|---|---|---|
| **行为组件目录**（Spawner/Chase/Shooter/Projectile/Patrol/Hazard/Collectible/Health…） | `examples/05_editor/main.cpp:45-91` | C（组件切分直接采纳） | 03 §3.3 组件目录 | M2 |
| Play/Stop 整场景快照沙盒 | 同上 496/510/1883 行 | C（方案，量小重写更快） | 05 §4 | M4 |
| Undo 快照栈（60 上限）+ entityToJson 复用 | 同上 178/1981-1995 | C（历史方案；ADR-009 后主方案为 Prowl2D 属性级双轨，快照器保留 Play 沙盒与超大操作退化路径） | 05 §4 | M4 |
| 后端零泄漏门面纪律 | `engine/include/mjv/*` vs `engine/src/*` | C | 01 §1 铁律 2 | M0 |
| 手写序列化反面教训 | 同上 1893-1985 | （反例）→ 注册表驱动的论证材料 | 05 §5 | — |
| **不移植** | 自研 Registry（O(n·m)）、raylib 层、迷你物理、3D 模式 | — | — | — |

### 1.4 Editor-RPG2D（开放署名，可拷代码）

| 项 | 源 | 级 | 去处 | 里程碑 |
|---|---|---|---|---|
| 编辑器模式栈 | `src/EditorsManager.cpp`（~40 行） | A | `Editor/Interaction/ModeStack` | M4 |
| GUI 双指针焦点仲裁 | `src/GUIManager.cpp:32-33` | A（并入 ImGui 语义） | `Editor/Interaction/Focus` | M4 |
| "GUI 未占用才轮到场景"约定 | `CursorOnMap.cpp:402` 附近 | A | 同上 | M4 |
| 放置交互状态机 | `src/Editors/MapEditor/Map/CursorOnMap.cpp`（719 行） | B（SFML→引擎视口） | `Editor/Interaction/PlacementTool` | M6c（塔防摆塔复用） |
| 自动瓦片 Group+friends | `include/Tileset.hpp`（坐标表外置数据） | B | `Assets/Tileset` 数据结构 | M6c |
| chunk 烘焙 + 相机剔除 | `Chunk.cpp`、`Map.cpp:198-219` | B（VertexArray→Vulkan 顶点段） | 02 §3.3 路径 B | M6c |
| Shader 降级守卫模式 | `src/ShadersManager.cpp` | C（推广到所有资源类型） | 02 §2 纪律 | M1 |
| macOS bundle 定位 + CMake 资源拷贝 | `src/main.cpp:62-77`、`CMakeLists.txt` | A | 编辑器打包脚本 | M4 |
| 二进制序列化骨架（加 magic/version + UTF-8 改造） | `src/BinaryWriter.cpp` | C（仅骨架思想，烘焙格式参考） | 06 §4 .baked | M7 |
| **不移植** | BFS 洪泛寻路（无视障碍）、逐对象 draw、启动全量加载、extern 单例网、wchar_t dump | — | — | — |

### 1.5 duality（MIT）

| 项 | 源 | 级 | 去处 | 里程碑 |
|---|---|---|---|---|
| Tilemap dirty-rect 更新协议 + 32×32 扇区碰撞 | `Source/Plugins/Tilemaps/` | B | Tilemap 运行时（03/05） | M6c |
| Tilemap gzip 版本化序列化 | 同上 | B | 场景 codec 的 tilemap 段 | M6c |
| SpriteAnimator（LoopMode/自定义帧序列/帧混合思想） | `Source/Plugins/.../SpriteAnimator` | C | `Animator2D`（03 §3.2） | M5 |
| GameObject/Component 组合模式 | `Source/Core/Duality/` | C（心智模型，我们走 ECS） | — | — |

### 1.6 Looper（许可待核实 → 全部 D 级）

| 项 | 源 | 用法 | 里程碑 |
|---|---|---|---|
| per-instance SSBO 海量精灵 + 脏区间更新 | `engine/renderer/renderer.cpp:854-915`、`UpdatePerInstanceBuffer` | D：M0/M1 实现期对照（Vulkan 用法/同步设计） | M0-M1 |
| VMA 集成方式 | 同上 | D | M0 |
| 编辑器 gizmo / 裁剪相机 | `editor/gizmo.*`、`collision_camera` | D | M4 |
| thread_pool/work_queue | `engine/core` | D（我们有 Luma JobSystem 可 A 级移植，二选一） | M0 |

### 1.7 Prowl2D / Prowl（自有；理念/API 蓝本，ADR-009）

**定位（ADR-009 升格）**：Prowl2D 是 Lemon 的**理念、工作流与 C# API 风格蓝本**——目标一致（Unity 开发者无缝过渡、KISS、小而可定制），操作理念以"高度类似 Unity 的 2D 引擎"为准；归档状态不变（不新增功能、保持可编译）。代码级供体仍为 Luma 等。

| 项 | 来源 | 用法 |
|---|---|---|
| C# API 命名面（GameObject 正名 / LemonBehaviour / Unity 生命周期 / AddComponent 双路由） | `Prowl2D/Prowl.Runtime/GameObject/{GameObject,MonoBehaviour}.cs` | 04 §2.1/§3 门面设计蓝本（命名级对齐） |
| SceneDispatcher 位掩码调度（未 override 零成本；dense 数组；每帧至多一次重建；[ExecutionOrder]） | `Prowl2D/Prowl.Runtime/GameObject/SceneDispatcher.cs` | 04 §2.1 档①调度设计（C++ ScriptBox 池重写） |
| PrefabLink + PropertyOverride（链接+覆盖列表；嵌套 prefab；Apply/Revert/Break） | `Prowl2D/Prowl.Runtime/GameObject/PrefabLink.cs`、`Prowl2D/Prowl.Editor/Prefabs/PrefabUtility{.Api,.Overrides}.cs` | 03 §2 Prefab 模型（映射到 ECS） |
| Undo 属性级双轨（属性 diff + action 命令；Guid 找回；连续合并） | `Prowl2D/Prowl.Editor/Core/Undo.cs` | 05 §4 主方案（C++ 重写） |
| Camera2D 清单（像素完美 snap / 指数阻尼跟随 / 边界钳制 / 编辑器相机自愈） | `Prowl2D/Prowl.Runtime/Components/Camera2D.cs` | 02 §3.5 相机分册（M1） |
| async/await 主线程上下文（协程替代） | `Prowl2D/Prowl.Runtime/Tasks/MainThreadContext*` | 04 §3.1（C++ 定时器队列底座） |
| UGUI 式运行时 UI（RectTransform/GameCanvas/Layout，6.5k 行 C#） | `Prowl2D/Prowl.Runtime/Components/UI/` | ADR-008 第四候选（M5 决策，对照不拷） |
| 品类优先级结论（图集+合批+排序层先行） | `docs/Archive/Prowl-2D-Roadmap.md` §4 | 排期依据（M1 顺序） |
| 压测场景设计（500 怪压测模板） | 同上 | Samples/bench 场景蓝本 |
| "单测全绿 ≠ 编辑器可用"教训 | `.zcode/plans` S8 计划 | 每里程碑 GUI 冒烟验收纪律（08） |
| Roslyn 脚本编译集成 | `Prowl2D/Prowl.Editor/Projects/Scripting/RoslynScriptBackend.cs` | M3 后期内嵌编译参考（自有代码可拷） |
| macOS GL 驱动坑记录 | `Prowl2D/AGENTS.md` §4 | 归档（Vulkan 路线已绕开） |

## 2. 按里程碑的移植倒排（动工清单视图）

| 里程碑 | 移植项（级） |
|---|---|
| **M0 spike** | Luma JobSystem（B）、CoreCLRHost 最小闭环（B）、Luma 构建体系（C）、Looper 实例化渲染（D）、MoteurJV 零泄漏纪律（C） |
| **M1 渲染** | Luma RenderableManager（B）、批键（A）、粒子全套（B）、Nut RHI 切面（C）、Editor-RPG2D 降级守卫（C）、Prowl2D Camera2D 清单（C） |
| **M2 ECS** | MoteurJV 组件目录（C ✅ 27 组件+注册表）、yami Team schema（C ✅ 内联默认表）/RLE codec（M6c）/分区自适应（M6c）、Luma JobSystem（B ✅ 值语义修正）+ 系统调度（C ✅）、F3 清单（C ✅ 统计层+--stats 文本）；新增第三方 nlohmann/json v3.11.3（.scene 序列化，THIRD_PARTY 已登记） |
| **M3 脚本** | Luma CoreCLRHost 全量 + ScriptLoadContext（B）、yami 生命周期形状（C）、Prowl2D SceneDispatcher 调度 / 命名级 API 面 / MainThreadContext（C）、Prowl Roslyn（C，后期） |
| **M4 编辑器** | Luma 面板框架与面板集（B）、MoteurJV Play 快照（C）、Prowl2D Undo 双轨 / PrefabLink + Inspector override（C）、Editor-RPG2D 模式栈/焦点仲裁/放行约定（A）、yami GUID+manifest（C）；新增第三方 Dear ImGui v1.92.9b-docking（MIT，编辑器 UI，THIRD_PARTY 已登记，M4.0）+ stb（公有领域，PNG 导入/截屏，M4.0） |
| **M5 VS 模板** | duality SpriteAnimator 思想（C）、yami 存档接口（C）、yami 默认素材底包（MIT 直用）；新增第三方 **RmlUi 6.3**（MIT，v1.x 富 UI 首选，spike-04 三判据验收通过，ADR-008；CPM 锁 tag，THIRD_PARTY 已登记；**2026-09-28 批③a 转正式**——ADR-014，`Engine/Ui` + 自研 RenderInterface over RHI）+ **rbfx**（MIT fork，RmlUi↔引擎渲染层适配 D 级对照，ADR-008 接入形态依据）；FreeType VER-2-14-3 CPM 单源（RmlUi 字体引擎；2026-10-05 批⑦ mac 自 brew 切 CPM 同版 + win 缺口补齐，THIRD_PARTY 已更新） |
| **M6c 音频** | 新增第三方 **miniaudio 0.11.25**（公有领域/MIT-0，vendored 三件 `Engine/Audio/thirdparty/`，网络阻断 CPM 不可行用户手备包，THIRD_PARTY 已登记，ADR-015；miniaudio 类型不出 `AudioEngine.cpp`）；无参考引擎移植项（音频为规划外缺口补齐，自研封装） |
| **M9 TD 模板**（编号沿革 M6c→M6d→M9，2026-09-30 定） | duality Tilemaps（B）、Editor-RPG2D 放置状态机/自动瓦片/chunk 烘焙（B） |
| **M7 发布** | yami Deployment 清单（C）、Editor-RPG2D 序列化骨架（C） |
| **M8 光照** | Luma 延迟光照裁剪版（B，含 WGSL→GLSL 直译） |

## 3. 移植工作量汇总

A/B 级（真拷代码）合计约 **45–55 人天**；C 级 schema/思想项不计移植工时（已含在各里程碑设计工作量中）。对照自研等价物估算（RenderableManager/粒子/CoreCLRHost/JobSystem 四大件自研 ≈ 60+ 人天且风险高），移植策略为项目节省约 1.5–2 个月日历时间与大量试错成本。

## 3.5 OS 平台差异验证点（macOS 先行；Windows 移植时逐项过）

引擎经 SDL3 语义层隔离 OS 差异，但"接口隔离"≠"行为一致"——下列功能 macOS 已实测，
Windows 首次移植时需人工复验（M4.6 §7 登记；新 OS 级功能在此追加）。
**批⑦（2026-10-05）**：win 侧机器面（编译/出包）已就绪，本表四项 + codepage 归
[批⑦ 真机清单](../DevLog/2026-10-05-m7a-b7-windows-closure.md)（§3.6 批⑦ 增补表其后）：

| 功能 | 语义层接口 | macOS 状态 | Windows 验证点 |
|---|---|---|---|
| 窗口标题动态改写 | `Window::SetTitle` | ✅ M4.6 标题栏 | 中文/●脏标记编码正常 |
| 外部文件拖入导入 | `Window::TakeDroppedFiles`（SDL_EVENT_DROP_FILE） | ✅ M4.6b（Finder 拖 PNG） | 资源管理器拖入路径形态（盘符/反斜杠）经 `std::filesystem` 归一 |
| 关闭按钮/退出确认 | `Window::PollEvents` 返回 false → 状态机 | ✅ M4.6（--smoke-close） | 无差异预期；跑同款冒烟即可 |
| 文件选择器手输路径 | FilePicker（编辑器内实现，无 OS 对话框） | ✅ M4.6b | `C:\` 盘符路径回车直达 |

## 3.6 Windows 编译阻断项（首次移植前清零；2026-09-24 全栈审查 F-11 登记；**2026-09-30 第 0 批全数处置**）

§3.5 行为验证表默认"能编译"——下列阻断项不清零则到不了行为层。**"补一个 win
preset 就能编"不成立**；M7 开工前（Gate C 前置，08 §M7）逐项处置：

| # | 阻断点 | 位置 | 处置方向 | 处置状态（2026-09-30） |
|---|---|---|---|---|
| 1 | `#include <unistd.h>`（getpid） | `Editor/App/EditorAppSmoke.cpp`、`EditorAppSmokeTpl.cpp`、`Editor/Templates/VsTemplateGen.cpp`（EditorApp.cpp 已在减脂批消除） | 平台抽象（`std::filesystem` 无此需求；SDL/条件宏） | ✅ 新增 `Engine/Core/Process.h`（`lemon::CurrentProcessId()`：POSIX getpid / Win GetCurrentProcessId；SDL 3.2.14 无 SDL_GetPID 故未用 SDL） |
| 2 | `__attribute__((format(printf,…)))` | `Engine/Core/Log.h` | `PRINTF_FORMAT` 宏按编译器分支（MSVC 用 `[[msvc::format]]`/SAL） | ✅ `#if defined(_MSC_VER)` 分支（MSVC 侧暂不加注解） |
| 3 | `__builtin_strcmp` | `Engine/ECS/ComponentRegistry.h`（1 处）、`Engine/ECS/SystemPipeline.cpp`（3 处；合计 4 处非登记的 5 处） | 直接 `std::strcmp`（编译器自会内联，GCC 专有内建零收益） | ✅ 全换 `std::strcmp`（ComponentRegistry.h 补 `<cstring>`） |
| 4 | `popen/pclose` 拼 shell 编译命令 | `Editor/Assets/ProjectWizard.cpp` | 进程抽象层（`CreateProcessW`/`_popen` + 宽字符 argv，顺带消 shell 注入面） | ✅ 编译面：MSVC `_popen/_pclose` 宏分支过渡。**行为面未清**：中文路径 codepage 归 Windows 真机首调（届时按原方向换 CreateProcessW 宽字符） |
| 5 | `std::filesystem::rename` 覆盖既有目标 | 存档/管线缓存写路径（原子写已收敛在 `WriteFileAtomic`——一处收口） | 语义层封装（Windows 走先删后改名或 `ReplaceFile`） | ✅ 新增 `Engine/Core/FileOps.{h,cpp}` `RenameReplace()`：Win = `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`（同卷原子、UTF-8→UTF-16），POSIX = `fs::rename`；`WriteFileAtomic`（AssetDatabase）与管线缓存写（RHI）已切换 |

**批⑦ 增补（2026-10-05）**——批④⑤ 新代码面（GameEntry/packager/新测试）带入的
阻断 + 依赖缺口，静态扫描实证后全数处置：

| # | 阻断点 | 位置 | 处置 | 处置状态（2026-10-05） |
|---|---|---|---|---|
| 6 | win 分支用 `DWORD`/`GetModuleFileNameA` 但全 TU 无 `<windows.h>`；ICD 自举块 `setenv` POSIX-only | `Engine/Entry/GameEntry.cpp` | 补 `_WIN32` include；ICD 块收窄 `__APPLE__`（win 侧 ICD 走驱动注册表，无此概念） | ✅ 批⑦ A1 |
| 7 | CoreCLRHost `char_t` 分叉两处（`hostfxr_set_error_writer` 回调 / `initCmdLine` argv——win = `wchar_t` 编译不过）；dotnet 根候选链无 win 默认位；`LoadLibraryA` ACP 窄码中文路径哑火 | `Engine/Scripting/CoreCLRHost.cpp` | 回调/argv 按平台分支（win 侧 UTF-8→UTF-16 经 `fs::path`）；根链尾补 `%ProgramFiles%\dotnet`；`LoadLibraryW` + widen | ✅ 批⑦ A2（宽字符仅保 load-bearing 两处，见伴生项） |
| 8 | `<unistd.h>` / `::getpid` / `::setenv` / `::popen` 残留（批④⑤ 新代码面） | `tests/engine_tests.cpp`（unistd+24 处 getpid）、`Samples/bench-script/main.cpp`、`Tools/packager/main.cpp` | `Core/Process.h`；`_putenv_s` 分支；`_popen/_pclose` 宏分支（阻断项④ 同款）+ win 双引号 `Quote`（cmd.exe 不认单引号） | ✅ 批⑦ A3/A4/A8 |
| 9 | spike dlfcn 无守卫（`spike/03-csharp`；04 的 vendored vulkan.h 反而有守卫） | `spike/` | win preset `LEMON_BUILD_SPIKES=OFF`——M0 mac 验收产物不为 win 面维护（决策 D-7d） | ✅ 批⑦ A5 |
| 10 | RmlUi 软依赖 FreeType 在 win 无系统源（mac 走 brew）——CI/真机首编的依赖缺口 | `cmake/Dependencies.cmake` | FreeType CPM `VER-2-14-3` 三平台单源（SDL3 先例）+ `Freetype::Freetype` ALIAS 接线（RmlUi 软依赖 target 检查直取，`FT_DISABLE_*` 五连关）；mac 自 brew 2.14.3 切同版 | ✅ 批⑦ A7（smoke-uirml `font=Noto` 断言把守） |
| 11 | MSVC 默认 `/MD` → 干净机需 VC redist（`vcruntime140.dll` 进闭包） | 根 `CMakeLists.txt` | `CMP0091 NEW` + `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded[Debug]`（静态 CRT；决策 D-7c——win 闭包预期只剩 vulkan-1.dll） | ✅ 批⑦ A6 |
| 12 | **review 轮实抓**：hostfxr API 字符串参数全 `char_t`（首版只分支了回调/initCmdLine，漏 `initForConfig` 与 `GetExport` 三参）；windows.h min/max 宏咬 `std::min/max`（`Process.h` 公共头泄漏面） | `CoreCLRHost.cpp` / 根 CMakeLists | 四调用全 `Widen`（CP_UTF8——`fs::path(std::string)` win 按 ACP 非按 UTF-8，自查纠）；MSVC 全局 `NOMINMAX WIN32_LEAN_AND_MEAN` | ✅ 批⑦ review（mock 门实抓，[DevLog](../DevLog/2026-10-05-b7-review-hardening.md)） |

伴生项（宽字符路径）**范围收窄（2026-10-05）**：全链 UTF-8→UTF-16 仍是 M8 级工程；
批⑦ 只保真机判据直接依赖的两处（CoreCLRHost `LoadLibraryW`/`initCmdLine` argv——
包在非 ASCII 安装路径能起 CoreCLR）。其余（窄 argv 入口、manifest/索引编码、编辑器
CJK 资产名）维持"归真机首调"口径，见 §3.5 表。`win` CMake preset ✅ 既有 + 批⑦
`LEMON_BUILD_SPIKES=OFF`。

> **验证口径（批⑦ 更新）**：①–⑫ 全部 macOS 侧验证"不回归"（构建 + 回归 full
> 19/19 + ctest 4/4）；win 分支另过 **mock 门**（mac clang 真编 `_WIN32` 段，实抓
> ⑫ 两处，[review DevLog](../DevLog/2026-10-05-b7-review-hardening.md)）；**MSVC
> 实际编译的机器门禁 = CI `win-build-test` job**（2026-10-05 落地，LunarG SDK
> 静默装 + preset win + ctest -C Release；首跑待 workflow_dispatch）——07 §3.5
> 行为验证表与 Gate C ② 的最终勾销仍以**真机**为准
> （[DevLog 批⑦](../DevLog/2026-10-05-m7a-b7-windows-closure.md) 真机清单）。

## 4. 登记模板（新增移植项用）

```markdown
| 模块 | 源仓库@commit | 源路径 | 级 | 许可/合规操作 | 目标路径 | 里程碑 | 估工 |
```
