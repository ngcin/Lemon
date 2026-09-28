# A 档运行时补间 Lemon.Tween（用户插入项，批② 外挂）

2026-09-28 · 批② 动画生产线之外的用户插入项。背景：三档评估讨论（当日会话）——
关键帧属性动画（B 档，编辑器 dope sheet）与运行时补间 API（A 档）拆开决策，
用户拍板「实现 A 档即可，编辑器之类的不列入计划」。本批 = 纯运行时 tween：
**无新资产格式、无编辑器面、无新 ECS 组件**。

## 决策

1. **落点 = World 持有 `TweenTable` + 管线尾插 `TweenSystem`**（03 §13 纪律：
   通道族新增机制一律「World 持有 + vtable 尾追」，不动组件注册表——零布局
   冻结扰动、EnterPlay 新建 World 自清零）。ClipTable/FxChannel 同款口径，
   **不入 StateHash**：补间是指令不是模拟态，效果经组件字段（pos/scale/
   colorRGBA 本就入哈希）可验漂移；基准场零调用 = 金回放零漂移（结构性保证：
   空表系统早退）。
2. **求值序：插 AnimGraph 后、事件派发前**（注册序第 19 位，AnimGraph 同款
   尾插——其后系统（事件派发/销毁提交）不消费 RNG，子流 id 语义零影响）。
   语义：CSharpBatch 当 tick 发起的 tween **本 tick 即首写**（脚本 Update 里
   `Tween.Scale(...)` 立即见效）；完成事件当帧派发（无 1-tick 迟滞）。
3. **字段所有权：存活 tween 拥有字段**。同帧脚本后写被补间覆写（后写者赢的
   反向——tween 在脚本之后跑）；同实体同字段新建 tween = 顶替旧 tween
   （DOTween 同款防打架）；Kill/Once 完成后归还脚本。与 Animator「脚本可
   覆写」相反但同族自洽：Animator 是持久档面数据（脚本终审），tween 是瞬时
   指令（最近指令终审，用户能立刻看见打架而非静默失效）。
4. **字段寻址 = FieldMeta 按名解析**（Inspector/序列化同一元数据，零新反射）。
   建 tween 时一次解析存 offset+kind，逐 tick 经 `getFn` 可写取址直写。
   类型白名单：Float（rot 等）/ Vec2（pos/scale）/ UInt32（colorRGBA，按
   四字节颜色通道 0..255 插值——现目录唯一值得补间的 UInt32；spriteId 字节
   插值无意义，作者自慎）。其余类型（Int16/EntityRef/Blob…）拒建（返 0）。
5. **模式与缓动**：Mode = Once / Yoyo（三角波永续，idle 呼吸/悬浮用，Kill 停）；
   Ease 9 种纯函数（Linear/In·Out·InOutQuad/Out·InOutCubic/OutBack/
   OutElastic/OutBounce）。推进 = elapsed 累计 + 纯函数采样（Animator time
   同款，回放确定）；dt 已缩放 → timeScale=0 冻结；duration≤0 = 立即完成。
6. **完成通知双通道**：GameEvent 表尾追加 `TweenFinished`（src=实体，
   userArg=tween 句柄；AnimFinished 同款 Events.Subscribe 消费）+ C#
   `Tween.Alive(handle)` 轮询（事件回调里接力不方便时用）。
7. **ABI 表尾追加 4 桥**：`tweenTo(entity, compId, field, float* to4,
   duration, ease, mode) → 句柄（0=失败：实体亡/组件缺/字段名未命中/类型
   不可插值）`、`tweenKill(entity, compId, field) → 移除数`、
   `tweenKillEntity(entity) → 移除数`、`tweenAlive(handle) → 0/1`。
   旧宿主未注册 = SDK 判空降级（no-op / 句柄 0）。
8. **句柄 = 单调 u64**（不回收复用，Alive 线性查——低频轮询 + 条目量 ≤ 百级，
   不做代际槽）。实体销毁 → 条目当 tick 自清（Advance 里校验存活）。

## 改动

- `Engine/ECS/TweenTable.h/.cpp`（新）：TweenEntry/缓动纯函数/Create（FieldMeta
  解析 + from 现值捕获 + 顶替）/KillField/KillEntity/Alive/Advance（推进 +
  TweenFinished 入队 + 死条目交换删除）。
- `Engine/ECS/World.h`：`tweens_` 成员 + `Tweens()` 访问（注释同 Fx 口径）。
- `Engine/Systems/Systems.h/.cpp`：`TweenSystem`（空表早退）+ 安装于
  AnimGraph 与 ScriptEventDispatch 之间。
- `Engine/ECS/Events.h` + `Lemon.SDK/Interop/Components.cs`：GameEvent 尾加
  `TweenFinished`（Custom 之后追加以保用户区起点值——AnimFrame 先例）。
- `Engine/Scripting/ScriptHost.h/.cpp`：NativeApiVtable 尾加 4 项 + 实现
  （g_world/g_scene 空守卫）。
- `Engine/CMakeLists.txt`：源清单加 TweenTable.cpp。
- `Lemon.SDK/NativeApi.cs`：4 指针 + Native 助手（CopyUtf8 同款）。
- `Lemon.SDK/Tween.cs`（新）：`Lemon.Tween` 静态类——通用 `To<T>(g, field,
  float/Vec2/uint, dur, ease, mode)` + 常用糖 Position/Scale(+uniform)/
  Rotation/Color/Alpha（读现色保 RGB 换 A）+ Alive/Kill/KillAll。
- `TestScript/TestScript.cs`：`TweenProbeBehaviour`（typeId 15 表尾）——五相位
  状态机（Once 线性精确值 / Yoyo 折返 / 颜色字节插值 / Kill 冻结 / 字段所有
  权：脚本每帧写 pos=99 被 tween 覆写）。
- `tests/script/main.cpp`：`TestTweenSdk()`（管线含 TweenSystem；dt=0.25 断言
  精确中值/终值/完成事件恰一次/Alive 边沿/冻结语义）。
- `tests/engine_tests.cpp`：`TestTweenTable()`（C++ 单元：建项校验拒建路径/
  缓动精确值/Once 完成恰一事件/Yoyo 折返值/销毁实体自清/同字段顶替）+
  `TestSystemPipelineOrder` 18→19（"Tween" 插 AnimGraph 与
  ScriptEventDispatch 之间——插位回归锁）。
- 文档：03 §8.3 属性补间短节 + 04 native 表注记 + AGENTS.md 登记句。

## 验证

- 阴性验证：test 管线不装 TweenSystem（引擎其余全在）跑 script-tests →
  断言红（tween 未推进、scale 停在脚本直写值）→ 装回 → 绿。
- ctest 3/3（engine-tests/script-tests/imgui-isolation）+ 回归 full 14/14。
- 基准场零调用路径：金回放/终验随回归套覆盖（结构性早退）。

## 遗留

- 真人验收：模板里加一次受击闪白（Color tween）+ 拾取缩放弹跳（Scale
  OutBack）的手感体验——代码面无阻塞。
- B 档（关键帧属性动画 + dope sheet 编辑器）明确不立项（用户 2026-09-28
  拍板）；本批的缓动纯函数与字段寻址是将来 B 档可直接复用的地皮。
