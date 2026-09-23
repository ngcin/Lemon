# 2026-09-21 smoke-ui 真人链路 21/21 全绿：ImGui DnD 投递源码级定位 + 驱动四修

`--smoke-ui`（注入式真人会话回归：真实 ImGui 事件管线驱动的点击/拖拽/键盘/打字，
11 段 21 项断言）全段 OK，无调试环境 ×2 连跑稳定；`tools/editor-regression.sh`
**11/11**（新增 smoke-ui 步）；终帧截图目检：Console Collapse 已被点击切换、布局
回默认、Console 日志完整走线（导入→重扫→保存×2→Play 字节一致→布局保存→重建）。

## 本轮定位要点（ImGui 1.92.9b 源码级，后续注入式测试的硬知识）

- **DnD 投递条件**：`Delivery = was_accepted_previously && !IsMouseDown`，其中
  `was_accepted_previously = (上一帧 AcceptIdCurr == 本帧 TargetId)`——目标必须
  **连续两帧**被 hover（注册），松开帧才投递；且 `EndFrame` 的 `is_elapsed` 规则
  使松开帧是**唯一机会**（未投递即 ClearDragDrop）。重叠目标按**最小 rect 面积**
  取胜；`BeginDragDropTarget` 还要求非活动源行（`SourceId==id` 拒绝）。
- **H 段（行拖拽挂父子）三连根因**：①松手坐标用的是 f93 拖拽启动前的行位置——
  布局已变，实际松在**源行 Gate 上**（被 SourceId 检查拒绝，永不投递）→ 修：松手
  帧重读登记点现位。②行位移元凶：G 段对 Gate 的选择点击与 H 段按下仅隔十几帧，
  注入帧距 < ImGui 双击时限 → 判成**双击**弹出重命名行（+24px），全体行下移——
  按下点 x+24 避开（真人手速不会触发）。③dock 标签物理点击在注入管线里不稳 →
  改用 `ImGui::FocusWindow`（对 dock 窗口 = 原生选中其标签，同真人点标签语义）。
- **I 段（目录进出）**：非活动 dock 标签的面板体**不绘制**（SkipItems），登记点
  不存在——交互前必须先 FocusWindow 把 Assets 切成活动标签。
- **K 段（Console）**：驱动 else-if 链上诊断块占用 f150，把 K 的裁决分支截胡
  （f150 永不执行、consoleOk 恒 0）——裁决挪 f151；并收紧判据为「Collapse 开关
  被找到并点到」= 标签已切 + 面板体已绘制 + 控件可点，三者缺一即 FAIL。

## 结论

本轮 **0 个新产品 bug**（6 个真 bug 均在前几轮抓住并修复，见 2026-09-21 M4.7d 条）；
4 处全部是驱动侧坐标/时序/帧号问题。`LEMON_SMOKE_UI_DEBUG=1` 可出拖拽期行级
[dnd] 逐帧 dump 与 Console/布局登记表态，后续注入式调试直接复用。
