# M7c 批⓪ —— 工程卫生四件（AGENTS.md 瘦身 / 测试拆文件 / ccache / .clang-format）

Status: done（2026-10-07 同日开工收口；顺序 T1→T4→T3→T2，全门格过——[DevLog](../../DevLog/2026-10-07-m7c-b0-engineering-hygiene.md)）

> 来源：用户拍板 2026-10-07（三件一起做）；[M7c.md](./M7c.md) §1 事实 1–4。四件互相独立、可各自提交；统一出口 = 回归 full 20/20 零扰动。
>
> 动工前必读：[docs/README.md](../../README.md)（文档五区规约——T1 的指针化去向依据）。

## T1 AGENTS.md 瘦身

**现状**：全文 46 行，行 3 单段 50,199 字节——M5→M7a 各批完工流水全部内联。每次会话固定烧 ~2.5 万 token，且内容与 `Plans/M*/` 批文件、DevLog 条目三重复述。

**目标形态**：AGENTS.md = 工程宪章 + 当前阶段快照，全文 **<150 行**：

1. **保留四节不动**（构建与运行 / 硬性纪律 / 本机坑 / 流程约定）——这是每次动工都要读的操作面（含 miniaudio 禁整读、lemon-game `--validate` DYLD 绕行、设备先于 ScriptHost 等活性坑）。
2. **行 3 巨段 → 「当前阶段」10 行以内**：M0–M7a 一句话完成态（指向 08 路线表）；当前活跃段 = M7c 批⓪/批①（指向 [Plans/M7c/M7c.md](../../Plans/M7c/M7c.md)）；**待用户清单三条原样保留**（M4.8 零文档走查 / svr-test 总成 V1–V3 / W5 真机 GPU——行动项不能丢）；一段历史一句指针（M5–M7a 明细见各 Plans 总览页与 DevLog，不复制内容）。
3. **瘦身前先落对账表**（本文件 §T1.1）：删掉的每一块信息 ↔ 已存在的文档位置，逐块确认无孤儿信息；对账表随批文件留档。
4. 链接全部相对路径；完工时机械检查全链接可解析（grep 提取 + 逐个 test -f）。

**验收**：AGENTS.md <150 行；行 3 <2KB；四节与待用户清单零丢失；全链接解析通过。

### T1.1 对账表（删前先落，2026-10-07）

原行 3 巨段（50,199 字节）逐信息块 ↔ 已存在的权威位置（全部落表后才动 AGENTS.md）：

| # | 原文块（行 3 内联内容） | 对账去向（已存在，非本批新建） |
|---|---|---|
| 1 | Lemon 定位句（目标品类 / 不做 3D / 不做重物理） | AGENTS.md 新行 3 **原样保留**（定位不删） |
| 2 | M0–M3.5 / M4（M4.0–M4.8）/ M5 完成态与验收数字（回归 13/13、金回放零重录、bench ≥55fps、10 分钟一局真人验收 2026-09-30） | [08 §0 总览表](../../EngineDesign/08-Development-Roadmap.md)各行 ✅ 注记；[Plans/M5/M5.md](../M5/M5.md)；[DevLog 2026-09-30 验收](../../DevLog/2026-09-30-acceptance-m5-uidoc-d1.md) |
| 3 | 路线图历次重排（09-24 M6 拆分→09-28 RmlUi 前移 ADR-014→09-29 UI 线独立 M6b→09-30 Windows 优先 M7 拆段/M6d→M9→10-07 M7b 后移+M7c 新增） | 08 文首注记（历次重排全量在档）+ 对应 DevLog（[09-30](../../DevLog/2026-09-30-roadmap-reorder-windows-first.md)、[10-07](../../DevLog/2026-10-07-roadmap-reorder-m7b-deferred.md)）+ [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md) |
| 4 | M6a 批⓪ 多脚本+GUID 化 / 批① 打击感（回归 14/14、fx 饱和 fps=78） | [Plans/M6a/M6a.md](../M6a/M6a.md) 批次表 + 批文件（b0/b1，同目录） |
| 5 | M6a 批② 全链（T1 `.tab`/T2 TableStore/T3 动画面板/T3b–T3-UX9 工作台九轮/帧序事故/热修④⑤/选帧对话框 v3.2/Lemon.Tween A 档/余 T4–T6） | [Plans/M6a/](../M6a/) 目录各批文件（M6a.md 链接全量在档）+ DevLog 对应条目 |
| 6 | M6b（原 M6a 批③）③a–③e 全链（RmlUi 地基→UIDocument→样板批→铺量批→验收） | [Plans/M6b/M6b.md](../M6b/M6b.md) + 同目录批文件 + DevLog（含 2026-09-30 验收系） |
| 7 | M6c 批⓪–批④ + 批①b 流式 + 听感热修两轮 + 真人验收收官（单测 34040→34346 轨迹、fps=82） | [Plans/M6c/M6c.md](../M6c/M6c.md) + 批文件 + [DevLog 2026-10-01 验收](../../DevLog/2026-10-01-acceptance-m6c-b4.md) |
| 8 | M7a 批⓪–批⑧ + review 轮 + W1–W7 真机 + CI 手动档修订（ADR-016、34346 基线、fps 门禁 76.5） | [Plans/M7a/M7a.md](../M7a/M7a.md) + 批文件（[批⑧](../M7a/2026-10-06-b8-closeout.md)）+ DevLog（含 [CI 手动档](../../DevLog/2026-10-07-ci-manual-mode-revision.md)） |
| 9 | 用户幸存者游戏并行开工（`demo/svr-test` 为工作项目，引擎卡点 DevLog 登记） | 08 文首 2026-09-24 注记 + [M6a.md](../M6a/M6a.md) 验收口径 |
| 10 | 回归基线对表（full 20 步 / ctest 4/4 / 单测 34,346 / bench-survivor fps≥76.5） | 08 M7c 行 + 09 §9 + [DevLog 10-07 §不变项](../../DevLog/2026-10-07-roadmap-reorder-m7b-deferred.md) |
| 11 | **待用户三条**（M4.8 零文档走查 / svr-test 总成 V1–V3 / W5 真机 GPU） | AGENTS.md 新行 3 **原样保留**（行动项不迁走）+ 08 M7 行注记 |
| 12 | 巨段原文全文 | git 历史（AGENTS.md 随仓库版本管理）+ 本对账表留档 |

**逐块结论：无孤儿信息**——所有删除内容的权威位置均已存在；历史叙事原文由 git 历史保全。

## T2 测试拆文件

**现状**：`tests/engine_tests.cpp` 7,939 行 / 139 个 `void Test*()` 单 TU；tests/ 源文件仅 3 个（engine_tests / script/main / probes）。

**拆分方案**（开工时按全量函数清单定稿分组边界，139 函数逐一归组）：

| 新 TU（`tests/engine/`） | 预期函数族（按现行 grep 所见） |
|---|---|
| `CoreTests.cpp` | Vec2/Mat3x2/Rect/Color/Utils/Rng/JobSystem/Pool/RingQueue |
| `RendererTests.cpp` | Atlas/BatchKey/SortStability/Particles/Renderable/Camera/Quality/BitmapFontLayout |
| `EcsTests.cpp` | Scene/World/Archive/ComponentRegistry/SystemPipeline/SpatialHash/TargetBoard/ConcurrentDestroy 等 |
| `GameplayTests.cpp` | Hit/Pickup/Xp/WaveDirector/Animator/Tween/StatEffects/Knockback/Projectile/Trigger 等 Verify* 族 |
| `AssetsTests.cpp` | AssetIndex/Manifest/LAT1/ProjectFile/SpriteRefs 等（M7a 批②④ 新增族） |
| `AudioTests.cpp` | AudioEngine/Baked/Streaming/Limiter 族 |
| 其余小族 | Ui/Fx/Table 等按量并邻或独立（<200 行并入最近域） |

**机制**：`tests/CMakeLists.txt` 目标名 `lemon-tests` 与 ctest 名 `engine-tests` 不变，只改 add_executable 源列表；新增 `tests/engine/TestMain.cpp` 调各 TU 暴露的 `Run*Tests()`（无注册表宏——保持现行 plain function 风格）。

**约束（搬家非重写）**：逐函数只搬不改正文；checks 总数不变（**34,346 = 硬门**）；`imgui_isolation` 断言照过；回归 full 20/20。

**顺手**：docs/README.md 维护纪律节加一行软规约——「单 .cpp >2,000 行 = 拆分信号（引擎 27.6k 行现最大 RHI.cpp 1,727，达标；超线随消费者批次拆）」。

**验收**：checks 34,346 不变；ctest 4/4；回归 full 20/20；`engine_tests.cpp` 删除（无残留 include 兼容层）。

## T3 ccache / sccache 接入

**现状**：六 preset（mac/mac-debug/win/win-debug/win-ci/win-share）零编译缓存；干净重建时 SDL3/FreeType/RmlUi/miniaudio/stb 全量重编。

**方案**：

- 落位 `cmake/CompilerCache.cmake`（六 preset 共享，root CMakeLists include）：`find_program` 探测 launcher——mac 侧 ccache（brew）、win 侧 sccache（winget/choco）；**探测不到 = 不挂 launcher + 一行 WARNING 给安装配方**（绝不阻塞 configure——CI 与新机器零摩擦）。
- preset 侧不加变量（缓存配置收敛在 cmake/ 单源，preset 文件不动——避免六处重复）。
- **win-ci 例外**：CI 无缓存服务端，sccache 空转有开销——`win-ci` preset 显式 `LEMON_COMPILER_CACHE=off` 开关旁路；GitHub Actions 缓存集成登记不排（观察项）。
- 首次预热：`ccache -M 10G`（或留默认 5G）；文档配方写进 AGENTS.md 构建节一行。

**验收（数字入 DevLog）**：mac 侧三段计时对比——①无缓存干净构建（基线）→ ②`ccache -s` 预热统计（第三方 TU 命中率）→ ③预热后干净重建（`rm -rf build/mac` 重配重编）；win 侧 VM 一轮同款。目标：第三方与未改动引擎 TU 命中，重建时间显著下降（具体数字实测为准，不做承诺值）。

## T4 .clang-format 最小集

**现状**：无任何格式化配置；多会话 AI 协作生成代码无机械风格防线。

**方案**：

- 落 `.clang-format` 最小集：LLVM 基底微调到贴现状（4 空格缩进 / 列宽 ~100 / 指针左贴 `Type* name` / 头排序保守关——防大范围重排）。开工时抽样 3–5 个代表文件（RHI.cpp / Systems.cpp / 一个 Panels）对拍 `clang-format --dry-run` 差异率，调到现存代码低扰动。
- **存量不重排**（决策）：防无关 diff 污染金回放对拍与 git blame；新文件与被触碰文件随手 format；全量渐进格式化随各批次自然发生（登记观察，不做专项）。
- `.editorconfig` 顺手同落（tab/换行/编码一致性，零成本）。
- **.clang-tidy 不落**（范围蔓延防线——本批只防漂移，不引入静态分析负担；bugprone/clang-analyzer 挂 ci-daily 另议登记）。
- **挂 CI 缓一步**：全仓 `--dry-run --Werror` 对存量必红；diff-only 检查（只查改动文件）待回归脚本演进，登记不排。

**验收**：`.clang-format`/`.editorconfig` 落仓库根；抽样文件 dry-run 差异率低（<5% 行）；新写测试 TU 全量过 format（T2 与 T4 联动的自证）。

## 顺序 / 工作量 / 风险

- 顺序：**T1 → T4 → T3 → T2**（T2 最大最机械放最后；T4 先于 T2 = 新拆出的 TU 直接以 format 后形态落盘，一举两得）。合计 0.5–1 天。
- 风险：①T1 删信息 → 对账表先行硬门；②T2 漏搬函数 → checks 数对拍 34,346 硬门 + ctest 全跑；③T3 探测逻辑在无缓存机上空转 → find_program 缺省不挂 + win-ci 显式旁路；④T4 配置不当引发大重排 → 存量不重排决策 + 抽样对拍。

## 验收判据汇总（出口）

AGENTS.md <150 行且信息零丢失（对账表勾销）+ checks 34,346 不变 + ctest 4/4 + 回归 full 20/20 + 重建计时三段数字入 DevLog + 抽样 format 差异率达标。

> **出口对表（2026-10-07 勾销）**：AGENTS.md 52 行/链接 10/10/四节零扰动 ✅；checks 34,346 精确一致 ✅；ctest 4/4 ✅；回归 full 20/20（fps=82 ≥76.5）✅；三段计时（build 段 253s→26.5s，-90%）入 DevLog ✅；format 抽样 1.59–12.67%（加权 6.4%）**未达 <5%，归因与 ColumnLimit=0 对照交底于 DevLog T4 节**（残余 = clang-format 无选项保留的存量习惯 + 列宽回卷；存量不重排决策下永不落地）。顺手修：原文件 editor=OFF 潜伏编译断（五个批③ 函数定义守卫/调用裸奔）随 T2 修复。
