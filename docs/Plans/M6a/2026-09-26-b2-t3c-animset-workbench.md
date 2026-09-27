# M6a 批② T3c：动画集工作台 + 按名播放（2026-09-26）

Status: **done**（除真人验收一项）；批次 = [b2 内容生产总计划](2026-09-25-b2-content-production.md) T3 的第三轮（T3 初版 → T3b 三通道 → **T3c 集化重构**）。

## 0. 背景与用户驱动

T3b 交付后用户实测反馈：动画创建"很孤立"——一个角色 attack1/attack2/walk/run/idle 各是
孤立 .clip，不是一个整体的编辑体验；点名 Godot SpriteFrames 工作流为参照，提出
"目前的动画可能要重新设计"。

三轮讨论收敛（AskUserQuestion + 追问）：

1. **架构**：工作台方案（编辑器层重构，file model 先维持）→ 本批升级为显式集容器。
2. **目录=角色作用域**（首版设计）→ **被用户否决**：实际开发资源不可能完全按目录
   约定组织。修正：作用域 = 显式**集成员关系**，与文件夹无关。
3. **文件形态**（用户要求先看 Unity/Godot 实况再定）：
   - Godot = 单文件内联（SpriteFrames.tres，段无独立身份，名字即身份；改名同样断
     引用——autoplay/play(name) 不自动传导；变体复用 = 整份复制）。
   - Unity = 段文件 + 容器（.anim 各有 GUID = **Lemon 现有 .clip**；.controller 容器
     存状态名单 + 状态机；变体复用 = Override Controller）。
   - **用户决策：容器 + 段文件**（Unity 盘面）。Lemon 已把 Unity 路线前半段建完，
     .ani = 补 .controller 的壳；段身份/GUID/金档零迁移。
4. **状态机节奏**：分级推进——本批集工作台 + 按名播放；**帧事件 + 段末过渡下批**
   （ARPG 手感价值最高）；可视化状态机图挂"游戏阻塞触发"。

## 1. 数据模型（schema）

`.ani`（JSON，与 .clip 同款手写定版格式）：

```json
{
  "schemaVersion": 1,
  "name": "player",
  "segments": [
    { "name": "idle", "clip": "<guidHex16>" },
    { "name": "walk", "clip": "<guidHex16>" }
  ]
}
```

- **段身份 = .clip 文件 GUID**（集只存引用）——改名随 .meta 走引用不断，重建走墓碑
  复活（T3c-1 修复的语义正是为此服务）。
- 段名 = 集内按名解析键；编辑器保存校验非空 + 集内唯一（运行时对重名取路径序先者
  ——编辑器拦在写盘前）。约定：段名 = 段文件 stem（创建/重命名流强制，手改不拦）。
- AssetType 新增 `AnimSet`（枚举插位自由——.meta 按 AssetTypeName 字符串序列化）。
- 成员关系互斥（一段至多属一集）：编辑流不产生多集归属；运行时登记遇跨集重复 =
  路径序首集胜 + 红字（数据面自愈，不炸 Play）。

## 2. 运行时（按名播放）

- `ClipTable` 新增集索引：`RegisterSet(setId, {name, clipId}...)` /
  `FindByName(setId, name)`（0 = miss）/ `SetOfClip(clipId)`（0 = 无集）；`Clear()`
  同清。World 持有、非 ECS、零哈希——**不入 StateHash，基准场零调用 = 金回放逐位
  不变**（09 §6.8 同款口径；本批未动任何 ECS 布局与 ClipDef）。
- `BuildPlayClipCache` 尾段：遍历 AnimSet 条目（Entries() = relPath 升序 → 重名/
  跨集归属一律路径序先到先得，可复现），悬空段/坏段红字跳过不炸 Play。
- 作用域语义（Godot SpriteFrames / Unity Animator 同款集合内局部）：`Play(g, "walk")`
  = 实体当前段（`Animator2D.clipId`）所在集内查名；**跨集同名互不干扰**（玩家和每只
  怪物各有 idle/walk 是常态不是冲突）。实体无段/不属集/集内无名 = 解析失败。
- vtable 表尾追加 `clipByName(entity, name) -> int64`（-1 = 失败；空宿主降级同 T2
  表通道先例）；SDK 字符串重载（Play/Queue/CrossFade）改为**按名优先 → GUID hex
  回退**（旧脚本传合法 hex 恒可达，向后兼容）；miss = stderr 红字（每实体×名去重）
  + no-op——**绝不拿 0 写组件**（clipId 0 = 挂 Animator 关闭语义）。

## 3. 工作台 v2（Animation 面板）

- 目标双态：`.ani` 集模式 / 裸 `.clip` 传统模式（T3b 全机制原样保留，向后兼容）。
  下拉集在前；双击 .clip 经 `OpenAnimationEditor` 集归并解析（现场扫描 .ani 成员
  ——双击频度低，解析成本可接受）→ 已属集则开集并选中该段。
- 集模式布局：目标行（集下拉 + 新建动画集… + 新建…）→ 集头行（集名 + 保存集）→
  左列段清单（190px；单击选中段 → 右区复用段编辑器；右键 重命名/从集移除/删除段
  文件）+ 右区段编辑。
- `+ 新建动作` 连续建段：段名/fps/循环 + 内嵌精灵表框选（复用 DrawSheetBody
  createMode）→ 添加到集（TryCreateClip 落盘到集目录 → 追加段 → 保存集）→ 表单
  可再来（不停向导，全段落进同一集）。
- 重命名 = db.Rename 改段文件（GUID 随 .meta 走）+ 集条目改名 + 保存集；从集移除
  保留文件；删除段文件 = 墓碑 + 从集摘除。
- 入口：浏览器双击 .ani / .clip（归并）；文件夹右键"新建动画集…"（可勾"并从源目录
  图片建首段"——T3b 通道 A 的集化版）；"从此文件夹创建动画…"保留（建裸段）；Inspector
  Animator2D clip 槽右键"在动画工作台打开"。
- ImGui 纪律沿用 T3b 修正批：全部弹窗边沿触发（wizPending_/pickPending_/
  setCreatePending_/segRenamePending_）、Rescan 后只用 guid 重查条目。

## 4. 验证

| 项 | 结果 |
|---|---|
| 引擎单测 TestAnimSetAndClipIndex（27 checks：golden 格式/roundtrip/空集/坏档/索引七口径） | ✔（engine-tests 33470 OK） |
| script-tests TestClipByName（命中/集内无名 no-op/GUID hex 回退，typeId 13 探针） | ✔（1574 OK；behaviours 类型计数断言 13→14 同步更新） |
| smoke-anim 扩链（anim-set.ani 种植 + roundtrip + Play 快照集登记 + 按名/反查双口径，RESULT 增 `set(rt/cache)`） | ✔ `set(rt=YES cache=YES) => OK` |
| 构建双警告基线不变（kSmokePngGuid/ent unused 均为存量） | ✔ |
| 金回放口径 | 见 §5 |
| 完整回归 14 步 | ✔（2026-09-26 全绿） |
| 真人验收：真素材连续建五段（攻击1/攻击2/行走/跑步/idle）+ 脚本 `Play(g,"attack1")` | **余**（用户侧） |

## 5. 金回放口径（零重录论证）

本批未动：Animator2D 布局（28B 冻结）、ClipDef 字段、帧映射纯函数、场景序列化。
新增状态全部落在 World 持有非 ECS 通道（ClipTable 集索引两张 map）+ 编辑器资产层
（新 AssetType）。基准场景/金档脚本零 by-name 调用 → 集索引从不进入任何确定性路径
（unordered_map 迭代序不入哈希）；登记序 = relPath 升序，仅在有集项目且运行时按名
查询时生效。

## 6. 遗留与下批

- 下批（T3d 候选）：帧事件（.clip `events[]`：判定帧/音效/发弹）+ 段末自动过渡表
  （.ani 扩展位已留；都走非 ECS 通道，零重录）。
- 挂起：可视化状态机图（游戏阻塞触发）；Override Controller 式变体复用（远期）；
  Inspector Animator2D 的集视图化（现仅右键跳工作台）。
- 文档回写（T6 归口）：05 §7 工作台、06 §2 .ani schema、03 §8.1 按名解析、
  04 Lemon.Anim 字符串重载语义。
