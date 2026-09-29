# EditorApp 批③c-6：bench + final 族外迁（Run 三挂点收口）

- 日期：2026-09-30（批③c-5 同日续）
- 性质：Run 正文机械外迁收官批——③c 最后两族（bench 指标 + final 终验），
  此后 Run 主体 = 主循环骨架 + 各族 seed/per-frame/verdict 挂点 + 跨族共享
  段界/捕获（批③计划的"Run 三挂点收口"形态达成）。

## 改动

- EditorApp.cpp：1596 → 1237 行（累计 7212 → 1237，**-82.8%**）。
- 新 TU `Editor/App/EditorAppBench.cpp`（354 行）：
  - `BenchState g_bench`（匿名 ns：帧段累计 18 字段 + 停跑证据 7 字段；
    kBenchWarmup/kSegN 随迁 TU 级 constexpr）；
  - 三函数：`BenchSample`（**十参数直传段界 time_point**——段界是 Run 帧局部、
    每帧 epoch 重置；结构体化会跨帧残留旧戳、改变 continue 帧的累计语义，
    故签名传参、段界戳赋值语句零改动）、`BenchCaptureStop`（守卫随体，
    返回 playAliveAtStop 等价增量：benchScene=AliveCount 其余 0）、
    `BenchVerdict`（survivor `exitCode=1` 改 `ok = pass` 聚合返回——两模式
    互斥；scene 恒 0 只报数）。
- 新 TU `Editor/App/EditorAppFinal.cpp`（201 行）：
  - 五函数：`FinalSeedProject`（向导块，#ifndef 分支 return false ✓）、
    `FinalSeedScene`（else-if 链守卫留原位）、`FinalFrame`（frame 20 热重载
    播种）、`FinalSample`（minFps 统计）、`FinalVerdict`（finalOk 聚合返回）。
  - **play 往返四量**（playAliveAtStop/playEnterMs/playExitMs/playVerified）
    是 Run 跨族共享态（play Stop 块写、bench/final/smoke 三族读）——以参数/
    返回值过桥，不入任何族状态。
- EditorApp.h：+26 行（八声明；`<chrono>` 已在）；CMake：+2 源。
- 留驻：playPaced/presentMode/watchdog 等共享条件、--play Stop 块、
  editor-smoke 自检与 overlay 断言、smoke-close 裁决。

## 缺陷与拦截

- 拼接首轮编译器拦下三类：①S6 体漏 `launch`→`Launch()` 变换（fx 饱和
  守卫）；②`BenchClock` 是 Run 内 using——新 TU 匿名 ns 补同款 using；
  ③**五处函数体切片 off-by-one**（把块闭 `}` / exitCode 行含进切片又手工
  补 → 重复括号/exitCode 泄漏）——全部编译期显形，修切片后一次过。
- 修脚本的字符串替换**误伤断言行**（替换目标子串在断言里也出现，把
  `A(464,...)` 拆成两行）——TypeError 拦截，手工还原。教训：对脚本自身做
  sed 式修补时，替换锚必须含上下文行。
- 复核脚本 `body()` 提取器踩多行签名坑（十参数签名第二行无 `{`，depth
  断在参数区）+ 开 `{` 行重复 append——修正后八函数逐行等价全过。
  纯复核侧口径错误若干（挂点序把"循环内挂点"写成循环前、注释行偏移、
  0/1-based 换算），代码侧零真缺陷——本批拼接缺陷全部被编译器拦截在
  构建期，未产出过错误二进制。

## 验证

- 构建：修切片轮后零错误。
- 回归 full：**16/16 终树一次过**（跑前 pgrep 确认无残留实例——③c-5 的
  泄漏进程教训已内化为流程动作）。
- 程序化 review：八函数对 HEAD 逐行等价重建（55/33/84/51/17/3/17/63 行）、
  BenchState 字段集与原两组局部一致、两 TU 无裸 launch/exitCode/return 1
  残留、挂点序 fseed@262 fscene@308(else-if 内) < while@438 < fframe@537
  fsample@795 benchS@808 benchCap@817 benchV@834 < fverdict@944。

## 批③c 收口状态

Run（1237 行）现含：匿名 ns 通用 helpers、主循环骨架（输入路由/Play 步进/
段界戳/渲染提交/捕获）、各族 22 个挂点调用、--play 往返与 editor-smoke
自检/overlay 断言、UI bridge 尾部（FeedGameUiInput 等，批④迁）。重构前
7212 行 → 1237 行（-82.8%）；smoke/bench/final 全族状态机收敛进 6 个
专题 TU（Smoke/Tpl/Uirml/Bench/Final + 批②三 TU）。

## 后续

- 批④：GameUiBridge（LoadUiDocument/FeedGameUiInput/Resolve*/LoadProjectFonts/
  MountSceneUiDocuments/ReconcileUiDocuments）/ ScriptReloadPipeline。
- 低优：per-TU include 瘦身；editor-regression.sh 前置残留实例守卫。
