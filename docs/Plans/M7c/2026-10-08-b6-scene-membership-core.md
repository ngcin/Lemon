# 批⑥：LoadScene 引擎核心——SceneMembership + 场景档案 + 换场协议（Single 同步路径）

Status: in-progress（**切片 ⑥a ✅ 机器面 2026-10-08**——membership 数据面 + 场景档案 + BuildInto 拆分 + 单测五件；单测 34,481（+46）/ ctest 4/4 / 回归 full 20/20（zh 态；en 态 19/20 红项 = 登记的批③ i18n 候选池缺陷逐位复现，非本批引入）/ bench fps=90 / 构建零警告，[DevLog](../../DevLog/2026-10-08-m7c-b6a-scene-membership.md)；**⑥b 换场协议编排 + ⑥c StateHash/回放扩展随后小批推进**——用户拍板"不要一次性大批量完成"，每切片独立验收）

- 日期：2026-10-08
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md)（D1 单 registry+membership / D3 统一管线 / D6 Audio.Paused 强制清）· [M7c.md](./M7c.md) 批⑥ 行 · [03-ECS-Runtime](../../EngineDesign/03-ECS-Runtime.md) §2（Scene 语义修订注记随本批落）
- 性质：ADR-017 批⑥ 的引擎侧——换场的实体层与数据面。C# 门面/vtable/事件族归批⑦，async 分帧归批⑧。

## 开工首查（ADR-017 预埋两项，本批落账）

### 查① 全 `Scene&` 调用面盘点（2026-10-08 rg 核证）

| 调用面 | 命中密度 | membership 世界下的处置 |
|---|---|---|
| `Engine/Systems/Systems.{h,cpp}`（系统实现） | 25/23 处 | **零改动**——`Tick(World&, Scene&)` 的"遍历实体"语义在单 registry 下即"遍历全组"（DDOL 实体照常被系统看见 = Unity 同款） |
| `Engine/Renderer/SceneExtractor` | Extract(Scene&) | **零改动**——同上，DDOL 实体照常提取渲染 |
| `Engine/Scripting/ScriptHost` | 7/7 处 | **零改动**——C# tick 遍历 ScriptBox 池，全 registry 语义 |
| `Engine/ECS/{Hierarchy,Scene,World}` | 结构层 | 本批动：World 增档案面；Scene/Hierarchy 原样 |
| `Engine/Serialization/SceneArchive` | 7/4 处 | 本批动：**BuildInto 拆分**（装载追加段）；Save/Load 语义不变 |
| `Engine/ECS/StateHash` | 哈希 | **零改动**（见查②） |
| `Editor/`（EditorContext/HierarchyPanel/ViewportPanels/Smoke） | ~35 处 | **零改动**——编辑器 edit/play 双 registry 模式保留（ADR-017 D1：EnterPlay/StopPlay 归位复用，编辑态单组 = 不打标） |
| `tests/`（Ecs/Gameplay/Editor 等） | ~120 处 | **零改动**——夹具走 CreateScene/Load 现状路径 |
| `Samples/` `Engine/Entry/GameEntry` | 少量 | ⑥b 起换场编排接入时动 |

**结论**：方案 B 的波及面与 ADR-017 预估一致——集中在 SceneArchive / World / （⑥b 的换场编排）三处，无隐藏消费者。

### 查② membership 哈希流口径定案

**membership 不入哈希流，ComputeStateHash 零改动，金回放零重录**：

- `SceneMembership` 按ScriptBox 先例为普通 entt 组件、**不入 ComponentRegistry** → ComputeStateHash（按注册表 id 序遍历池）天然不哈希它，哈希流逐位不变；
- 实体在册即入哈希（组件字段照哈希）——**漏清场（旧组残留）与幸存者漂移（DDOL 误死）都被现有哈希抓**，分组信息对判定器冗余；
- 场景身份（装的是哪张图）由回放流的**换场 op 记录**承载（批⑦ vtable op 入回放，错图即 op 分歧）；
- 批⑧ async 契约延伸：Build 段经 BuildInto 直接进 registry，分帧期间哈希可见部分构建态——**回放走同步路径**（ADR-017 D3 已定），金档不经过 async 中间态，无冲突。

## 切片（用户拍板小批推进）

- **⑥a（本切片）数据面**：SceneMembership 组件 + 打标/组清场/DDOL 根树重标签 + World 场景档案 + SceneArchive::BuildInto 拆分（Load 行为逐位不变）+ 单测。
- **⑥b 换场协议编排**：引擎侧 SwitchScene 编排（组清场 → DDOL 保留 → 随行清扫链（Fx 非实体附着清/UI origin=Scene 卸/Audio.Paused 清）→ BuildInto + 打标）+ smoke-scene 单跳（编辑器外无头形态）。
- **⑥c StateHash/回放扩展用例**：多次换场 + DDOL 幸存者回放轨迹用例 + 03 分册 §2 注记收口。

## ⑥b 开工前必读（2026-10-08 review 发现，三件集成面预警）

1. **F1 销毁通知必须走管线**：脚本 OnDestroy 补发点在管线 #17 `DestroyCommitSystem::Tick` 内（`Systems.cpp:1478` 的 `NotifyPendingDestroys` + `CommitDestroys`）——换场编排**直接调 `Scene::CommitDestroys` = 静默漏 OnDestroy**。编排形态二选一：①组清场入队后由管线 #17 在下帧 Essential 通知+提交，换场逻辑挂在 #17 之后的专用段；②编排自行 `backend->NotifyPendingDestroys` 后再 commit。倾向 ①（复用恰好一次去重，时序 = 帧边界天然达成）。
2. **F2 初始人口未打标**：EnterPlay/GameEntry 现状建 play 场景不盖章（组 0 全组）——首次 LoadScene 时组 0 的初始实体不会被任何 `QueueDestroySceneGroup(handle)` 收走。⑥b 定案：EnterPlay/GameEntry 建档+打标初始场景（推荐，"零未打标"不变量从进 Play 第一帧成立）vs 编排特判组 0。
3. **F3 BuildInto 后置宿主 pass**：BuildInto 只建实体与组件——ScriptBox 槽 typeId=-1 待解析、SpriteRef 待归一、prefab spawn 通道待确认。编排须镜像 GameEntry 装载后序列（ResolveSlotBehaviour / ResolveSpriteRefs / GUID 归一），否则首帧渲染与脚本全哑。

## 任务清单（⑥a，文件/行级）

| # | 文件 | 动作 |
|---|---|---|
| T1 ✅ | `Engine/ECS/SceneMembership.h`（新） | 组件（scene 句柄 + flags/DDOL 位）+ 四原语声明（Stamp/QueueDestroyGroup/MarkDontDestroyOnLoadTree/Count×2）；头注落 ScriptBox 同款纪律（不入注册表/序列化/StateHash） |
| T2 ✅ | `Engine/ECS/SceneMembership.cpp`（新） | 四原语实现；子树遍历沿 SceneArchive::CollectSubtree 先例（Hierarchy 链 + 深度护栏） |
| T3 ✅ | `Engine/ECS/World.h` / `World.cpp` | SceneRecord{handle,name,path,isLoaded} + CreateSceneRecord/SceneRecordAt/FindSceneRecord/ActiveSceneHandle/SetActiveSceneHandle；scenes_/active_ registry 面原样保留 |
| T4 ✅ | `Engine/Serialization/SceneArchive.{h,cpp}` | Load 拆 ParseSceneDoc/ApplySceneName/BuildEntities 三内部段；新增 **BuildInto**（不清空追加装载，false = 解析失败场景不动）——Load 组合三段行为逐位不变（坏条目丢弃/回收/name 恢复/迁移链全保留） |
| T5 ✅ | `Engine/CMakeLists.txt` | 源清单加 SceneMembership.cpp |
| T6 ✅ | `tests/engine/SceneTests.cpp`（新） | 五测：打标计数 / 组清场保 DDOL（含根树）/ BuildInto 共存 + Load 清空对照 / StateHash 对 membership 不敏感 / World 档案发号寻址 |
| T7 ✅ | `tests/engine/TestFramework.h` + `TestMain.cpp` + `tests/CMakeLists.txt` | RunSceneTests 声明/调用/源清单三处注册 |

## ⑥a 收口（2026-10-08）

- **实测**：单测 **34,481 checks**（基线 34,435 + 新增 46）/ ctest 4/4 / 回归 full **20/20**（zh 态整跑；en 态首跑 19/20 红项 = smoke-anim `row=NO`，与登记的批③ i18n 候选池已知缺陷逐位一致——`~/.lemon/editor-settings.json` 遗留 `en` 所致，切回默认 zh-CN 后全绿；非本批引入）/ bench-survivor 门禁 fps=90 ≥ 76.5 / 构建零警告。
- **实现期发现与修正（三件）**：
  1. `StampSceneMembership` 原稿全量覆写 scene 值——增量装载（BuildInto 二装）会抹掉 DDOL 幸存者的来源组。改为**仅收编未打标实体**（无组件或 scene==0）：新装段全收编、幸存者 scene/flags 双不动（测试"incremental stamp only recruits unassigned"钉住）。
  2. `CountSceneGroup` 原稿只数带组件实体——无组件实体与"组件在但 scene==0"必须同归未指派组（头注释承诺，实现首版漏了；首跑 FAIL 抓出后修正）。
  3. 附带修：`GameEntry.cpp:515` 批① 遗留 `-Wunused-but-set-variable`（`fe` 判存未用 → `!= nullptr` 直判），构建回到零警告。
- **语言设置交底**：回归机器态的 `~/.lemon/editor-settings.json` 本日由 en 切回 zh-CN（批③ 默认值）——与登记项"smoke 未固定语言 → 回归依赖机器设置问题"同源，修法仍留候选池。

## 出口判据（批⑥ 整体）

单测新增五件全绿 + checks 基线 34,435 只增不减 + 回归 full 20/20 + ctest 4/4 + bench-survivor 门禁不降 + 金回放零重录（⑥c 用例证明）+ smoke-scene 单跳（⑥b）。本切片（⑥a）验收 = 构建 zero warning + lemon-tests 全绿 + 新增 checks 计数入本文件收口段。
