# M6a 批② T3d：动画控制面——绑定、.controller 状态机、帧事件、资产族定名（2026-09-27）

Status: **done**（四小批一次落地 + ani.scene 双怪对决终验通过，2026-09-27；
[DevLog](../../DevLog/2026-09-27-m6a-b2-t3d-anim-controller.md)。余真人验收：编辑器
打开 svr-test/Scenes/ani.scene 进 Play 观战 + Inspector AnimGraph 绑定槽实操。
执行偏差三条记录在 DevLog：①金回放三档**重录**（加组件入哈希流，M5 批②同款
先例——原计划"零重录"论证只覆盖字段不覆盖新组件）；②附带解堵 kMaxTextureSlots
与 ImGui 描述符池 64→256（M6c 图集打包前过渡）；③手写 GUID 低位撞号返工一次
（0x101/0x201 唯一低位））

## 0. 背景与决策链

用户四轮讨论（2026-09-27，接 T3-UX3 集内落位修复）：多角色 idle/run 常态命名分布 →
"不可能每角色一套状态机逻辑" → 对照 Unity（Controller/Override）与 Godot
（AnimationPlayer/AnimationTree——状态机每 Tree 一份，非每动画一份）机制 → 定
**四层分层 + 资产族对齐 + 骨骼只留边界**。全部决策与取舍见 ADR-013，本文只分解实施。

T3c §6 预留的"下批（T3d 候选）：帧事件 + 段末自动过渡"被本批吸收升格：段末过渡 =
controller 的 exitTime 过渡；帧事件 = 批③ 原样保留。

## 1. 批次分解（四小批依序，各自独立验收/可单独回滚）

### 批① 实体↔集显式绑定 + 引用卫生

- 新组件 `AnimGraph { uint64_t controllerGuid; uint64_t setGuid; }`（16B，入档入哈希，
  参照槽位组件先例）。**Animator2D 28B 冻结布局不动**——绑定走新组件不加字段
  （static_assert/金档前提零触碰）。
- 按名解析作用域改绑定驱动：`Play(g,"run")` = 实体 `AnimGraph.setGuid` 集内查名；
  无绑定回退现行"当前 clip 所属集"口径（向后兼容，svr-test 零改动可用）。
- SDK：`Anim.SetSet/SetController`（GUID hex 入参，纯字段写）；模板与样板脚本去
  GUID 魔法常量改按名（`kMobWalk = Anim.ClipId("...")` → `Play(g,"walk")`）。
- Inspector：Animator2D 邻位绑定区两资产槽（ED_REF hint/拖入/右键清空，clip 槽同款；
  T3c 遗留"Inspector 集视图化"在此兑现）。
- 验收：script-tests 按名解析三口径（绑定集/无绑定回退/双 miss no-op）；回归 14/14；
  金回放零重录（新组件基准场零实例）。

### 批② .controller 状态机最小面（核心批）

- schema（06 §2.2 扩行，JSON 手写定版同 .clip/.ani 先例）：

  ```json
  { "schemaVersion": 1, "name": "BasicCharacter",
    "params": [ {"name":"speed","kind":"float"}, {"name":"attack","kind":"trigger"} ],
    "entry": "Idle",
    "states":  [ {"name":"Idle"}, {"name":"Run"}, {"name":"Attack"} ],
    "transitions": [
      {"from":"Idle","to":"Run","when":[{"param":"speed",">":0.1}]},
      {"from":"Run","to":"Idle","when":[{"param":"speed","<=":0.1}]},
      {"from":"Attack","to":"Idle","on":"exitTime"} ] }
  ```

  参数 ≤8（编辑器拦）；无 any-state/blend/layer（ADR-013 D4 边界）。
- 运行时：EnterPlay 解析进 World 级 ControllerTable（ClipTable 同款快照/登记序
  relPath 升序）；`AnimGraphSystem` 排 CSharpBatchSystem 后（管线尾加，见 §2）：
  读 AnimParams → 评估当前状态出边（纯函数）→ 写换段队列（Play/Queue/CrossFade
  语义复用 M6a 批①，不新造切换机制）；trigger 评估即清。
- `AnimParams` 组件（FIELD_RT 入哈希不入档：`{u16 slot, u8 kind, u8 pad, f32 value}×8`；
  槽位 = controller 参数表定序）；EnterPlay 按 controller 默认值初始化。
- SDK：`Anim.SetParam/GetParam/Trigger(g, name, ...)`（vtable 表尾追加参数名→槽解析
  1 项，clipByName 先例）；脚本直写 Play 仍优先于图（ADR-013 D1）。
- 状态→clip 解析：经 `AnimGraph.setGuid` 集内按**状态名**查（集 = 绑定表，ADR-013
  D2）；缺绑 = warn-once（实体×状态去重）+ 保持当前帧。
- 验收：engine-tests 评估纯函数单测（条件命中/exitTime 段末切/trigger 清零/缺绑/
  坏 controller 档防御）；smoke-anim 扩 graph 链（种植 controller+集+绑定，断言
  参数驱动切段）；script-tests SetParam→切段端到端；金回放零重录（FIELD_RT +
  基准场零实例零调用）；bench-survivor 动画/杂项 1.0ms 预算档不超。

### 批③ 帧事件 + 播完可观测（T3c 原案）

- `.clip` 扩 `events[]: [{frame, name}]`（帧号→事件名；payload v1.1）。
- AnimatorSystem 帧跨越检测（curFrame 前后差，纯函数推导 ⇒ 事件序确定）→ 既有
  Events 通道发包；C# `Events.Subscribe<AnimEvent>`（判定帧/音效/发弹消费面）。
- Once 段末 = 隐式 `finished` 事件——补上 `IsPlaying` 判不了 Once 播完的缺口
  （Anim.cs:148 自认）。
- 验收：事件序确定性单测（loop 回绕/Queue 切段边界不重发不漏发）；回归 14/14。

### 批④ 资产族定名迁移（ADR-013 D2）

- AssetDatabase 四行（`AssetDatabase.cpp:27/29/168/170`：Extension + TypeFromExtension，
  `.clip→.anim`、`.ani→.override`）；`.meta` type 字符串不动（只换扩展名，.meta 零迁移）。
- 存量迁移 db.Rename 保 GUID：demo/svr-test、Templates/vs-survivor、smoke-anim 种子、
  测试 golden 文件；编辑器源码 `.clip`/`.ani` 字符串清扫（打开路由/过滤器/向导文案）。
- docs 同步：06 §2.2 资产族表、05 §7、03 §8.1、04 SDK 行、历史行映射注记
  （ADR-012 `.table→.tab` 修订同款："历史文档中 .clip/.ani 按此映射读取"）。
- 验收：金回放零重录（GUID/clipId 不变 ⇒ 哈希不变）；回归 14/14 + smoke-anim +
  editor-regression full。

## 2. 时序与确定性口径（批② 核心义务）

- 管线尾加 `AnimGraphSystem`：#13 AnimatorSystem → #14 CSharpBatchSystem → **#15
  AnimGraphSystem**（读当 tick 脚本参数）→ 写队列 → 下一 tick #13 消费——与脚本
  直写 Play 完全同拍，回放语义零新增例外。
- AnimParams FIELD_RT 入哈希（影响 curFrame/spriteId 演化必入哈希——M6a 批①
  换段队列同款论证）；ControllerTable World 持有非 ECS，unordered_map 迭代序不入
  哈希，登记序 relPath 升序（T3c §5 同口径）。
- 图评估无逐帧累加隐状态：当前状态由"换段队列消费后的 clipId ↔ 状态名"反查
  （clip→状态经集绑定双向解析），trigger 是唯一边评估期状态且即评估即清。

## 3. 挂起（不进本批）

可视化状态机图（游戏阻塞触发，Unity/Godot 图编辑器对齐留后）；逐帧时长；
多槽 Queue；Random/Queue LoopMode；Play 态资产热更；骨骼动画（ADR-013 D5 边界内，
届时另立 ADR 定输出路径与运行时选型）。

## 4. 文档回写（T6 归口或随批落）

03 §8.1（图评估/参数/新系统行）、04（Lemon.Anim 新面）、05 §3（绑定区）/§7、
06 §2.2（.controller schema + 资产族表）、08 M6a 行。
