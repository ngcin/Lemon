# 2026-10-16 M8 批⑫ —— 帧事件 UI（.clip events[] 编辑面）

## 输入

动画创作面插队系列第四批（⑨⑩⑪ 三批补齐 controller 编辑面后的最后一块
"运行时有、编辑器没有"）：`.clip events[]` 自 M6a T3d 批③ 起运行时闭环
（解析→PlayCaches→`GameEvent::AnimFrame` 派发→C# Subscribe，svr-test
DuelBehaviour 攻击判定为真实消费者），但 AnimationPanel 对 `edit_.events`
零读零写零展示——作者面 = 手写 JSON（AnimAsset.h 自注），全仓仅 Monster01/02
Attack.clip 两个手写 events 资产。用户拍板 UI 形态（三选一）= **角标 + chips 行**。
[批文件](../Plans/M8/2026-10-16-b12-frame-events-ui.md)

## 交付

- **Engine（纯函数，运行时零改动）**：`RemapClipEvents(events, map)`（
  `AnimAsset.h`，SanitizeClipEvents 同居）——帧序变更 → 事件帧重映射单源化，
  落尾按 (frame,id) 稳定重排保档面升序。**随批修背景缺陷**：事件绑定帧下标，
  而插帧/复制/删帧/Ctrl+←→换位/拖拽重排五路径现状全不动 events（仅靠保存时
  Sanitize 清越界兜底）——删帧 3 后原帧 6 的事件仍挂下标 6，UI 上桌即用户可见
  语义漂移。语义表：插帧后移/删帧随删前缩/换位互换/拖拽段旋转/复制不带事件/
  整表替换（sheet picker「替换为」）保守清越界 + 黄字。
- **Editor：AnimationPanel**
  - 胶片带角标：事件帧右上旗点（多事件细环；播放游标扫过提亮 = 命中反馈）+
    tooltip 列该帧打点；
  - 预览条事件 chips 行：全部打点一览（`帧·#id`，流式折行 + 32 截断 + tooltip
    余量）；点 chip 跳帧选中（选中即预览跟随，ro 可用）、× 删除、右键编辑、
    「＋ 打点」在选中帧新建；
  - 右键帧格：「添加帧事件…」/「编辑帧事件…」（该帧首个；多点编辑走 chips
    右键）；
  - 编辑模态（边沿触发同 wiz/pick 族）：帧号 DragInt（0 起）+ id DragInt
    **钳 0–65535**（运行时 ClipEventDef u16 收窄——防静默截断）+ 删除钮 +
    同帧同 id 完全重复拒（运行时会双发）；CommitEvent 按 (frame,id) 升序落位
    （stable_sort 对已序表零重排 = no-edit 往返逐字节不变）；
  - TrySave roundtrip 预验补 `events` 等值（原只比 name/frames）；
  - ro/Play 态：chips 查看/跳帧可用，编辑入口禁用；HandleKeys 弹窗守卫表 +
    模态开态；ResetEditingState 清模态残留。
- **i18n**：双语 +17 keys（anim.event.*，185→202 对齐）；
  `anim.err.events_dropped_fmt` 文案随语义更新（"删帧所致"→"帧表变更所致"
  ——该 key 现仅服务整表替换路径）。
- **测试**：EditorTests 新 `TestClipEventRemap`（+11：插入/单删/多删集/相邻
  换位/拖拽旋转双向/空表全删/Monster01 同型 golden 字面 + roundtrip/id=0
  省略回读）；`--smoke-anim` f39 链（f38 any 与 f40 间空位，帧预算 134 不动）
  五断言位 `events(add/dup/rt/sync/del)`——全走 UI 同款提交路径，**纯内存 +
  全量还原**（不动种子档盘面，b11 教训）；ForTest 钩子六个（EventCount/
  EventFrameAt/EventIdAt/Add/Delete/InsertFrameHead/DeleteFrameAt）。

## 验证

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记，HEAD 同场复现）；
  单测 **34,982 checks OK**（+11）；ctest 4/4。
- `--smoke-anim` zh+en：`events(add=YES dup=YES rt=YES sync=YES del=YES)` 全位
  YES（隔离双绿）；首跑红位 = pick/sheet/zoom 注入抖动族既有登记——新 events
  位在失败轮中亦全 YES，正交性直接可证。
- 回归 full **26 步全有绿记录**：24 直绿 + smoke-drag（rot 精度位，隔离三连绿）
  + anim-en（隔离双绿）按 09 §9 抖动口径归因；bench-survivor 门禁 fps=77 ≥ 76.5。
- **金回放三档 mismatches=0 零重录**：sim-st/sim-mt（m8b5 档）+ script（m8b1 档）
  原样 `--replay`——Engine 仅新增纯函数模板（无运行时调用点改动），理论口径
  实跑坐实。

## 裁决与登记

- smoke rt 位实做 = **档面升序不变式**（乱序提交 → [(0,2),(1,1)]），盘上 events
  roundtrip 由单测 golden 钉——冒烟不重写共享种子档（b11 player.ani 教训）。
- sync 位 = 头插帧后事件整体后移 + 头删帧复位（插删重映射核心，帧表/事件双双
  回到基线 = 后续帧链零污染）。
- 命名事件/payload 维持 v1.1 登记；per-frame 帧时长维持 v1.1。
- AnimationPanel 本地撤销栈（帧 + 事件操作统一）归批⑩"面板本地撤销与场景结构
  轨 Undo 互通"既有登记，不本批。
- 样本 + 手册端到端 + vs-survivor 去 GUID 化 + 真人组合验收 → 批⑬。
