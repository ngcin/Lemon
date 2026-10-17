# M8 批⑮（候选）—— 动画运行时语义加固（终态 / facing 选择 / 播放查询 / Play 热改提示）

Status: planned（候选立项 2026-10-17；**排期同日用户拍板定形**——⑮a 紧随批⑬b、
⑮c 顺手、⑮d 随批⑭ 走查、**⑮b 挪 M9 首批**（8 向素材合流），见 §6）——立项输入：
动画系统全量评估（会话结论四项推荐：终态显式建模 > facing 离散选择 > IsFinished
查询 > Play 热改提示；核心结论 = 帧动画 + 数据驱动状态机闭环完整、无结构性缺陷，
本批全部为小口径语义加固，**不触 ADR-013 红线**（blend tree / layers / sub-FSM
维持禁做））

## 1. 输入与定形

- **评估结论（2026-10-17 会话）**：运行时闭环（Play/Queue/CrossFade/参数黑板/
  any-state/帧事件注册表）与创作面（图编辑器/胶片带/打点 UI）均已收口；残余四缺口
  全部为**语义隐式化或 API 面小洞**，量级 = 单批四段（⑬ 同款分段制，各段独立出口）。
- **共同前提**：全部改动**默认档零行为漂移**（金回放零重录口径）——终态标记缺省空、
  新 API 无人调用即零成本、热改提示仅编辑器态。

## 2. 范围

### In

| # | 面 | 内容 |
|---|---|---|
| 1 | ⑮a 终态（terminal）显式建模 | `.controller` 顶层可选 `terminal: ["Death", …]` 字符串数组（additive——旧引擎/旧档白名单忽略，schemaVersion 不动）；`ControllerDef` 编译期解析为 states 序位图（`BuildControllerCache`）；`AnimGraphSystem` 出边循环加一行守卫：**any 边（`from:"*"`）在终态不命中**（终态 = 不可被全局中断打断；显式 from 边不拦——复活流作者自决）。编辑器：图节点终态徽记（双圈/角标）+ Inspector 复选 + 右键 toggle + i18n 双语；lint 增强：终态挂条件出边 → 黄字（info 级，复活边属合法但提示确认） |
| 2 | ⑮b facing 方向剪辑选择 | **离散选择非混合**（评估结论：flipbook 无姿态代数，blend tree 借参数空间不借插值——Unity 2D 同款退化用法）。C# 门面 `Anim.PlayFacing(g, "walk", dx, dy)`：向量 → 8 方向后缀（N/NE/E/SE/S/SW/W/NW）拼段名 `walk_NE` → 既有 `clipByName` 低频桥解析 + 既有 Play 通道；**缺段回退链**：`walk_<dir>` 缺 → 裸 `walk` 缺 → warn-once no-op。零 schema、零运行时新概念、零新 vtable 槽（纯 SDK sugar，`Lighting.cs` 门面同款定位）；换向频度 = 方向变化沿（非逐帧），解析不缓存（量级评估：每实体每换向一次字符串拼 + map 查，万怪极端场 = 方向沿密度 << 帧率，先不优化，登记 v1.1 触发式缓存） |
| 3 | ⑮c 播放状态查询补齐 | `Anim.IsFinished(g)`：直读 `Animator2D.ended`（FIELD_RT，C# 镜像现成——`Systems.cpp:1189` 非 loop 段钳 total 置位、切段归零，**零新桥零引擎改动**）；loop 段恒 false（ended 语义使然）。`IsPlaying` 语义维持不动（= playOnStart 开关，兼容），注释改写三态口径（Playing / Paused / Finished 互斥面）；`Progress(g)`（time/total 归一进度）**列 v1.1 触发式**——total 在引擎 clip 表，需新桥（clipByName 家族扩展），消费者出现即做 |
| 4 | ⑮d 编辑器 Play 态热改提示 | 快照语义**保留不推翻**（Play 中改 .controller/.clip 当局不生效 = 确定性根基）；编辑器 file-watcher（现成线程）检测 Play 态下动画族资产 mtime 变化 → 控制台 info + AnimControllerPanel 顶部横幅「controller 已改，退 Play 后生效」。消除"静默无感被当 bug"的走查摩擦（b5 走查期实报同族）。零引擎改动 |
| 5 | 测试与文档 | ⑮a：ParseControllerJson terminal roundtrip + 评估单测（any 边终态不命中 / 显式边照常 / 缺省空逐位同）+ EditorTests lint + smoke-anim 徽记断言位（帧预算 138 → +~4 重锚登记）；⑮b：script-tests PlayFacing 解析/回退链/非法向量用例；⑮c：script-tests ended 直读用例；文档 = ADR-013 D4 增补条目（terminal 语义，any-state 开禁同款修订格式）+ 04 §Anim 注记 + Manual 动画章（随批⑭ 手册轨） |

### Out（明确不做，含否决理由）

- **blend tree / layers / sub-FSM**：ADR-013 D4 红线维持（评估结论再确认：整图翻页
  无姿态可混，连续混合属骨骼路径 D5 触发后再议）；
- **controller 参数化状态绑定**（状态名模板 `walk_{dir}` 进图）：把 facing 塞进
  决策层 = 图复杂度换 sugar，方向选择是脚本侧职责（与 D1"脚本直写优先"一致）；
- **Play 态热重载 controller**：推翻快照语义 = 动确定性根基，不做（⑮d 只补提示）；
- **PlayFacing 解析缓存 / Progress 桥**：v1.1 触发式登记；
- **terminal 进 C# SDK 面**（`Anim.IsInState(g,"Death")` 类查询）：无消费者，
  出现即议（需新桥，vtable 余量 12 足够但不动）。

## 3. 设计要点

### ⑮a 终态：opt-in 语义 = 零重录前提

- **为什么顶层数组而非 states 对象化**：`states: ["Idle","Run",…]` 是绑定键平铺
  （状态名 = 集内段名，ADR-013 D2/D4），对象化 = 全仓 .controller 重写 + 旧档
  兼容面爆炸；顶层 `terminal` 数组 = 白名单解析（`ControllerAsset.h:83` editor.nodes
  同款容错先例），旧引擎读新档忽略、新引擎读旧档缺省空——**双向兼容**。
- **求值序不漂移**：守卫加在既有循环过滤处（`Systems.cpp:1360` `t.from != cur`
  旁：`t.from == kAnimAnyFrom && ctrl->IsTerminal(cur)` → skip），无 any 档与
  无 terminal 档路径逐位不变；有 terminal 档的 any 边组内序不动。
- **与既有"终态驻留"拓扑的关系**：现状 Death 靠无出边自然滞留 = 拓扑事实（b11
  anytest 验收语："图不越权"）；本批把它升格为**声明的语义**——不标记 = 旧行为
  （any 边可打进"死亡中"），标记 = 显式封中断。svr-test Duelist 若落标记 = 作者
  选择的**行为变更**（any→Hit 在 Death 态被挡——现由 DuelBehaviour HP>0 脚本门控
  兜着，标记后脚本门控可简化）；该变更按 09 §6.8 口径走"行为链对拍"验收，金回放
  是否重录以对拍结果定（m8b5 档 sim 两档预计漂移 = Death 态 any 消费差异，若
  DuelBehaviour 门控未删则逐位同零重录）。
- **lint 分级**：terminal 状态挂**条件出边**（when/trigger）→ 黄字"终态出边将
  打断驻留语义，复活流请确认"；挂无条件 exitTime 出边 → 不告警（合法自动续）。

### ⑮b facing：后缀表定形

- 8 方向后缀序 = 顺时针 N/NE/E/SE/S/SW/W/NW；方向归并 = 主轴阈值（|dx|、|dy|
  与 0.382（tan22.5°）比较——避免 atan2 浮点跨平台漂移，纯比较可移植）；
- 向量零长（dx=dy=0）= 不换（驻留当前段，调用方自己判停走）；
- 后缀常量表进 SDK 单处定义，doc 注释钉死与素材命名约定（`walk_NE` 族）；
  素材侧无 8 向全档时回退链兜底（只做 E/W 两向的怪 = `walk_E`/`walk_W` 直接可用，
  其余方向回退裸 `walk`）。

### ⑮d 横幅：watcher 复用面

- 编辑器已 watch `Assets/` 与 `Assets/Scripts` 两根（file-watcher 线程，
  `EditorApp.cpp` 族）；本批只加"Play 中 + 扩展名 ∈ {.controller,.clip,.ani} +
  mtime 新于 EnterPlay"三条件 → 置脏标志，面板每帧查标志渲染横幅。不新增线程
  不动 watcher 协议；退 Play 快照重建自然清位。

## 4. 涉面与量级（估）

| 面 | 文件 | 量级 |
|---|---|---|
| schema/解析 | `Engine/Assets/ControllerAsset.h/.cpp`（terminal 数组 + lint 增强 + 序列化定版格式） | ~120 行 |
| 词表/评估 | `Engine/ECS/ControllerTable.h`（位图 + IsTerminal）、`PlayCaches`（编译）、`Systems.cpp:1359` 守卫一行 | ~40 行 |
| 编辑器 | `AnimControllerPanel.cpp`（徽记/toggle/横幅）+ anim.json 双语 | ~180 行 |
| SDK | `Lemon.SDK/Anim.cs`（PlayFacing ~60 行 + IsFinished ~15 行 + 注释改写） | ~80 行 |
| 测试 | engine-tests（terminal 解析/评估/lint）+ script-tests（PlayFacing/IsFinished）+ smoke-anim 断言位 | ~300 行 |
| 文档 | ADR-013 D4 增补 + 04 注记 + Manual 章（⑭ 手册轨合流） | ~80 行 |

合计 ~800 行，四段各自独立出口（⑬ 同款）；单批 ≤2k 行 / ≤3 天纪律内（~2.5 日）。

## 5. 出口判据

- **⑮a**：terminal roundtrip 单测绿；评估单测三例（any 终态不命中 / 显式边照常 /
  缺省空逐位同）绿；图编辑器徽记 + toggle 可用（smoke-anim 断言位）；lint 黄字
  单测；**无 terminal 档金回放三档 mismatches=0 零重录**（机械证明：现录现放）；
  构建零警告。
- **⑮b**：script-tests PlayFacing 全绿（8 向 + 回退链 + 零向量 no-op）；svr-test
  或样本侧一个真实消费者（E/W 两向即可）；零新 vtable 槽实证（60 不变）。
- **⑮c**：IsFinished script-tests 绿（Once 播完 true / loop 恒 false / 切段归零）；
  IsPlaying 注释与 Manual 口径同步。
- **⑮d**：进 Play 改 .controller → 横幅出现（手测 + watcher 回调单测）；退 Play
  重建后横幅消；无 Play 态改动零渲染成本。
- 全批：单测/script-tests/ctest 4/4/回归 full 26 步全有绿记录；vtable 60·组件 33·
  系统 21 不变；smoke-anim 帧预算重锚登记。

## 6. 排期位次与依赖（2026-10-17 用户拍板定形）

- **拍板结论**：**⑮a 紧随批⑬b**（同函数域 `ControllerStructuralWarnings`，同窗口
  做完避免冲突）；**⑮b 挪 M9 首批**——8 向素材才有真实消费者，塔防段（M9 =
  Tilemap+TD）怪天然 8 向走位，与素材面合流落地即有验收对象，提前做只能靠
  script-tests 假消费者撑出口（违背可量化验收纪律）；**⑮c 随 ⑮a 顺手清**（直读
  现成字段）；**⑮d 随批⑭ 组合走查过目**；执行序 = ⑬b → **⑮a → ⑮c** → ⑬c →
  批⑭（带 ⑮d）→ M8 收口 → M9 首批 ⑮b。
- **随 ⑮a 的现成验收用例**：svr-test Duelist 的 Death 状态落 terminal 标记 +
  **删 DuelBehaviour 的 HP>0 门控**（现由脚本兜着"死亡中不受击"）——行为变更按
  伤害时间线对拍验收（09 §6.8 口径），脚本变简单即语义加固收益的实证；对拍
  漂移则金回放按需重录评估（m8b5 档 sim 两档）。
- **可并行面**：⑮b/⑮c（SDK 域 `Anim.cs`）与 ⑬c payload 类型化（`AnimEvents.cs`/
  Events 面）不同文件域，M9 侧可穿插；⑮d（编辑器 watcher 面）独立。
- **M8 段收官不被本批阻塞**（批⑦ gate = 批⑥ mac 侧已收口）；⑮b 移入 M9 后，
  M8 段动画系列以 ⑬–⑮a/c/d 收口。
