# 2026-09-24 Engine-Review Medium 批B：ScriptHost 窗口与结构命令（M10-M13）

[Medium 排查轮](./2026-09-24-engine-review-medium-batch-a.md)第三批，本批含**唯一的
行为语义变更**（M11：Awake/OnDestroy 内 native 调用从静默空转 → 真实生效），验证面
拉满（金回放三档 + regression full）。

- **M11 native 窗口五处统一**：新增 `NativeApiWindow` RAII 守卫（保存/恢复前值，嵌套
  安全），五处统一——TickBatch/DispatchEvents 原两处手工置位改写 +
  `AttachBehaviour`/结构命令 Destroy（OnDestroy）/AttachScript（Awake/OnEnable）三处
  补设。`AttachBehaviour` 签名补 `World&`（Awake 内 Ui/Time/Save 需 g_world；调用方
  编辑器×2/样例×2/测试×3 全部手里有 World）。
- **M10 behaviours 列表契约加固**：`char buf[4096]` 零初始化 + 返回值 ≤0 早退，兜住
  托管侧失败/短写走未初始化栈内存。**插曲（回归网价值实证）**：首版误把返回值当
  **字节数**做强制封口（实际是**类型数**，托管侧自写 `'\0'`，Exports.cs:196）——
  `buf[8]='\0'` 把类型表截断成首个残名，SpawnerBehaviour 解析不到 → 编辑器冒烟
  script-spawn 零刷怪 FAIL，`editor-regression full` 当场抓住（11/13 → 修后 13/13）。
  契约细节以托管侧实现为准，评审建议里的"强制 buf[n]=0"本身就是错的——留档提醒。
- **M12 结构命令 get-or-create**：case 2（AddComponent）补 `hasFn` 前置（对已有组件
  再 emplace = entt 池损坏，与 NativeWrite/AttachBehaviour 同口径）；case 4
  （AttachScript）改调 `AttachBehaviour` 复用（原无条件 `Emplace<ScriptBox>`，对快照
  已带 ScriptBox 的实体 = 池损坏）。
- **M13 countFn 缺失防御**：批量系统驱动组件缺 `countFn` 钩子 = 整系统跳过 + WARN
  一次（告警只响一次标志位）。不预留就 gather = 块缓冲 realloc 期先前系统
  `fr.blocks` 悬垂 → C# 线性步进野读；宁可响亮地不跑，不静默降级。当前全部引擎
  组件都供钩子，属"未来注册缺钩子"的防御。

**回归**：script-tests **1492 checks OK**（+`TestAwakeWindowAndDoubleAdd`：Awake
Ui.Set 即时可见 / 双 AddComponent no-op / AttachScript 覆写 / OnDestroy Ui.Set 生效；
既有 behaviours 计数 7→8 随 typeId 7 注册对齐）；engine-tests 13186 不变；**金回放
m5b2 三档 PASS mismatches=0**（sim mt/st + script 双档——M11 行为变更零重录证明成立：
回放路径 attach 不调改态 native API，与预判一致）；`editor-regression.sh full`
**13/13**。

**口径坑复记**（22e0588 已记过、本轮又踩）：bench-script 金档是 **1800 帧**流（sim
是 3600），`--frames` 必须对齐金档帧数——跑 3600 会报 mismatches=1800 的假 FAIL
（HEAD 同样报，先 A/B 排除自身回归再查口径）。

剩余：批C（M14/M15 C# 域生命周期与事件）。
