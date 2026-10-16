# M8 批⑫ —— 帧事件 UI（.clip events[] 编辑面）

Status: done（2026-10-16 当日机器面全过——构建零编译警告（唯一 ld duplicate-library
= b1 已登记项）/单测 **34,982**（+11）/ctest 4/4/smoke-anim zh+en `events(add=YES
dup=YES rt=YES sync=YES del=YES)` 全位 YES（隔离双绿；首跑红位 = pick/sheet 注入
抖动族既有登记，与本批正交）/回归 full **26 步全有绿记录**（24 直绿 + smoke-drag
隔离三连绿 + anim-en 隔离双绿归因注入抖动，09 §9 口径；bench-survivor 门禁
fps=77 ≥ 76.5）/金回放三档 **mismatches=0 零重录**（sim-st/mt m8b5 档 + script
m8b1 档原样放——Engine 仅新增纯函数，运行时零改动坐实）；真人走查归批⑭ 组合
验收（2026-10-17 批⑬ 插队顺延）。原立项：2026-10-16 用户拍板（插队系列"动画创作面打磨"第四批；UI 形态用户
选定"角标 + chips 行"；编号沿革 = b9 立项⑩ → b10 插队顺延⑪ → b11 插队顺延⑫，
[M8.md §3](./M8.md)）

## 1. 输入与定形（用户 2026-10-16 拍板）

- **动因**：运行时链路自 M6a T3d 批③ 起闭环（解析→PlayCaches→AnimFrame 派发→C#
  Subscribe；svr-test DuelBehaviour 攻击判定为真实消费者），但 AnimationPanel 对
  `edit_.events` **零读零写零展示**（AnimAsset.h 自注"作者面 = 手写/表驱动"）——
  全仓仅 Monster01/02 Attack.clip 两个手写 events 资产。controller 编辑面已被
  ⑨⑩⑪ 补齐，帧事件是创作面最后一块"运行时有、编辑器没有"。
- **形态（用户三选一拍板）**：胶片带角标 + 预览条事件 chips 行 + 右键帧格入口 +
  轻量编辑模态（帧号 + id 两字段）。导航与编辑兼顾，符合面板"选中驱动"惯例；
  纯右键形态（总览弱）与固定底表（b9 三表被否先例）均不取。
- **背景缺陷随批修（事件帧漂移）**：事件绑定帧**下标**，而插帧/复制/删帧/
  Ctrl+←→换位/拖拽重排五个路径现状均不动 events——删帧 3 后原帧 6 的事件仍在
  下标 6（视觉上已换了内容），仅靠保存时 SanitizeClipEvents 清越界兜底。UI 上桌
  后即用户可见语义漂移，必须随批修（RemapClipEvents，见 §3-A）。
- **id 域守卫**：编辑侧 `ClipEventEdit.id` 为 u32，运行时 `ClipEventDef.id` u16
  收窄——id > 65535 静默截断是数据损坏面。UI 输入域钳 [0,65535]。

## 2. 范围

### In

| # | 面 | 内容 |
|---|---|---|
| 1 | Engine 纯函数 | `RemapClipEvents(events, map)`：帧序变更→事件帧重映射单源化（SanitizeClipEvents 同居）；运行时零改动 |
| 2 | 编辑器·标记 | 胶片带事件帧角标（旗点 + 多事件计数；播放游标扫过提亮 = 命中反馈；tooltip） |
| 3 | 编辑器·chips | 预览条信息组下加事件行：全部事件 chips（`帧·#id ×`，手动折行 + 窄窗省略）、点 chip 跳选帧、× 删除、＋ 在选中帧打点、选中帧 chip 高亮 |
| 4 | 编辑器·入口 | 右键帧格 +「添加帧事件…」（该帧有事件再加「编辑…」）；编辑模态（帧号 DragInt 0..n-1 / id InputInt [0,65535] / 删除钮（编辑态）/ 确定/取消）；同帧同 id 完全重复拒 + 提示；按 (frame,id) 升序插入 |
| 5 | 重映射接入 | 五路径（Insert/Duplicate/Delete/换位/拖拽重排）走 Remap；sheet picker「替换为」= Sanitize + 黄字告警（保守：只清必然越界，等长换皮表保事件）；删帧旧 Sanitize 提示退役为 TrySave 兜底 |
| 6 | 保存门禁 | TrySave roundtrip 预验补 `events` 等值比较（原只比 name/frames） |
| 7 | ro/Play 态 | chips 查看 + 跳帧可用，编辑入口 BeginDisabled；HandleKeys 弹窗守卫表加模态开态 |
| 8 | ForTest 钩子 | EditEventsForTest / AddEventForTest（重复拒回执）/ DeleteEventForTest |
| 9 | 冒烟/单测 | f39 events 链（add/dup/sync/del/rt 五位）+ verdict `events(...)`；单测 = Remap 族 + TrySave events 等值 + golden 不动点 |
| 10 | 文档 | 本文件 + 05-Editor §7 落地口径（M6a 批② 裁剪清单移出"帧事件打点" + 顺手修旧路径 ClipEdit→AnimAsset）+ Manual 14 作者面小节 + DevLog + M8.md + WIP.md |

### Out（登记不动）

命名事件/payload（v1.1）；per-frame 帧时长（v1.1）；撤销栈（AnimationPanel 全面板
本无本地撤销——归 b10"面板本地撤销与场景轨 Undo 互通"既有登记）；手册完整端到端
样本 + vs-survivor 去 GUID 化（归批⑭，原⑬ 顺延）；解析侧 schema 不动（id 上限守卫在编辑面，
旧档宽容度不变——金回放零重录前提）。

## 3. 设计要点

### A. RemapClipEvents（帧序变更语义表）

| 变更 | 事件行为 |
|---|---|
| 插入帧 i | f ≥ i 后移 +1 |
| 复制帧（Ctrl+D） | 事件留原帧，不随复制 |
| 删除帧集合 D（多选） | f ∈ D 随删；f > 各删除点按删除量前缩 |
| 相邻换位（Ctrl+←/→） | 两帧事件集合互换 |
| 追加帧 | no-op（新帧在尾部） |
| 整表替换（sheet picker「替换为」） | SanitizeClipEvents + 黄字告警（保守） |

实现 = 单个纯函数 `RemapClipEvents(std::vector<ClipEventEdit>&, F&& map)`（map:
old → int64，负值 = 随帧删除），各路径传小 lambda；升序/去重不强制（档面约定由 UI
插入侧维持）。

### B. 序列化与档面

- no-edit 往返逐字节不变（events 仅非空追加——既有行为，Monster01/02 单测钉）。
- UI 新增/编辑按 (frame,id) 升序插入，维持"按帧升序"档面约定（运行时线性扫
  不依赖序，约定仅为档面整洁）。
- 同帧同 id 完全重复：添加/编辑入口拒 + 提示（运行时会双发——编辑面堵）。

### C. smoke f39 链（帧预算 134 不动，f39 = f38 any 链后空位）

API 级无头链（面板目标 = Seed 尾置的 anim-edit.clip，2 帧 0 事件——f30 探针同源）：
AddEvent(1,1) → 重复 AddEvent(1,1) 拒 → AddEvent(0,2) 乱序提交 → 断言档面升序
[(0,2),(1,1)]（**rt 位 = 升序不变式**——盘上 events roundtrip 由单测 golden 钉，
冒烟不重写共享种子档，b11 教训）→ 头插帧（事件整体后移 1→2）+ 头删帧（复位）
= **sync 位（重映射核心）** → DeleteEvent×2 还原基线（后续帧链零污染）。全程
CommitEvent/UI 原语同款路径，纯内存。

## 4. 验证判据（实测落账 2026-10-16）

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记，非本批）；单测
  **34,982**（+11，TestClipEventRemap）；ctest 4/4（script-tests 不变——无 C# 面）。
- `--smoke-anim` zh+en：verdict `events(add/dup/rt/sync/del)` 全 YES，既有位零回归
  （隔离跑双绿；首跑红位 = pick/sheet/zoom 注入抖动族既有登记——新 events 位在
  失败轮中亦全 YES，正交性直接可证）。
- 回归 full 26 步全有绿记录：24 直绿 + smoke-drag（隔离三连绿）/anim-en（隔离
  双绿）按 09 §9 抖动口径归因；bench-survivor 门禁 fps=77 ≥ 76.5。
- 金回放三档 mismatches=0 **零重录**（sim-st/sim-mt m8b5 档 + script m8b1 档原样
  `--replay`）——Engine 仅新增纯函数（RemapClipEvents 模板，无运行时调用点改动）。
- Monster01/02 Attack.clip 形态 no-edit 往返稳定：单测 golden 字面钉（同构缩样
  13 帧 frame=6 id=1 + roundtrip 等值）。

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| chips 折行 en 长文本越窗 | 手动 SameLine 流式 + 窄窗限数省略号 + tooltip（M7c 批⑫ 越窗先例） |
| f39 被既有链占用 | 实施时核对帧阶梯；占用则抬帧预算 134→138 同步回归脚本两处（b10 review-fixes 先例） |
| 模态与 Space 播放冲突 | HandleKeys 守卫表加模态开态（sheet picker 同款） |
| 重映射改语义引发存量档漂移 | 五路径全在编辑器内存态（不落盘）；旧档载入→no-edit 保存逐位不变单测钉 |

## 6. 遗留与登记

- 样本 + 手册端到端 + vs-survivor 去 GUID 化 + 真人组合验收 → 批⑬
  （**2026-10-17 编号顺延 → 批⑭**：批⑬ = 帧事件注册表插队立项，见
  [b13 批文件](./2026-10-17-b13-anim-event-registry.md)）。
- 命名事件（id → 名字映射表）~~v1.1 触发式——消费者（多项目复用打点协议）
  出现即做~~ → **批⑬ 已立项**（2026-10-17 用户拍板提前，触发条件放宽 = 打点
  负担按 id 个数线性涨、svr-test 已有两个手写打点资产）。
- AnimationPanel 本地撤销栈（帧操作 + 事件操作统一）→ 归 b10"面板本地撤销与场景
  结构轨 Undo 互通"登记，不本批。
