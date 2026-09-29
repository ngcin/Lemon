# EditorApp 批③b：g_tpl* 文件全局收敛为 TplSmokeState 单结构体

- 日期：2026-09-29（批③a 同日续）
- 性质：纯改名重构（零行为/零控制流变化）；批③c（smoke-template 采样族整体
  外迁 SmokeHarness）的整装前置——状态先抱团，搬家才是一次搬运。

## 改动

- EditorApp.cpp 匿名命名空间：20 个 `g_tpl*` 文件级标量 → `struct TplSmokeState`
  （35 行，字段名/类型/初始化/行内注释逐项原样）+ 单实例 `g_tplSmoke`。
- 全文 168 处引用正则改名（`g_tplWaveStarts` → `g_tplSmoke.waveStarts`，首字母
  小写；含两处历史注释内的旧名一并归一）。声明块内 36 处随结构体替换消化。

## 评估发现（为什么不是 Run 局部变量）

原计划收进 Run 局部，勘察后否决——两处**静态存储刚需**：
1. smoke-template 事件计数 sink 是**无捕获 lambda**（`SetEventSink` 收函数指针），
   只能改静态存储期的变量；
2. `FeedGameUiInput`（非 Run 成员函数）跨函数读 `pointerHold` 让位窗。
单实例文件级是满足两者的最小形态；③c 时结构体可整体搬进 harness（实例经
指针/成员可达）。

## 验证（首跑抖动，如实记录）

- 构建链接过（改名完备性的编译器背书：漏改即 undeclared）。
- **提交前程序化 review**：当前文件可由 HEAD 经"③a 切分 → ③b 正则改名 →
  结构体替换"精确重建（3869/3869 行零差异）；review 抓出手写结构体一处注释
  错字（站桩→站栈，已修——纯注释零行为，构建复验）。
- 回归首跑 **14/16**：basic smoke 与 final acceptance 两步 FAIL（final 全判据
  一起红 = 运行早期劣化形态，非单点断言）。
- 处置：两步**单独复跑双双全过**（final 全判据 OK：fps=59/_bag 66/hr ≤2000/
  cold 340ms/autosave/play byte-exact）；随后全量复跑 **16/16 全绿**。
  符合"首跑负载抖动、复跑全绿"既有先例（T1：drag/ui 抖动 12/14 复跑绿）。
  改名类零控制流变化与失败形态（跨模式整体劣化）不吻合，归因环境负载。

## 遗留

- 批③c：smoke-template 采样族（Run 内 ~2537–3300 区段）+ smoke-drag/ui/anim
  状态机逐模式外迁 SmokeHarness，Run 收敛为 播种/每帧/末帧裁决 三挂点。
- final* 计量字段（EditorApp.h 成员）与 Run 局部冒烟变量随 ③c 各模式归 harness。
