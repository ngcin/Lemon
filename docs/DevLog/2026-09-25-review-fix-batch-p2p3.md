# 2026-09-25 · 审查修复批二：P2×7 + P3×4（回归 14/14，Debug 23267 checks）

来源：[全栈审查](../Reports/2026-09-24-code-review-546a755.md)同源的六路模块深读
剩余属实项（前批 P0×1 + P1×11 见
[2026-09-24 修复批一](./2026-09-24-engine-editor-review-fix-batch.md)）。本批全部为
**代码修复 + 回归验证**，无设计变更。

## P2（正确性）

- **粒子 blend/filter 入口钳制**（`Particles.cpp Emit`）：EmitterConfig 为数据驱动
  uint8_t，越界值原样拷入 ParticleData → 与 `RenderableDesc` 当初同一越界面
  （`SpriteBatcher` 只有 4 管线/2 采样器槽）。按 `RenderableManager::Create` 同款
  钳制（`&3/&1` + 告警一次）。
- **旋转精灵剔除半径**（`Renderable.cpp Extract`）：`max(radX, radY)` 只对轴对齐
  成立，旋转态角点最远到半对角线 → 屏幕边缘旋转中的精灵整帧消失。带旋转时改
  半对角线保守界（轴对齐保持紧界，不多剔不误剔）。
- **`lemon_play_reset` 误清静态订阅**（`Events.cs`/`Behaviours.cs`/`Exports.cs`）：
  原调 `Events.Reset()` 连 Configure 期静态订阅一并清——`ClearInstances` 特意保留
  类型注册表免重 Configure，静态订阅同理应跨局存活（Stop→Play 后静态链全哑）。
  拆 `Events.PlayReset()`（只清待发/计数）；实例级订阅改由 `ClearInstances` 逐实例
  `ClearSubscriptions` 退订（M15 助手路径）。script-tests 补跨局存活断言
  （Configure 期 Hit→Custom42 订阅在 reset 后仍回执 ×1）。
- **批量 systemIndex 错位**（`ScriptHost.cpp PullBatchRegistry`）：无效查询
  （compCount 越界/未知组件 id）原**静默跳过** → `batch_` 与 C# 注册表错位，
  `fr.systemIndex`（语义 = C# 注册序，`Batch.Tick` 按 `Get(idx)` 解析）指错系统——
  错系统的 ForEach 拿对方查询的指针数组按自己类型解释 = 野读写。改为**占位不跳过**
  （compCount=0 占位保 1:1 对齐 + 红字告警，TickBatch 跳过零参占位）。
- **`BehaviourTypeNames` -1 无解**（`ScriptHost.cpp`）：cap 不足时托管侧返回 -1，
  原固定 4KB 栈缓冲每次重试同容量 = 永远失败、脚本类型表永远空（脚本无法挂载）。
  改堆缓冲倍增重试（4KB 起、1MB 上限 ≈ 3 万+ 类型）。
- **Inspector EndCombo 配对**（`InspectorPanel.cpp DrawEnumControl`）：枚举下拉
  Selectable 命中后 early-return 跳过 `EndCombo()` = ImGui ID/弹出栈错乱。改为
  写入标记 + 恒达 EndCombo（其余四处 combo 本就正确）。
- **Degree 字段拖拽不入 Undo**（`InspectorPanel.cpp DrawField`）：DragFloat 后的
  `TextDisabled("deg")` 后缀把"最后控件"换成 Text（恒不 active）→ 属性轨全程失明
  （空闲快照持续刷新、松手不提交）。DragFloat 后就地捕获
  `IsItemActive/IsItemDeactivated` 汇入 scrub 通道（g_scrub* 兼作捕获通道）。
- **Play 态创建/删除落错世界**（`EditorContext.cpp`）：`CreateEntity`/
  `CreateSpriteEntity*`/`DuplicateEntity`/`DestroyEntityTree` 恒走 `scene_`（edit
  世界）——Play 态创建不可见且 Stop 后残留；Delete 拿 play 句柄戳 edit 世界
  （no-op 或 id 撞车错删）。全家改 `ActiveScene()`（Play 落 play 世界、随 Stop 丢弃，
  ADR-011 横幅口径；播种/测试路径均非 Play 态 = 行为不变）。

## P3（体验/边界）

- **FocusSelection zoom 量纲**（`ViewportPanels.cpp`）：zoom 语义 = 相对 720 参考
  高的缩小率（`halfHeight=360/zoom`），原公式用真实 RT 像素算"像素/单位"存入
  zoom = 偏大 rtH/720 倍（视口越高聚焦过度放大越狠）。改按参考高量纲
  （`0.6·min(720/h, 720·aspect/w)`）。
- **quit-confirm 后 `exitRequested_` 残留**（`EditorApp.cpp` 退出裁决）：确认框已开
  （armed）期间再点关闭 = 残留真值；用户以非退出分支收场（SceneOp 保存/丢弃后
  dirty 即清）→ 下一帧走"干净直接退"，用户只是开了个场景编辑器却无提示退出。
  消费点吸收重复请求（armed 时清零）。
- **窗口失焦卡键**（`Window.cpp`）：失焦/最小化瞬间按住的键永远等不到 KEY_UP
  （事件去了新焦点应用）→ Play 态角色持续单向移动。`FOCUS_LOST/MINIMIZED` 时
  全清键状态表。
- **Gizmo 父子世界→本地换算**（`ViewportPanels.cpp`）：Move/Rotate/Scale 三分支原
  把世界增量/目标直写本地字段（Rotate/Scale 还拿本地 pos 与世界 pivot_ 混算）——
  父链带旋转/缩放时子实体拖拽方向、步长全错（Resize 分支早已有 selPw_ 逆换算）。
  新增 `WorldOfLocal/LocalOfWorld/ParentWorldOf` 三助手，三分支统一"世界系算目标
  →逆父链落本地"；根实体路径（单位变换）与旧代码逐位一致（smoke-drag 14/14 证）。
  Δrot/世界缩放因子经 TRS 乘性传递，本地直接加/乘即正确。

## 验收

- Release（mac）：ctest 3/3；engine-tests **23267 checks**；script-tests
  **1554 checks**（含新增静态订阅跨局断言）；`editor-regression.sh full`
  **14/14**（smoke-drag 全过 = Gizmo 根实体路径零漂移）。
- Debug（mac-debug）：ctest 3/3，engine-tests 23267 checks（EnTT 内部断言全开）。

## 语义注记

- `lemon_play_reset` 后静态订阅存活是**行为变更**（原被误清）：Configure 期注册的
  域级订阅现跨局存活，换域（LoadScript/热重载）仍全清——与 Unity "Enter Play
  不重载域" 语义一致。
- Play 态创建/复制/删除现落 play 世界（原落 edit 世界）：所见即所得 + 随 Stop
  丢弃；Undo 在 Play 本就禁用，无栈污染。
