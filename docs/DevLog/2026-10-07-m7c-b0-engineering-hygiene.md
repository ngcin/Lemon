# M7c 批⓪ 工程卫生四件落地（T1 瘦身 / T4 格式 / T3 缓存 / T2 拆测试）

**日期**：2026-10-07 · **批**：[M7c 批⓪](../Plans/M7c/2026-10-07-b0-engineering-hygiene.md) · **顺序**：T1→T4→T3→T2

## T1 AGENTS.md 瘦身

- 实测行 3 单段 50,199 字节（全文 46 行）。**对账表先落批文件 §T1.1**（12 块信息逐一映射到既有权威位置，结论"无孤儿信息"），随后收缩。
- 出口全过：全文 46→52 行（新增 ccache 配方一行等，远低于 150 门）；新"当前阶段"块 7 行 1,578 字节（<2KB）；**四节操作面 git diff 实证零扰动**（仅删行 3 一行）；待用户三条原样保留（M4.8 走查 / svr-test V1–V3 / W5 真机 GPU）；全链接 10/10 机械解析通过。

## T4 .clang-format 最小集 + .editorconfig

- 落 `.clang-format`：LLVM 基底 + 4 空格/列宽 100/指针左贴/`AccessModifierOffset -4`/`IndentCaseLabels`/include 保守不重排（`SortIncludes false` + `IncludeBlocks Preserve`）+ 单行短形态全开（if `WithoutElse`/lambda `All`/case/loop/enum）+ `BreakTemplateDeclarations Leave` + `AlignTrailingComments false`。`.editorconfig` 同落（tab/换行/编码）。
- **抽样对拍（5 文件，调参后）**：RHI.cpp 6.77% / AnimationPanel.cpp 4.11% / engine_tests.cpp 7.45% / Math.h 12.67% / EditorContext.cpp 1.59%（加权 ~6.4%）。**未达批文件 <5% 判据，交底如下**：残余差异全部归因两类——①clang-format 无选项保留的存量习惯（多语句单行、尾注释多空格、`case X: return` 单行、宏续行对齐；v23 的短函数合并不再覆盖多语句体）；②列宽回卷（存量超 100 列行：tests 263 / RHI 56）。对照组 ColumnLimit=0 时 0.63–8.45%（除无选项类外全部 <5%）佐证已达选项可达下限。**决策维持 ColumnLimit 100**（新文件列宽纪律；存量差异因"存量不重排"永不落地）。判据按本条修订理解。
- 工具注：本机 Intel brew 已无瓶子（要源码编 llvm 全家），clang-format 经 `pip install clang-format`（23.1.2）落地。

## T3 ccache 接入（mac 实测 / win 预留）

- 落 `cmake/CompilerCache.cmake` 单源（root CMakeLists 于 CPM 前 include，第三方 TU 同吃缓存）：mac 探 ccache / win 探 sccache，探测不到一行 WARNING 不阻塞；外部已设 `CMAKE_*_COMPILER_LAUNCHER` 时不接管。`win-ci` preset 显式 `LEMON_COMPILER_CACHE=OFF` 旁路（win-share 继承同旁路——CI 无缓存服务端）。
- ccache 4.14.1 安装走 GitHub release 源码包自编（brew Intel 无瓶子死路；vendored 依赖齐全，两分钟出二进制），`/usr/local/bin/ccache`，`-M 10G`。
- **三段计时（干净重建 = `rm -rf build/mac`，段 = configure / build / ctest）**：

| 轮 | configure | build | ctest | 备注 |
|---|---|---|---|---|
| ① 无缓存基线（`LEMON_COMPILER_CACHE=OFF`） | 336.5s | **253.3s** | 2.45s | 4/4 |
| ② ccache 冷（装填） | 353.0s | 198.0s | 2.43s | 392 可缓存调用 391 miss；另 188 调用不可缓存（第三方自定义命令等） |
| ③ ccache 热 | 796.5s* | **26.5s** | 2.48s | **392/393 直接命中（99.75% direct）**；4/4 |

  **build 段 253s → 26.5s（约 -90%）**。*③轮 configure 796s 为网络波动（见下条观察），与缓存无关——三轮 configure 的主体都是 CPM 对 imgui 等的逐轮克隆/校验。
- **观察登记（不排期）**：configure 段 336–796s 的大头是 CPM 每轮 `Cloning into 'imgui-src'`（走网络、不吃编译缓存）；若 imgui 等 CPM 包配 `SOURCE_CACHE` 本地缓存，configure 可望数倍缩短。另 win 侧 VM 同款计时一轮待机器窗口（prlctl 通道在，未跑）。

## T2 测试拆文件（engine_tests.cpp → tests/engine/ 七 TU + TestMain）

- 7,939 行 / 139 函数 → `CoreTests(9) / RendererTests(13) / EcsTests(39) / GameplayTests(27) / AssetsTests(17) / EditorTests(19) / AudioTests(15)` + `TestMain.cpp` + 共享 `TestFramework.h`（Expect/ExpectNear/ExpectNear0/g_checks 跨 TU 汇总）。机制 = 各 TU 暴露 `Run<TU>Tests()`，TestMain 汇总调用（域内保持原定义序）。
- **搬家非重写**：拆分器（括号深度解析 + 映射表）生成，143 实体（139 测试 + 4 局部 helper）**函数体逐字节对拍通过**；脚手架（include 全集共享 + namespace 包裹 + Run 出口）为本批新生成，随后整体过 clang-format（T4 联动自证）。`tests/CMakeLists.txt` 源列表切换，`lemon-tests`/ctest 名不变，`engine_tests.cpp` 删除（无兼容层）。
- 编辑器守卫按原文件**逐函数实测**重建（定义区守卫 30 个 / 调用区守卫 33 个——两集合本就不对称，LAT1 六函数"调用守卫、定义开放"如实保留）。
- **顺手修（原文件潜伏 bug）**：`TestPrefabCachePlaySpawn / TestCameraFollowCore / TestSceneExtractorCore / TestPoolDataStable / TestSaveChannelSplits` 五个 M7a 批③ 函数原文件中**定义在 `#ifdef LEMON_EDITOR_CORE` 区内、调用在区外**——editor=OFF 时原 engine_tests.cpp 同样编不过（该配置自批③ 起从未构建过所以未炸）。修复 = 五处调用按定义侧补守卫。
- **门格**：editor-on **34,346 checks 精确不变（硬门）** + ctest 4/4；editor-off 首次可编译可运行（33,721 checks——编辑器域测试按设计排除）。`docs/README.md` 维护纪律新增"单 .cpp >2,000 行 = 拆 TU 信号"软规约；拆后最大 TU 1,913 行达标。

## 中断与恢复备注

③轮计时跨了一次会话中断（01:51 启动、14:16 续跑）：/tmp 脚本丢失重写，ccache 缓存（391 条）与生成文件无损；③轮为中断后重跑，机器负载时段不同，build 段 26.5s 的量级结论不受影响。

## 出口对表（批文件验收汇总）

| 判据 | 结果 |
|---|---|
| AGENTS.md <150 行且信息零丢失 | ✅ 52 行；对账表 §T1.1 勾销 |
| checks 34,346 不变 | ✅ 精确一致 |
| ctest 4/4 | ✅（editor-off 配置另 33,721 新基线） |
| 回归 full 20/20 | ✅ PASS=20 FAIL=0（bench-survivor fps=82 ≥ 76.5；[ci-daily 报告](../../build/ci-reports/2026-10-07.log)） |
| 干净重建计时三段数字入 DevLog | ✅ 本条目 T3 表 |
| 抽样 format 差异率 <5% | ⚠️ 实测 1.59–12.67%（加权 6.4%），残余全量归因"无选项保留类 + 列宽回卷"，ColumnLimit=0 对照佐证已达下限；判据按本条目 T4 节修订理解 |
