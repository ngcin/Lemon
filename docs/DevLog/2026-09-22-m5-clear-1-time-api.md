# 2026-09-22 M5 清障①：C# Time API（DeltaTime/Elapsed/FrameCount）

M5 玩法脚本（技能冷却/磁吸/导演波次）全依赖 dt——此前 dt 已传到域线程
`TickBody` 但在调度层丢弃。落地方式（比改 `Update` 签名更优：零破坏——blank
模板/手测项目/全部既有脚本不动）：

- **`Lemon.SDK/Time.cs` 新增 `Lemon.Time`**：`DeltaTime`（当前固定步长）/
  `Elapsed`（局累计秒，内部 double 防长局漂移）/ `FrameCount`（局帧号）。
  `TickBody` 首行 `Advance(dt)`——档① Update 与档② ForEach 同帧同值，零跨界
  调用零分配（Elapsed 命名：C# 禁成员与类型同名，`Time.Time` CS0542）。
- **归零语义 = "局"**：换域 `LoadScript` Reset + 新导出 `lemon_time_reset`
  （`ScriptHost::ResetScriptTime` 指针启动期缓存，旧 Entry 兼容 no-op）；
  `EditorContext::EnterPlay` 调——Play↔Edit 不换脚本域，不显式归零则第二局
  帧号延续（script-tests 实测抓到：前序测试 14 步推走 FrameCount，TimeProbe
  的 `FrameCount==1` 永不命中——正是该语义的活证）。
- **测试**：TestScript 表尾注册 `TimeProbeBehaviour`（typeId 3，既有 typeId
  零扰动）：dt=0.25 下三帧回报 Custom 250/500/3（精确二进制值），第 3 帧自毁；
  `lemon_time_reset` 二轮验证（新 World 重见帧 1 回报）；hot-reload 断言
  3→4 类型。sink 的 readBack 判定从区间收窄为 `==201`（250 = dt×1000 会撞
  [200,300) 区间）。

**回归**：script-tests **1281 checks OK**；engine-tests 13010；editor-regression
full **11/11**（smoke-ui/script-chain/final 覆盖 Play 新挂点）；bench-script
PASS：avg 0.235ms（基线 0.248），托管分配总 **0 B**（Advance 零分配），毒脚本
60 帧禁用照常。

**顺手发现不修**：`--smoke-ui` 裸跑（不带 `--project`/`--no-reopen`）会自动
重开用户最近项目（EditorApp.cpp:1530 排除条件只含 smoke/finalTest，漏
smokeUi/smokeDrag）——在用户项目上跑断言链必然 FAIL。回归脚本标准用法已带
防御；M5 冒烟基建改造时一并修（已登记）。
