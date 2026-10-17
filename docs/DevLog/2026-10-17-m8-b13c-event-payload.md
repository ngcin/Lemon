# 2026-10-17 M8 批⑬c — 帧事件数值载荷类型化（机器面收口）

批文件：[Plans/M8/2026-10-17-b13-anim-event-registry.md](../Plans/M8/2026-10-17-b13-anim-event-registry.md) §4 ⑬c。
上一条目：[⑬b 反向 lint](./2026-10-17-m8-b13b-anim-event-lint.md) / [⑮a](./2026-10-17-m8-b15a-terminal-state.md) / [⑮c](./2026-10-17-m8-b15c-isfinished.md)。

## 改动

- **schema**（`Engine/Assets/AnimAsset.h/.cpp`）：`.clip` events 行可选 `"val"` 数值——
  `ClipEventEdit` 增 `val` + `hasVal`（区分"未写"与"写了 0"，旧档 no-edit 往返
  逐字节不变靠它）；解析 val 非数值拒（定版格式纪律）；序列化仅 hasVal 时落盘，
  格式与 fps 同款（整值省小数、非整值一位小数）。
- **运行时**（`ClipTable.h` / `PlayCaches.cpp` / `Systems.cpp`）：
  `ClipEventDef` 增 `float val`；AnimatorSystem 推 AnimFrame 时
  `payload[1] = ev.val`（payload[0] 帧号不动）。**零新事件类型、零 vtable、
  EventPacket 既有字段——运行时结构零改动**。
- **SDK**（`Anim.cs` / `GameObject.cs`）：`OnFrame` 家族增 `Action<GameEventMsg>`
  带载荷重载（实例/静态 × 自身/指定实体共四个新形）；文档钉死 P0 = 帧号、
  P1 = 打点数值载荷（无 val 旧档 = 0）。
- **编辑器**（`AnimationPanel.cpp` / `BuiltInPanels.h`）：打点模态增「附带数值」
  复选 + DragFloat（复选关 = 不落盘，旧语义）；`CommitEvent` 尾参缺省扩展
  （既有调用面零改动）；ForTest 存取器 `EventHasValAtForTest/EventValAtForTest`、
  `AddEventForTest` 带 val 形。i18n 两枚 key 双语同位插入（anim.json 两 locale
  各 229 keys 对齐）。

## 测试

- engine-tests（+13 → **35,037**）：val 解析（小数/整数/无 val hasVal=false）/
  落盘格式（`"val": 12.5` / `"val": 7 }` 整值无小数）/ roundtrip / no-edit
  往返不出现 val / val 非数值拒（TestClipEdit 扩）；clip 表登记携带 val
  （TestClipCacheBadArchive good.clip 扩）；**全管线端到端**
  `TestAnimEventPayload`：帧 1 双打点（id5 val=2.5 / id6 无 val）→ AnimFrame
  payload[1] 对拍（2.5 / 0），payload[0] 帧号不动。
- script-tests（+1 → **1,925**）：AnimEventProbeBehaviour 增 msg 重载订阅，
  夹具 pushFrame 带 payload[1]=2.5 → `animreg-msg == "1|25"`（×10 整数化免
  文化漂移）。
- ctest 4/4；回归 full 26 步（含金回放三档——既有档无 val ⇒ payload[1]=0
  逐位同，零重录论证同批⑫：载荷经 EventPacket 既有字段、值由资产确定派生）。

## 出口判据对照（批文件 §4 ⑬c）

- `float:force` 类打点 → 读取端到端 ✅（TestAnimEventPayload 全管线 + script-tests
  msg 重载读取）；
- 回放零重录 ✅（回归金回放步；既有档 val 缺省 0 = payload[1] 恒 0 逐位同）。

## 余项

- 字符串 payload / 事件分组 → v1.1 触发式（批文件 §7 既登记）。
- svr-test 侧消费者（如 attack-hit 打力值）未迁移——按"后置项随消费者"纪律，
  游戏侧需要时即刻可用（.clip 手写 val 或模态编辑均可）。
