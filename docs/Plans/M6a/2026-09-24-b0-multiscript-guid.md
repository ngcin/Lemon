# M6a 批⓪：架构地基 —— scripts[] 多脚本 + sprite 引用 GUID 化

Status: planned

> 拆分自 [M6a 总览](./M6a.md)（08 §2 M6a WBS 第 1 条）。先行理由：多脚本与 GUID 晚做返工面
> 最大——模板拆脚本、用户项目实体组织、prefab/场景档格式都压在它们上面。动工前必读：
> [04 §2.1/§3](../../EngineDesign/04-CSharp-Scripting.md)（多脚本注记与双路由）、
> [06 §2-§3](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（GUID/manifest/schema）、
> [03 §2/§13](../../EngineDesign/03-ECS-Runtime.md)（销毁两阶段/迁移链）、
> [09 §6.8](../../EngineDesign/09-Testing.md)（回放零重录推论）。

## 1. 现状盘点（2026-09-24 逐行核对）

### 1.1 多脚本：卡点全在 C++ 存储与序列化面，C# 调度早已多实例

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| A | ScriptBox = 单槽 POD `{typeId, flags, scriptGuid, className[24]}` | `Engine/Scripting/ScriptBox.h:16-21` | entt 同类型组件单实例 → 每实体最多一脚本 |
| B | `.scene` 实体附加单数 `"script": {guid, class}` | `SceneArchive.cpp:175-179`（写）/`:228-242`（读，`Emplace` 单实例） | schema 面=单数成员 |
| C | schemaVersion 恒 v1，迁移链空转 | `SceneArchive.h:20`、`SceneArchive.cpp:376-384` | 首次格式变更在即，迁移链首例 |
| D | 编辑器装配链全按单槽 | `EditorContext.cpp:301-311`（AttachScript get-or-create 覆写）/`:320-332`（ResolvePlayScripts）/`:335-347`（RefreshScriptsAfterReload）；`ScriptHost.cpp:399-413`（AttachBehaviour）/`:521-528`（op4） | 换多槽后覆写语义必须拆为「解析既有槽」与「追加新槽」两条 |
| E | destroyNotified 位挂在 ScriptBox.flags | `ScriptBox.h:14`、`ScriptHost.cpp:498/539-551`、`Tests/engine_tests.cpp:1585-1616` | 实体级恰好一次语义，多槽后不得逐槽各发一次 |
| F | Inspector Script 段单 combo + 单移除 | `InspectorPanel.cpp:675-730` | 列表化 |
| G | **C# 侧无卡点**：Behaviours 按 typeId 分桶 `Instances` 列表，Attach 追加、Detach 按实体清全部 | `Lemon.SDK/Behaviours.cs:132-170` | 调度零改动（04 §3 注记预判成立） |
| H | **C# 挂脚本通路已存在**：`SceneOps.AttachScript`（op4，compId=typeId）+ C++ case 4 | `Lemon.SDK/SceneOps.cs:16-17,69-74`、`ScriptHost.cpp:521-528` | SDK 双路由只缺 GameObject 门面方法 |
| I | GameObject 门面无 AddComponent/GetComponent（数据组件走 SceneOps 静态版） | `Lemon.SDK/GameObject.cs:11-40` | 双路由（IComponent 分路）整面缺失 |

顺带收口：smoke-template 死亡链断言读 `sb.flags & 1u`（disabled 位）与 `View<ScriptBox>`
（`EditorApp.cpp:3538-3560`）——flags 移到槽级后此断言要逐槽迭代，同批改。
`Systems.cpp:606,662` 死亡豁免只做 presence 检查，布局变更不影响。

### 1.2 sprite 引用：场景/prefab 里最后一个数字资产引用

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| J | `SpriteRenderer.spriteId` = AtlasRegistry 注册号，布局冻结 12B | `RenderComponents.h:15-21,44` | 数字号依赖扫描序 + manifest 记账（06 §2.2「只增不减」） |
| K | 残余风险：manifest 丢失/损坏后确定性重排，仅当资产集未变才与旧档吻合 | 06 §2.2 批④后修② 注记 | 导入过新素材的项目删 manifest 重开 → 引用错位类别仍存在 |
| L | GUID→spriteId 查表已存在（编辑器 + C# 双口） | `EditorContext.cpp:314-318`（SpriteIdOfGuidHex）、`ScriptHost.h:43`（vtable spriteOfGuid） | 解析只需复用，无需新机制 |
| M | 作者面只写数字号：Inspector 槽（combo/拖入/清空）只写 spriteId | `InspectorPanel.cpp:167-229` | 拖拽 payload 已带 guid（`AssetBrowserPanel.cpp:302`）——只差落笔 |
| N | 生成器/建实体口数值写号 | `EditorApp.cpp:699-710`（GenerateVsTemplate：CreateSpriteEntity(tag, sprite) + AttachScript(player, 0, "PlayerBehaviour")）；`EditorContext.h:76-78` | 模板档当前以数字号入库 |
| O | bench-sim / bench-script 场景零 SpriteRenderer | 两 main.cpp grep=0 | **加字段零重录的机械前提**（§4） |
| P | C# 镜像 12B + 探针行 | `Lemon.SDK/Interop/Components.cs:59-69`、`Interop/LayoutTables.cs:103` | 尾加后 24B 双侧同步（机械） |

## 2. 设计决策（本批定案，违者走 ADR）

1. **ScriptBox 内嵌定长槽数组，不走旁路池**。`kMaxScriptsPerEntity = 8`；槽结构
   `{typeId, flags, scriptGuid, className[24]}` 与旧单槽同构（40B/槽，ScriptBox 全量
   328B，仅脚本实体持有）。理由：ScriptBox 不入 ComponentRegistry、不入 StateHash、
   C# 不见其字节——布局零冻结约束；entt 单组件语义保留；序列化本就手写。8 槽 =
   模板三拆（3）+ 用户项目余量；溢出 = Inspector 禁用追加 + 告警。
2. **destroyNotified 升实体级**（ScriptBox 头部 uint32 字段），disabled 保持槽级
   flags bit0。恰好一次通知语义不变（F-08.2）。
3. **`.scene` schemaVersion 1→2**：写侧只出复数 `scripts: [{guid, class}, ...]`（含
   单脚本）；读侧 `ReadEntity` 双读（复数优先，单数 `script` 兼容——LoadEntityTree
   的旧 prefab 不走迁移链，靠双读隐式升级，保存时自然改写复数）；`.scene` Load 走
   迁移链 v1→v2（纯 json 变换：单数对象包成单元素数组）。老引擎读 v2 档响亮拒载
   （既有 newer-schema 分支），不静默丢脚本。
4. **同实体同类型脚本唯一——入口禁止、格式宽容**（2026-09-24 两轮论证后修正，
   推翻最初「Unity 同义允许」默认）。入口三闸：Inspector 追加同名拒绝（置灰 +
   提示）；C# `AddComponent<LemonBehaviour>` 幂等 get-or-add（已存在返回既有
   实例，04 §3.2 不对齐清单声明此差异）；`Behaviours.Attach` 内同实体同类型
   断言（重挂路径先审——EnterPlay 走 ClearInstances、热重载走换域清表，断言只对
   真泄漏响亮 = 免费的生命周期不变量检查器）。格式不设卡：scripts[] 数组天然可存
   重复项；加载侧遇同名重复保序留首见 + 告警（与最近场景脏档清洗同款）。理由：
   无每实例参数（ScriptBox 仅持久 {guid, class}）下重复表达力为零、纯副作用翻倍；
   热重载 StateBag 键 `(class, entity)` 冲突（`Behaviours.cs:46`）是现存炸弹。
   每实例字段（05 §5）落地时带两笔欠账——StateBag 键升三元组、同类型实例排序
   面——再评估放开。
5. **SpriteRenderer 尾加 `uint64_t spriteGuid`（12→24B，尾部 4B 填充）**，普通
   FIELD（入档/入 Inspector/入字段表）。**零重录**不靠新哈希旗标，靠 09 §6.8 推论
   「加字段可零重录（空组件名已在哈希流）」的机械前提 O：三金档基准场零
   SpriteRenderer 实例 → 哈希流逐字节不变。C# 镜像/探针同步 24B。
6. **解析落位编辑器侧**（内核不识 AssetDatabase，依赖向下不破）：EditorContext 新
   助手 `ResolveSpriteRefs(Scene&)`，挂 BackfillGuids 同款站点（`OpenScene:106` /
   `OpenSceneRecovery:245` / `ExitPlay:756` / 结构 Undo 恢复 `:835`）+ prefab 实例化
   口。规则：guid≠0 → 查表覆写 spriteId；查无 → 保留档内 spriteId + 悬空告警升级为
   guid 口径；guid=0（内置/程序化页）→ spriteId 原样。EnterPlay 快照继承编辑场景
   已解析 id，无需再解析。
   **存量自动回填**（反向）：guid=0 且 spriteId 反查命中登记资产（FindBySpriteId）→
   补写 guid + 标 dirty，下次保存即转 GUID 口径——demo/svr-test 存量场景开一次存
   一次就全面享受 GUID 化，免逐个重选精灵；未登记号（内置页/悬空）保持 guid=0。
7. **作者面双写**：槽位控件/生成器/建实体口写 guid 的同时写解析 id（即时可视），
   档内双值并存；**spriteId 降级为运行时缓存 + 旧档回退**，guid 是持久身份。
8. **SDK 双路由**：`GameObject.AddComponent<T>/GetComponent<T>/RemoveComponent<T>`
   以 `IComponent` 标记接口统一约束、运行时按 `typeof(T)` 是否 LemonBehaviour 分路
   （04 §3 原案）。behaviour 分路实现全在 SDK 域内：AddComponent →
   `Behaviours.TypeIdOf<T>() + SceneOps.AttachScript`（op4 已存在，零 C ABI 改动）；
   GetComponent → Behaviours 槽内按实体+类型首见查找；RemoveComponent → 新 op5
   `DetachScript`（compId=typeId）+ Entry 新导出 `lemon_scripts_detach`（单实例
   OnDestroy + 退订，区别于按实体清全量的 lemon_scripts_destroy）。
9. **guid 语义边界 = 资产全幅**：sprite 槽今天只能引资产全幅图（06 §5 M5 批③注记，
   切片消费面 = clip 帧引用），故「guid = 资产 GUID」完备；槽直引切片归 M6c，届时
   引用扩为 (guid, cell) 二元组——本批不埋洞也不预堵。

> **拍板记录（2026-09-24，默认取此可翻）**：三个争议点按推荐默认定案——①同类型
> 重复**入口禁止、格式宽容**（决策 4；最初按 Unity 对齐默认「允许」，两轮论证后
> 修正——无每实例参数下收益为零，StateBag 键冲突/生命周期泄漏不可检测为实害）；
> ②存量**自动回填**（决策 6）；③op5 三件套**本批做齐**（roadmap 原文只点名
> AddComponent，多花 ~0.5 天换无消费者半成品期最短）。spriteGuid 哈希口径不再挂
> 旗标（09 §6.8 推论 + §4 红线足够）。

## 3. 验收判据（全部满足才勾销）

1. `ctest` 全绿（引擎 + 布局探针 + script-tests），新增用例见各 T 所列；
2. **三档金回放零重录**：m5b2 金档（sim mt/st + script）原样 `--replay`
   mismatches=0——09 §6.8 推论的机械证明，验收⑦ 口径；
3. `tools/editor-regression.sh full` 全绿，含新增 guid 链（T5）与升级后的
   template 链（T4）；
4. `--gen-vs-template` 重生成 → `--smoke-template` PASS：玩家实体 scripts[3]
   （移动/战斗/HUD），HUD/波次/三选一/死亡-复活断言不回归——验收⑥ 主体；
5. scripts[] roundtrip：多脚本场景存→开→存文本不动点；v1 单数旧档（.scene 与
   .prefab 各一）加载升级正确——验收⑥ roundtrip 半句；
6. `--smoke-guid` PASS（T5）：资产改名 + 移位导入 + 删 `.lemon/manifest.json` 重开，
   场景引用逐实体不错位——验收⑤ 全量；
7. bench-survivor 抽查 ≥55fps（M5 收口基线 58~67 的不回归线；ScriptBox 变大不进
   无脚本实体热路径，基线不应动）。

## 4. 确定性与回放影响

- **ScriptBox 不入 StateHash**（M3 落地决策不变）→ 布局重构零回放影响；
- **spriteGuid 入字段表但基准场零实例**：`ComputeStateHash` 对注册表每组件名无条件
  入哈希（空组件名已在哈希流，09 §6.8 批② 先例），SpriteRenderer 名非新增；零实例 →
  字段迭代从不发生 → 三档金档逐帧哈希不变。**唯一禁区：本批不得给基准场播
  SpriteRenderer**（播了 = 字段字节进哈希流 = 全帧漂移）；
- bench-sim/bench-script 场景构造零改动；smoke-template 不属回放面；
- 编辑器 Stop 后逐字节一致断言（`EditorContext.cpp:762`）两侧同格式对称，不受
  schema v2 影响（快照是同进程同版本读写）。

## 5. 任务分解（T1→T6 依序；T2 起可与 T1 并行review）

### T1 ScriptBox 多实例 + scripts[] schema v2 —— 约 1.5 天

- `ScriptBox.h`：`ScriptSlot`（同构旧槽）+ `ScriptBox{uint32 notified; uint8 count;
  ScriptSlot slots[8];}`；`kScriptFlagDestroyNotified` 语义注记改挂实体级字段；
- `SceneArchive`：`kSchemaVersion=2`；WriteEntity 写 `scripts` 数组（含 guid/class，
  typeId 不持久）；ReadEntity 双读（复数优先/单数兼容）；`Migrate` v1→v2 首级实现
  （单数对象 → 单元素数组 + 版本号回写）；`SceneArchive.h:23-24` 注记同步；
- `ScriptHost`：AttachBehaviour 拆两路——`ResolveSlotBehaviour`（既有槽按 className
  解析，覆写 typeId 不追加；EnterPlay/热重载用）与 `AttachNewBehaviour`（追加槽；
  op4/Inspector 用）；NotifyPendingDestroys 改读实体级 notified；op5 DetachScript
  留给 T3 一并接线；
- `EditorContext`：AttachScript 追加槽（满 8 拒 + 告警）；ResolvePlayScripts /
  RefreshScriptsAfterReload 逐槽；
- `InspectorPanel.cpp:675-730`：Script 段列表化（逐槽 combo/移除/启停；Add Script
  popup 不变）；Hierarchy 脚本图标（`HierarchyPanel.cpp:260`）不动；
- `EditorApp.cpp:3538-3560` smoke-template 断言逐槽迭代；
- 测试：scripts[] roundtrip 不动点、多槽解析装配（mock backend 扩
  `Tests/engine_tests.cpp:1585-1616`）、v1→v2 迁移（.scene）、旧 prefab 双读
  （LoadEntityTree）、槽满拒绝、加载同名重复项清洗（保序留首见 + 告警）。

### T2 SpriteRenderer.spriteGuid + 解析链 —— 约 1 天

- `RenderComponents.h`：尾加 `uint64_t spriteGuid = 0;`（static_assert 12→24）；
  `ComponentCatalog.cpp:56-61` 加 FIELD + ED_ASSET 槽注记同步；C# 镜像
  `Components.cs` + `LayoutTables.cs` 探针行同步 24B；
- `EditorContext`：`ResolveSpriteRefs(Scene&)` 按决策 6 规则；挂 4 站点 +
  `InstantiatePrefabAsset` / `CreateSpriteEntityFromAsset`（后者本就拿 guid，补写
  组件侧 guid）；`CreateSpriteEntity(tag, spriteId)` 加 guid 参重载（生成器用）；
  OpenScene 悬空 spriteId 聚合告警（`EditorContext.cpp:101-123` 段）升 guid 口径；
- `InspectorPanel.cpp:167-229` DrawSpriteSlot：combo/拖入/清空三口双写
  guid+id（payload 已带 guid，`AssetBrowserPanel.cpp:302`）；属性 Undo 快照整组件
  字节，自动覆盖新字段，无额外改动；
- 测试：spriteGuid roundtrip；guid 优先/悬空回退/guid=0 三分支；改名解析；
  旧档（无 guid 字段）加载回退 spriteId 路径；**存量回填**（guid=0 + 已登记 id →
  补 guid 标 dirty；未登记 id 不动）。

### T3 SDK 双路由门面 —— 约 0.75 天

- `Lemon.SDK`：`IComponent` 标记接口（数据组件 struct 逐个标记，behaviour 侧
  LemonBehaviour 实现之）；`GameObject.AddComponent<T>/GetComponent<T>/
  RemoveComponent<T>` 运行时分路（数据分路包既有 SceneOps 静态版）；behaviour
  分路 AddComponent = 幂等 get-or-add（决策 4），查找/退订走 Behaviours 域内表；
- `SceneOps.cs`：op5 `DetachScript`；`Lemon.Entry/Exports.cs` 新导出
  `lemon_scripts_detach(typeId, entity)`（单实例 OnDestroy + ClearSubscriptions）；
  `ScriptHost` 解析该导出（旧 Entry 无导出 = 挂空安全，既有约定）+ op5 防御性
  clamp（typeId 越界/未注册告警跳过，与 op4 同口径）；
- 04 §3 门面草案的 AddComponent/GetComponent/TryGetComponent 对齐注记落定
  （GetComponentInChildren 仍缓——无消费者，不扩面）；
- 测试（TestScript）：AddComponent<LemonBehaviour> 当帧入队帧首生效 + Awake 次
  序；GetComponent 首见；RemoveComponent 单实例退订不断邻脚；同类型 AddComponent
  幂等（二次调用返回既有实例、不双实例）；`Behaviours.Attach` 同实体同类型断言
  （真双挂 = 红字 + 跳过，不崩）。

### T4 模板三拆 + 生成器重生成 —— 约 0.75 天

- `PlayerBehaviour.cs`（239 行）拆 `PlayerMovement` / `PlayerCombat` / `PlayerHud`
  三 LemonBehaviour；共享态（武器表/成长数值）按现码切面归组件或 GameMain 静态——
  切不开的宁可两脚本也不硬拆（留 DevLog 记录切面）；`GameMain.Register` ×3；
  **注册序 = 执行序**（Behaviours 按 typeId 分桶，跨类型 Update 序 = 注册序/
  `[ExecutionOrder]` 而非槽序）：移动 → 战斗 → HUD 注册，HUD 读当帧值；
- `GenerateVsTemplate`（`EditorApp.cpp:604-821`）：AttachScript ×3（:710）；sprite
  写号改 guid 双写（决策 7）；重生成 `Templates/vs-survivor/`（Main.scene 玩家实体
  scripts[3] + 6 prefab v2 格式入库）；
- `--smoke-template` 全链回归（3000 帧断言集含死亡-复活链）。

### T5 GUID 稳定性 smoke（验收⑤）—— 约 0.5 天

- 新 `--smoke-guid`：模板拷贝项目 → 开场景记基线（逐实体 guid→解析 id）→
  追加导入一张新图（记账号移位）+ `mv` 改名既有资产 + 删 `.lemon/manifest.json` →
  重开 → 断言逐实体解析 id 仍指向基线同 guid（= 引用不错位；若只看 spriteId 数字
  则必然错位——这正是本批消除的类别）；
- `tools/editor-regression.sh` full 档加 `grep_step "guid-chain smoke"`（第 14 步）。

### T6 文档回写 + 勾销 —— 约 0.5 天

- 03：§2 ScriptBox 多实例语义 + schema v2 迁移链首例；04：§2.1 ScriptBox 段与 §3
  多脚本注记改「已落地」措辞 + §3.2 对齐清单（同类型多脚本/GetComponent 首见/移除
  单脚本）；05：Inspector Script 段列表化；06：§2.1 spriteGuid 口径 + §2.2 残余
  风险注记勾销 + §3 schema 示例；08：§2 M6a 批⓪ 状态 + §0 表行；09：§6.8 零重录
  先例三（加字段 × 零实例基准场，实测 mismatches=0 落账）；
- DevLog 批⓪ 条目（含模板切面记录）；本页勾销；总览页表格更新。

**合计约 5 个工作日。**

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| ScriptBox 膨胀（328B/实体）入 bench 热路径 | 基准场零脚本实体；View 空池成本不变；bench-survivor 抽查判据 7 兜底 |
| v2 迁移漏通道（.prefab 不走迁移链） | ReadEntity 双读覆盖旧 prefab；测试 T1 明列 LoadEntityTree 用例 |
| spriteGuid 与 spriteId 手工档不一致 | guid 优先 + 解析失败回退 id + 告警（决策 6），测试三分支 |
| 零重录前提被本批自己破坏（误给基准场播 SpriteRenderer） | §4 红字禁区；replay 三档是判据 2，违者当场红 |
| 槽 cap=8 不够用 | ScriptBox 无布局冻结约束，扩容只动 kMaxScriptsPerEntity 一行 + 重生成模板；.scene scripts[] 本就变长 |
| 模板三拆共享态切面返工 | 决策留弹性（两脚本可接受）；切面前先通读 PlayerBehaviour 再动刀 |
| op5/新导出对旧 Entry 兼容 | 导出解析挂空 = 安全 no-op（playResetFn_ 同款约定），T3 测试断言 |

## 7. 验证命令（批⓪完工口径）

```bash
cmake --build --preset mac --target lemon-tests lemon-script-tests lemon-editor
ctest --test-dir build/mac --output-on-failure
# 三档金回放零重录（09 §6.8；m5b2 金档不得重录）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
# 模板重生成 + 机械验收
build/mac/Editor/lemon-editor --gen-vs-template /tmp/m6a-tpl-regen   # 与入库模板 diff 复核
tools/editor-regression.sh full build/mac                            # 含新增 guid 链
build/mac/Editor/lemon-editor --bench-survivor --frames 900          # ≥55fps 不回归
```
