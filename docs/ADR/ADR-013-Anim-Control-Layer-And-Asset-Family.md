# ADR-013：动画控制面分层与资产族对齐（.controller / .override / .anim）

- 日期：2026-09-27
- 状态：**已采纳**（M6a 批② T3d 落地同日定稿——四小批 + ani.scene 双怪对决终验，
  [DevLog](../DevLog/2026-09-27-m6a-b2-t3d-anim-controller.md)；批④迁移注记：历史
  文档中 `.clip/.ani` 字样按 `.anim/.override` 映射读取，ADR-012 `.table→.tab` 同款）
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

| 资产 | 现名 | 定名 | Unity 对应 |
|---|---|---|---|
| 帧动画剪辑 | `.clip` | **`.anim`** | AnimationClip |
| 动画集（每角色绑定/换皮） | `.ani` | **`.override`** | AnimatorOverrideController（简化后缀） |
| 共享状态机（新增） | — | **`.controller`** | AnimatorController |

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
- **不抄 mecanim 全量**：无 blend tree / layers / sub-state machine / any-state /
  姿态混合（CrossFade = 延迟切换的 M6a 批①定案不变；帧动画无姿态可混）。

### D5：骨骼扩展边界（预留，不实现——本 ADR 起为验收红线）

三接缝，从现在起守住，骨骼接入时决策层/指令层零改动：

1. 决策/指令层只说 clipId + time + loop，**永不接触** spriteId/帧号/骨骼；
2. 采样 = 纯函数 (clip, time) → 输出，且**编辑器预览与运行时同一条采样路径**；
3. ClipTable 泛化为带表示标签（frame/skel/…）的 clip 注册表。

输出路径届时二选一（另立 ADR）：骨骼 = 子实体（复用批处理/排序，主角与 Boss 可行，
杂兵海不可——实体数 × 骨数）或新 Renderable 类型（性能好，工作量大）；品类分工 =
杂兵帧动画、主角/Boss 骨骼。Spine 许可证（编辑器席位 + runtime 商业条款）是选型
前置风险：采样后端接口不得绑定特定厂商类型，开源替代留退路。

## 后果

- 正面：N 角色一份状态机逻辑（OnHit 复制粘贴止于现状两处）；消灭 GUID 魔法常量与
  隐式集作用域；资产族/目录与 Unity 肌肉记忆一致；骨骼接入时控制面零改动。
- 负面/边界：v1 无可视化图编辑（`.controller` JSON 手写 + Inspector 编辑；图编辑器
  仍挂"游戏阻塞触发"）；EnterPlay 快照语义不变（Play 中改 .controller 当局不生效）；
  后缀迁移触碰存量模板/demo/smoke 种子（保 GUID 迁移，独立小批走，见批次文件）。
- 关联：T3c §6"下批（T3d 候选）"由本 ADR 升格定形；批次分解与验收见
  [Plans/M6a/2026-09-27-b2-t3d-anim-controller.md](../Plans/M6a/2026-09-27-b2-t3d-anim-controller.md)。
