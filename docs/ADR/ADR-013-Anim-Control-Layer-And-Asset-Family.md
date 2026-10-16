# ADR-013：动画控制面分层与资产族对齐（.controller / .override / .anim）

- 日期：2026-09-27
- 状态：**已采纳，D2 于 2026-10-13 修订，D6 于 2026-10-16 增补，D4 于 2026-10-16 修订（any-state 开禁）**（M6a 批② T3d 落地同日定稿——四小批 + ani.scene 双怪对决终验，
  [DevLog](../DevLog/2026-09-27-m6a-b2-t3d-anim-controller.md)；批④迁移注记：历史
  文档中 `.clip/.ani` 字样按 `.anim/.override` 映射读取，ADR-012 `.table→.tab` 同款；
  **2026-10-13 D2 修订**：实用反馈推翻 Unity 对齐命名，`.anim→.clip`、`.override→.ani`
  复名 + 开项目保 GUID 自动迁移，[DevLog](../DevLog/2026-10-13-anim-ext-rename-clip-ani.md)——
  历史文档中 `.anim/.override` 字样按 `.clip/.ani` 映射读取；
  **2026-10-16 D6 增补**：精简可视化图编辑纳入 v1——b9 三表工作台实测"表单形态不可用"
  改道，图管结构 + 检查器管细节；布局随 `.controller` 顶层 `editor` 字段持久化
  （运行时零感知），[批文件](../Plans/M8/2026-10-16-b10-anim-graph-editor.md)；
  **2026-10-16 D4 修订**：any-state 开禁（触发条件当日命中——svr-test 实测每态手抄
  受击/死亡边成扇入乱麻），求值序/自环/编辑面语义见 D4 注记修订段，
  [批⑪ 批文件](../Plans/M8/2026-10-16-b11-any-state.md)）
- 影响：`03-ECS-Runtime.md` §8.1（图评估系统/参数组件）、`04-CSharp-Scripting.md`（Lemon.Anim 参数面）、`05-Editor.md` §3/§7（绑定槽/工作台）、`06-Asset-Pipeline-Out-of-Box.md` §2.2（资产族后缀）、`08-Development-Roadmap.md`（M6a T3d）、`Editor/Assets/AssetDatabase.*`（扩展名四行）、`Engine/ECS/ClipTable.*`（按名解析作用域）、新组件 `AnimGraph`/`AnimParams`、新系统 `AnimGraphSystem`

## 背景

T3c 已把 `.ani` 定位为"Unity .controller 的壳"，但实际只是"段名→clip 引用"的平铺清单——
没有 entry 状态、没有过渡边、没有参数。切换逻辑现状 = 每游戏手写 C#，且重复已经发生：
vs-survivor `PlayerCombat.cs:16-50` 与 demo/svr-test `PlayerBehaviour.cs:108-119` 是两份
几乎相同的 `Play(hit)+Queue(walk)` 复制粘贴，外加 GUID hex 魔法常量
（`Anim.ClipId("5bd31a7c20000002")`）；按名解析的作用域 = "实体当前 clip 恰属哪个集"
的隐式反推（T3c 计划自列遗留）。

用户 2026-09-27 连续四轮讨论定方向：①多角色共享状态机逻辑（不可能每角色一套）；
②资产族后缀与目录对齐 Unity；③集内动画落位修复（T3-UX3）后确认"跨集同名是常态"；
④骨骼动画（含 Spine 等外源格式）**本阶段不实现**，只在框架层面预留扩展边界，当前
一切按帧动画规划。

## 决策

### D1：四层分层——决策层新增，指令/采样/输出三层保留

| 层 | 职责 | 现状与本决策 |
|---|---|---|
| 决策层（新增） | `.controller` 状态机：状态词表 + 过渡条件 + 参数黑板 | 新建，**表示无关** |
| 指令层 | Play/Queue/CrossFade 换段队列（`nextClipId/fadeRemain/nextLoop`，FIELD_RT） | **保留不动**——图评估的落点就是写这组字段 |
| 采样层 | (clip, time) → 帧/姿态，纯函数 | 保留；帧映射纯函数是确定性回放的根，未来新表示同构 |
| 输出层 | 写 `sr->spriteId` | 保留；骨骼未来走新组件/新 renderable，不改 SpriteRenderer |

- **命令式 API 与图共存，脚本直写优先级高于图**：受击强切这类即时打断 = 脚本职责；
  图管常态循环（idle↔run、段末回归）。
- 图评估 = `AnimGraphSystem`，排 CSharpBatchSystem **之后**（读当 tick 脚本写的参数）、
  写换段队列由下一 tick AnimatorSystem 消费——与现有脚本 Play 同拍一帧延迟，
  时序纪律不新增例外。
- 确定性：条件评估纯函数；trigger 消费即清发生在图评估内；参数 = `AnimParams`
  组件（FIELD_RT 入哈希不入档，EnterPlay 从 controller 默认值初始化，基准场零实例 =
  金回放零重录，09 §6.8 口径）。

### D2：资产族后缀对齐 Unity（兑现 2026-09-19"扩展名与 Unity 一致"既定纪律）

> **2026-10-13 修订（用户实用反馈推翻）**：后缀复名 `.anim→.clip`、`.override→.ani`。
> 推翻动因：`.override` 取自 Unity AnimatorOverrideController（冷门概念，多数 Unity
> 用户也不认识），实际使用两周反馈"太奇特"——语义指向错位（集不是"覆盖"任何东西）；
> 原"`.ani` 与 `.anim` 一字母差必混"的反对理由随 `.anim` 退役消失（新对 `.clip/.ani`
> 视觉区分充分）。Unity 对齐纪律在动画族破例（.clip 亦非 Unity 名），其余资产族不动。
> 迁移 = 开项目自动改名（文件 + .meta 同行，guid/type 零迁移，manifestCarry 键随行
> 保 spriteId——`AssetDatabase::MigrateLegacyAnimExtensions`），仓内存量（demo/模板/
> Samples）已同步改名；`.meta` 的 "clip"/"animset" type 字符串自始未动。

| 资产 | 现名 | 定名 | Unity 对应 |
|---|---|---|---|
| 帧动画剪辑 | `.anim` | **`.clip`** | AnimationClip（Unity 用 .anim，本族破例） |
| 动画集（每角色绑定/换皮） | `.override` | **`.ani`** | AnimatorOverrideController（概念参照，名不复用） |
| 共享状态机 | `.controller` | **`.controller`**（不变） | AnimatorController |

- 改 `.ani` 的动因：与 `.anim` 仅一字母之差，搜索/文档/肌肉记忆必混；且其角色
  本就随本 ADR 从"controller 壳"移位为"Override 对应物"。
- 迁移走 `db.Rename` 保 GUID——集段引用/prefab clipId/金档哈希全不断链
  （CommitSegRename 同款机制）；`.meta` type 字符串不动，只换扩展名。
- 集语义升级：从"段名→clip 清单"扩为"**状态名→clip 绑定表**"（字典式，允许缺绑）。

### D3：目录约定（约定非机制，现行 SetClipDir 零改动支持）

集中式布局：`Assets/Animations/<角色>/`（集文件放 Animations/ 下，clip 子文件夹在旁
——现行"集在哪、文件夹在旁"的落位规则天然产出）+ `Assets/Controllers/`（全项目共享的
状态机库）+ `Animations/Shared/`（真正共用的动画单份多集引用）。文件名跨文件夹可重
（唯一性 = GUID；T3-UX3 已落位的跨集同名语义）。

### D4：词表语义——角色间动画数量不一致是工况不是问题

- controller = **按原型**（archetype）的状态词表（取并集；全项目一个小 controller
  库，非一个巨图——人类角色与炮塔不同原型各一份）。
- `.override` 绑定 = 字典式（状态名→clip），**允许缺绑**：缺绑状态靠"入口过渡条件
  永不满足"保持不可达（角色无该能力 → 逻辑不写该 trigger / bool 封门），零成本。
- 真进入空绑状态 = 作者错误：warn-once 红字 + 保持当前帧（对齐既有"队列目标未命中 =
  warn-once 丢队列"哲学）。
- v1 过渡条件最小面：参数比较（float `<`/`<=`/`>`/`>=`，bool，trigger）+ 段末
  （exitTime，= Queue 的图化形态，**吸收 T3c 遗留的"段末自动过渡表"**）。
- **不抄 mecanim 全量**：无 blend tree / layers / sub-state machine /
  姿态混合（CrossFade = 延迟切换的 M6a 批①定案不变；帧动画无姿态可混）。
  **any-state 已于 2026-10-16 批⑪ 开禁**（见下方修订段）；blend tree / layers /
  sub-FSM 维持红线。
- **D4 红线复杂度注记（2026-10-16 批⑩ 立项时结算；any-state 段已按批⑪ 修订）**——
  四项均为批级**全栈**工作（运行时 + schema + 编辑器 + 校验 + 金回放重录），且互相
  耦合（单做一项易产出与后续打架的临时语义，mecanim 是耦合系统，不做半套）：
  - **any-state** ~~（从任意状态出发的过渡）~~ **→ 2026-10-16 批⑪ 开禁**（触发条件
    当日命中：svr-test Duelist 实测 13 边中 9 条为 ×→Hit/×→Death 扇入手抄，Unity
    Mecanim / Cocos Marionette 同款节点，Godot 无——其惯用替代 = 代码直跳，本引擎
    C# `Anim.Play` 同型出口本就存在，D1 脚本优先）。定形语义：
    schema `from: "*"`（状态名 `*` 保留字拒；仓内零存量自环已核——自环 from==to
    解析拒，图切换无重启语义，重启归脚本 Play 同段重播）；
    **求值序 = any 边先于状态边、组内文件序、首条命中即切、每 tick 至多一条**
    （编译期稳定前置实现，无 any 档序与文件序逐位一致——金回放零重录口径）；
    当初预判的三难点落地口径：自环防重 = 切换体 `cid != an.clipId` 守卫天然 no-op
    （= Unity Can-Transition-To-Self 默认关）；trigger 消费 = 命中边消费即清（含
    同态 no-op——保守不误清）；exitTime 不互斥 = any+exitTime 合法（"任意当前段
    收尾时切"，an.ended 语义照用）。编辑面 = Any 节点（批⑪ 五轮：图空间可拖 +
    位置随 `editor.nodes` 保留键 `"*"` 持久化（未拖过 = 无档键、缺省布局包围盒
    左外侧，惰性落位不污染旧档）+ 右键「删除」= 清空全局边——节点本体恒在 =
    建边入口（Unity Any State 同款不可移除））+ 把手拖线/右键建边。
  - **sub-FSM**（子状态机）：状态标识从名字变路径（状态名即集绑定键、全局唯一，
    嵌套必撞名）、评估递归化、当前状态反推变"叶子→路径映射"、编辑器加钻取
    导航。**~2–3 批**。复活触发 = 真实项目单控制器 >15–20 态（目标品类
    典型 3–8 态）。
  - **layers**（并行层 + 权重 + 遮罩）：每实体多状态机并行 + 输出合成策略；
    单 sprite 输出下语义 ≈ 已有 CrossFade 队列，遮罩依赖骨骼/多渲染器（不存在）。
    **~3–4 批**。复活触发 = 骨骼路径（D5）落地时一并评估。
  - **blend tree**（参数轴加权混合）：采样层需多 clip 合成器、输出层需多 sprite
    带权渲染；帧动画无姿态代数——加权叠加 = 重影，真混合属骨骼路径（D5）。
    **~3–4 批**。复活触发 = 骨骼路径（D5）落地时一并评估。
  - 共同耦合（为什么不能"顺手加"）：当前状态靠播中 clipId 反推（单一值前提）；
    每 tick 至多一条切换、无级联（确定性口径）；采样 = 单 clip 纯函数。

### D5：骨骼扩展边界（预留，不实现——本 ADR 起为验收红线）

三接缝，从现在起守住，骨骼接入时决策层/指令层零改动：

1. 决策/指令层只说 clipId + time + loop，**永不接触** spriteId/帧号/骨骼；
2. 采样 = 纯函数 (clip, time) → 输出，且**编辑器预览与运行时同一条采样路径**；
3. ClipTable 泛化为带表示标签（frame/skel/…）的 clip 注册表。

输出路径届时二选一（另立 ADR）：骨骼 = 子实体（复用批处理/排序，主角与 Boss 可行，
杂兵海不可——实体数 × 骨数）或新 Renderable 类型（性能好，工作量大）；品类分工 =
杂兵帧动画、主角/Boss 骨骼。Spine 许可证（编辑器席位 + runtime 商业条款）是选型
前置风险：采样后端接口不得绑定特定厂商类型，开源替代留退路。

### D6：精简可视化图编辑纳入 v1（2026-10-16 增补）

原负面项"v1 无可视化图编辑（`.controller` JSON 手写 + Inspector 编辑）"由 b9
三表工作台实测推翻：表单形态"完全不会用"，可视化图是结构导航的正确载体。
定形 = **图管结构 + 检查器管细节**——节点=状态、边=transition 的画布编辑
（拖动/连线/选中/删除），条件与字段细节恒在选中检查器（复用 b9 控件）；布局随
`.controller` 顶层可选 `editor` 字段持久化（旧档缺省 = 确定性自动布局；解析器
白名单读取，未知字段天然忽略 → 运行时/旧档零感知）。**D4 红线除 any-state 外
不变**（2026-10-16 批⑪ 开禁 any-state，见 D4 修订段）：无 blend tree / layers /
sub-FSM。交互清单与验收判据见 [批文件](../Plans/M8/2026-10-16-b10-anim-graph-editor.md)。

## 后果

- 正面：N 角色一份状态机逻辑（OnHit 复制粘贴止于现状两处）；消灭 GUID 魔法常量与
  隐式集作用域；资产族/目录与 Unity 肌肉记忆一致；骨骼接入时控制面零改动。
- 负面/边界：v1 含**精简**可视化图编辑（2026-10-16 D6 增补；原"JSON 手写 + Inspector"
  形态由 b9 实测推翻）+ any-state 全局中断边（2026-10-16 D4 修订开禁；求值序 =
  any 先评）；mecanim 全量（blend tree / layers / sub-FSM）仍为红线；
  EnterPlay 快照语义不变（Play 中改 .controller 当局不生效）；
  后缀迁移触碰存量模板/demo/smoke 种子（保 GUID 迁移，独立小批走，见批次文件）。
- 关联：T3c §6"下批（T3d 候选）"由本 ADR 升格定形；批次分解与验收见
  [Plans/M6a/2026-09-27-b2-t3d-anim-controller.md](../Plans/M6a/2026-09-27-b2-t3d-anim-controller.md)。
