# Lemon 引擎设计 — 08 开发路线图

> 前提：单人全职（每周 5 天 × 6–8 有效小时）；总盘 9–12 个月到"两个可玩模板 demo + 一键出包"。
> 纪律（继承 Prowl2D 教训）：**每个里程碑必须有 GUI 级可验收产物**——"单测全绿但编辑器不可用"不算完成。
> **2026-09-24 重排**：M6 拆为 **M6a 玩法完善+幸存者产品化（4–6 周）→ M6b 音频（原 M6.5 前移改号）→ M6c Tilemap+TD 模板**。动因：原 M6 大半为 TD 专属件，却把幸存者向产品所需的通用件（多脚本/动画控制/GUID/配置表/产品壳）一并阻塞到 TD 之后；前置后**用户幸存者游戏即刻并行开工**（`demo/svr-test` 为工作项目，引擎卡点 DevLog 登记，M6a 验收以该游戏为准）。ARPG 模板暂缓决策（2026-09-24 用户），M6c 后按 RmlUi 升级触发条件（06 §8 v1.x）再议。历史文件中"挂 M6"字样按此映射读取。**2026-09-28 注记：RmlUi 触发条件已提前成立（用户游戏富排版 + 文本输入需求），正式接入重排至 M6a 批③，见 [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)。** **2026-09-29 重排：UI 线（原 M6a 批③）独立为 M6b 游戏UI产品壳（M6a 三线并进观感混乱，用户拍板；子批号 ③a–③e 与批文件随迁沿用）；音频 M6b → M6c、Tilemap+TD M6c → M6d。历史文件中"挂 M6b/M6c"字样按此映射读取。** **2026-09-30 重排（Windows/出包优先，用户三拍板）：M6d Tilemap+TD → 新增档位 M9（路线图原止于 M8），M6b ③e 图鉴随迁（游戏玩法内容，与 TD 模板同批消费更贴）；M6c 音频确认先于 M7 出包主体（.baked 音频类型随 M6c 定形，packager 一次做全类型）；M7 拆两段——M7a 独立运行时+最简出包先行（`Engine/Assets` 运行时资产层 + GameEntry/lemon-game，[评审建议书](../Reports/2026-09-30-engineering-recommendations.md) R1），Steam/安装器/云档等发行侧后置（真包后再议）。历史文件中"挂 M6d"字样按 M9 读取。决策记录见 [DevLog](../DevLog/2026-09-30-roadmap-reorder-windows-first.md)。**
**2026-10-07 重排（用户拍板）：M7b 发行侧（Steam/云档/成就/安装器/资源校验）后移至 M8/M9 之后——引擎与编辑器功能优先（出包能力 M7a 已具备，不阻塞任何事）；新增档位 M7c 引擎与编辑器功能段（批⓪ 工程卫生：AGENTS.md 瘦身/测试拆文件/ccache/.clang-format ＋ 批① Fx 表现升级：TTF→图集烘焙器/贴图血条+延迟条/飘字动效与中文字形；批② 起随 svr-test 滚动登记，[Plans/M7c/M7c.md](../Plans/M7c/M7c.md)），新执行序 = M7c → M8 → M9 → M7b。历史文件中"挂 M7b"字样仍指发行侧，仅时点后移。决策记录见 [DevLog](../DevLog/2026-10-07-roadmap-reorder-m7b-deferred.md)。**

---

## 0. 里程碑总览

| 里程碑 | 内容 | 周期 | 出口判据（全部满足才进下一个） |
|---|---|---|---|
| **M0 技术验证 spike** ✅ | Vulkan/EnTT/CoreCLR 三大风险各打一枪 | **3 周**（实际 1 天） | ✅ **GO**——全绿（[M0 Go/No-Go 报告](../Reports/2026-09-18-m0-go-no-go.md)） |
| **M1 渲染内核** ✅ | RHI + 合批 + 图集 + 粒子 + 位图文本 | 6–8 周（实际 1 天） | ✅（2026-09-18）bench-mow 107fps；GPU 1.52ms；CPU 3.05ms；批数恒 4（[09 §7.5](./09-Testing.md)） |
| **M2 ECS 运行时** ✅ | 组件目录 + 系统管线 + 空间哈希 + 查询层 + Team + F3 面板 | 4–6 周（实际 1 天） | ✅（2026-09-19）bench-sim 1 万怪 **avg 5.10ms**（判据 ≤8）；回放 5 分钟双档 PASS；11545 checks（含复审轮）（[09 §7](./09-Testing.md)） |
| **M3 C# 脚本层** ✅ | CoreCLRHost 全量 + 档②批量系统 + 档①脚本组件 + 事件桥 + 异常隔离 + 调试通路（热重载移 M4，[ADR-010](../ADR/ADR-010-M3-Scope-Thread-RNG.md)；ALC 卸载经 M3-2b 实测降级为已知 runtime 限制，同 ADR 修订） | 4–6 周（实际 1 天） | ✅（2026-09-19）bench-script 5k 弹整步 **0.25ms**；100k 净时比 **1.43×**（判据分档见 ADR-010 D5）；回放 18000 帧双档 PASS；毒脚本 60 帧自动禁用不崩；托管分配硬 0（D6）；断点通路 ✅（[09 §7.6](./09-Testing.md)） |
| **M4 编辑器 v1** ✅ | 面板框架 + SceneView + Inspector + Play 沙盒 + Undo + 资产浏览器 + C# 热重载（自 M3 移入，ADR-010） | 6–8 周（实际 1 天） | ✅（2026-09-20）判据链全量化 PASS：零代码判据场景 Play fps 59（≥45）、热重载 1.3s ≤2s StateBag 续跑 66/66、进出 4.3/0.4ms 逐字节一致、冷启 367ms（[Plans/M4/M4.md](../Plans/M4/M4.md)） |
| **M4.6 编辑器可用性加固** | 会话闭环（项目中心最小形态/最近项目/目录选择）+ 编辑效率件（重命名/拖拽导入/新建脚本/编译提示）+ 交互路径冒烟（触发：M4 收官当日真人实测 3 阻断 bug，已修） | 2–2.5 周（M4.6a ✅ `2273e7f`；M4.6b ✅ 2026-09-20；交互冒烟 = `--smoke-ui` 21/21 ✅ 2026-09-21；30 分钟真人走查待收官执行） | 新用户 30 分钟零命令行零文档完成 新建项目→导入→摆场景→Play→保存→关闭；交互冒烟 errors=0（[M4.6 计划](../Plans/M4/2026-09-20-m4.6-usability.md)） |
| **M4.7 编辑器 UI 精美化** ✅ | overlay 通道 P0 修复（网格/选框/Gizmo/标签四要素）+ 主题 token + 自绘图标 16 枚 + 工具栏三段式 + 网格 v2/v3 + 视口交互 v2（一段式拖拽/轴约束/Esc 取消）+ GameView Aspect + M4.7d 可选件（label-scrub/Console 折叠/面包屑/Layout 下拉） | 1–2 周（P0/a/b/c + d 全批次 ✅ 2026-09-21） | ✅（2026-09-21）批次判据全过：`--screenshot` 四要素可见、一段式拖拽 + Esc 恢复、全库无散落 ImVec4 字面量、`tools/editor-regression.sh` **11/11**；真人手测七轮暴露问题全修（1–5 轮 6 大类 14 项归档 [手测修复归档](../Reports/2026-09-21-m4.7-handtest-fix-summary.md)；6–7 轮见 DevLog 同日条目）（[M4.7 计划](../Plans/M4/2026-09-21-m4.7-ui-polish.md)） |
| **M4.8 编辑器收官批** | 组件级重置 + File 最近场景 + 冒烟状态隔离（ini 漂移根治）+ 30 分钟零文档走查收官（触发：手测指南 C–L 段全量走查，修复批 `bd3ad93` 之后） | 2–4 天（a/b/c ✅ 2026-09-22；走查待用户执行） | 走查全绿 + smoke-drag 连跑 10 次全绿 + 回归 11/11（[M4.8 计划](../Plans/M4/2026-09-22-m4.8-closeout.md)） |
| **M5 玩法 + VS 模板** | 技能/弹幕/命中/拾取/导演/HUD/存档 + vs-survivor 模板；**实体多脚本**（ScriptBox 多实例 + `.scene` `scripts[]` + Inspector 列表 + SDK `AddComponent<LemonBehaviour>` 路由，2026-09-22 用户实测登记；**批④ 未落地**——模板以单 PlayerBehaviour 规避，挂 M6 重排）；**编辑器后置项随消费者**：字段级重置（先补字段默认值元数据）、tag 资产化+下拉（等 FindByTag 用法）、Select 工具多选组操作、dotnet build 异步化（观察项转正评估） | 6–8 周 | **10 分钟完整一局可玩**（批④ 2026-09-23 代码面完成：vs-survivor 模板 + `--smoke-template` 机械链 PASS；真人 10 分钟一局验收待用户执行）；**压测 B 达标**（1 万怪 ≥45fps ✅）；**Play 调参 ADR**（ADR-011 显式不回灌 + Inspector 横幅 ✅） |
| **M6a 玩法完善 + 幸存者产品化** | M5 余项收口（`scripts[]` 多脚本、Animator `Play`/`CrossFade`）＋ sprite 引用 GUID 化 ＋ 打击感（位图数字/飘字/世界血条）＋ 内容生产（配置表外置 ADR、AnimationEditor 最小版、技能路径数据化、存档分档）。（原含产品壳 RmlUi 线——**2026-09-29 拆出独立 M6b**：③a–③c 已于 M6a 期内完成，余量与批文件随线迁 [M6b](../Plans/M6b/M6b.md)） | 4–6 周（批⓪ ✅ 批① ✅ 2026-09-24：多脚本/GUID 化/SDK 双路由/模板三拆 + 打击感（Anim 换段/Fx 飘字血条）；批② ✅ 2026-09-28：配置表双轨 ADR-012（.tab/CSV 导入/内嵌表格+浮动编辑）+ 动画工作台 v3.1（T3→T3-UX2 四轮）+ ADR-013 状态机 + 数值表双面落地（模板+svr-test 验收② 演示）+ 存档三档 slot_0/settings/meta（键约定：settings 版本化 KV / meta 收集条目）；[批⓪](../Plans/M6a/2026-09-24-b0-multiscript-guid.md) [批①](../Plans/M6a/2026-09-24-b1-combat-feel.md) [批②](../Plans/M6a/2026-09-25-b2-content-production.md)） | **用户幸存者项目全流程零 C++ 可玩**（主菜单→≥10 分钟一局→死亡结算→重开/回菜单；UI 屏部分由 M6b 承接）；新武器/敌人纯 prefab+C#+配置表；动画状态 Play 可切（T3d 升格：.controller 状态机/参数黑板/帧事件，ADR-013，ani.scene 双怪对决终验 ✅ 2026-09-27）；飘字/血条开启 bench-survivor ≥45fps；GUID 改名/manifest 重建引用稳定（批⓪ `--smoke-guid` ✅） |
| **M6b 游戏UI产品壳**（2026-09-29 自 M6a 批③ 独立；[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)） | RmlUi 地基 + 屏幕层五子批（③a–③c ✅ M6a 期内：渲染地基/字体资产/C# API 波1 机制——smoke-uirml 全链 + script-tests 1699 + 回归 15/15）→ ③d 前置 UIDocument 场景挂载（Unity 同构粒度 + 进 Play 自动装载双通道 + 归位，planned）→ ③d 模板迁移（六屏 → `.rml` + L2 默认皮）→ ③e 图鉴（**2026-09-30 迁 M9**）；流程状态机档1（单场景零引擎改动）+ LoadScene 档2 评估 | 3–4 周（③a–③c ✅；③d 前置 planned；[M6b.md](../Plans/M6b/M6b.md)） | vs-survivor 六屏全走 `.rml` + UIDocument 挂载；L2 换皮零代码；图鉴 500 条 ADR-008 预算内；与 M6a 验收① 合流：svr-test 主菜单→一局→结算→重开全流程 UI 层零 C++ |
| **M6c 音频系统**（原 M6.5 → M6b 前移 → 2026-09-29 改号 M6c） | miniaudio 后端 + 2D 定位声源（SFX 一次性/BGM 循环）+ 音频资产（`.wav`/`.ogg` 导入 → `.baked`）+ C# `Lemon.Audio` + 编辑器试听/Inspector 槽 + 主/组音量 | 2–3 周 | VS/TD 模板全程有声（命中/击杀/拾取/升级/BGM/波次横幅）；100 并发 SFX 模拟侧 ≤ 0.5ms（解码不占主线程）；无音频设备/静音输出不崩（无头 CI 可跑） |
| **M7 发布管线**（2026-09-30 拆 M7a/M7b，见 §2） | **M7a 独立运行时+最简出包**（`Engine/Assets` 运行时资产层 + GameEntry/lemon-game + 目录拷贝式 packager）→ **M7b 发行侧**（Steam/云档/安装器/资源校验，真包后再议） | 4 周（**M7a ✅ 全闭 2026-10-07**——代码面 2026-10-06 + 真人总成 V1–V3 2026-10-07 用户过：批⓪–批⑧ 三日全落 + 真机 W1–W4/W6/W7 + 模板真人验收两轮，[验收记录](../DevLog/2026-10-07-acceptance-m7a-b8-v1-v3.md)；W5 真机 GPU 移交物理机窗口；[M7a.md](../Plans/M7a/M7a.md)；M7b 移交清单见批⑧ 批文件 §2；**M7b 时点 2026-10-07 后移至 M9 之后（引擎优先重排，见文首注记）**） | M7a：干净 mac+Win 机 `lemon-game --project demo/svr-test` 跑满 60 帧全流程零 C++、`git clean -xfd` 后无需 `.lemon/` 缓存；M7b：depot 上传成功、安装即玩 |
| **M7c 引擎与编辑器功能段**（2026-10-07 新增档位——M7b 后移后的引擎优先段，[Plans/M7c/M7c.md](../Plans/M7c/M7c.md)） | 批⓪ 工程卫生（AGENTS.md 瘦身/测试拆文件/ccache/.clang-format）＋ 批① Fx 表现升级（TTF→图集烘焙器/贴图血条+延迟条/飘字动效+中文字形）；批② 起随 svr-test 滚动登记 | 批⓪ ✅ 2026-10-07（回归 20/20 / checks 34,346 精确不变 / 干净重建 build 段 253s→26.5s，[DevLog](../DevLog/2026-10-07-m7c-b0-engineering-hygiene.md)）＋ 批① ✅ 机器面 2026-10-07（单测四件 checks 34,402 / ctest 4/4 / svr-test 实装 duel.winner 实证，[DevLog](../DevLog/2026-10-07-m7c-b1-fx-presentation-upgrade.md)；真人走查 2026-10-07 过——贴图血条待正式素材复验观感）＋ 批② ✅ 机器面 2026-10-07（per-资产音频参数：ClipFx 覆写回退链 + meta 三键 + 浏览器右键参数弹窗，checks 34,435 / 回归 20/20 首跑 / bench fps=85，[DevLog](../DevLog/2026-10-07-m7c-b2-per-asset-audio-fx.md)；真人听感待用户） | 批⓪：AGENTS.md <150 行、checks 34,346 不变、回归 full 20/20、干净重建计时入 DevLog；批①：svr-test 暴击中文弹跳/贴图血条/延迟条真人走查过 + hash 反例单测 + bench-survivor 门禁不降 |
| **M8 光照与打磨** | 光照裁剪版 + 后处理 + 性能终测（压测 A + **2h soak 长时稳定**）+ incremental 模板（若余量） | 4 周 | 压测 A 全绿（10k 怪 + 50k 弹 + 100k 粒 @60fps）；soak 2 小时内存曲线平/零崩溃（崩溃转储登记） |
| **M9 Tilemap + TD 模板**（2026-09-30 自 M6d 后移新增档位） | Tilemap/自动瓦片/FlowField/A*/摆塔状态机 + tower-defense 模板（波次表编辑器并入 M6a 配置表 ADR 定形，本里程碑只消费产出）+ 图鉴/收集模板（M6b ③e 随迁） | 5–7 周 | TD 模板 10 波通关；千怪走流场 CPU ≤ 2ms；图鉴 500 条 ADR-008 预算内（随迁判据） |

累计：**48–59 周（约 11–14 个月）**；用户游戏内容开发自 M6a 起与引擎并行，日历增量小于工时增量。M0–M3（内核+脚本）约 4 个月是硬风险区；M4–M7（编辑器+模板+产品化）是体验交付区。

## 1. M0 技术验证 spike（Go/No-Go 关卡，3 周）

> **✅ 2026-09-18 完成，判定 GO**——三判据全部达标（10 万精灵 270fps / C# 批量开销比 1.5× / 1 天完成三大 spike，验证层零错误）。
> 实测数据与十条教训见 [M0 Go/No-Go 报告](../Reports/2026-09-18-m0-go-no-go.md)。遗留：跨 UCO 调用的 ALC 卸载 pin → M3 用 DomainManager 托管线程方案解决（已验证可行）。
> 回退保险解除（不再依赖 Prowl2D 回退）。

> 目标：用最小代码验证三大技术风险。**任一 No-Go → 正式回退 Prowl2D 路线**（回退决策记录进 ADR-000）。

| 周 | 任务 | 产出 |
|---|---|---|
| W1 | 仓库骨架 + CMake（Luma 构建体系 C 级移植）+ SDL3 窗口 + Vulkan 1.3 设备 + VMA + 交换链 + 三角形 | `spike/01-triangle`（Win+macOS MoltenVK 双绿） |
| W2 | 实例化精灵：单四边形 + per-instance SSBO + 脏区间更新（Looper 对照）→ 10 万精灵动画压测 | `spike/02-sprites`：**1660 级参考卡 ≥ 60fps**；MoltenVK ≥ 30fps（记录数字） |
| W3 | CoreCLRHost 最小闭环（Luma 移植）：C# 每帧批量改 10 万实例颜色 + 事件队列往返；EnTT 装入 + 固定步长循环跑通 | `spike/03-csharp`：桥往返开销实测表；Tracy 接入 |

**Go/No-Go 判据**：

1. 10 万实例化精灵 60fps（中端独显；MoltenVK 开发可用即可）——渲染路线成立；
2. C# 批量通道每帧开销 < 0.5ms @ 10 万实例 + 热重载可行——混合模型成立；
3. 单人 3 周内能写出可运行的 Vulkan 骨架（学习曲线验证）——人力现实性成立。
三条全绿 → 立项继续；任一红 → 回退 Prowl2D 并重启其 M1（沉没成本仅 3 周）。

## 2. 各里程碑工作分解与验收

### M1 渲染内核（6–8 周）

RHI 完整化（bindless/ShaderCache/preheat/设备丢失）→ RenderableManager 移植 → 批键合批 → 图集运行时 → 粒子系统 → 位图文本 → 相机与 SortingLayer → 质量分级。
> **✅ 2026-09-18 完成**——判据全过：107fps（IMMEDIATE）/ GPU 1.52ms / CPU 3.05ms（压测 A 语境）/
> 批数恒 4 / 设备丢失注入恢复。专项与优化过程见 [09 §7.5](./09-Testing.md) 与 DevLog。
**验收**：bench-mow（10 万精灵 + 5 万粒子混跑）≥ 60fps 且 CPU 渲染线程 ≤ 4ms；图集切换不闪帧；设备丢失模拟（驱动重置）自动恢复。

### M2 ECS 运行时（4–6 周）

World/Scene/生命周期 → 组件注册表（元数据）→ 全组件目录 → 系统管线（16 系统）→ 空间哈希 + 查询 API → 分离力 → Team → 销毁两阶段 + 池 → F3 面板。
> **✅ 2026-09-19 完成**——判据全过：1 万怪 **avg 5.10ms**（多线程；系统净时间 ~2.1ms）；确定性回放
> 5 分钟（18000 帧）**双档 PASS**（`--threads 1` / 多线程，逐帧状态哈希零分歧）；F3 统计层 + `--stats`
> （ImGui 版 M4 接同一数据源）。附加交付：.scene v1（nlohmann/json，roundtrip 不动点 + 迁移链骨架，
> 风险 #7 提前落账）；JobSystem（Luma 移植 + 值语义修正，单线程档实测 19.9ms——证明并行是 8ms 判据
> 的必要条件）。优化与事故全记录见 [09 §6.7–6.8](./09-Testing.md) 与 DevLog。
**验收**：`bench-sim` 无渲染注入 1 万怪 AI+移动+分离 ≤ 8ms/步；确定性回放（同输入 5 分钟逐帧一致）；F3 面板数据齐全。

### M3 C# 脚本层（4–6 周）

CoreCLRHost 全量（域线程模型，ADR-010）→ SDK 核心子集（~50 导出，分期口径见 ADR-010）→ 批量系统档② → 事件队列桥 → 脚本组件档① → 结构变更命令缓冲 → 异常隔离 → 调试通路 + ALC 卸载自检。
**验收**：§M3 出口判据（见总表，ADR-010 修订版）+ 异常脚本不崩引擎 + 托管分配面板为 0（示例脚本）。
> **✅ 2026-09-19 完成**——判据全过（实测数字见 [09 §7.6](./09-Testing.md)）：5k 弹整步 avg
> **0.248ms**；C# 净时比按 ADR-010 D5 分档判（100k **1.43×** ≤1.5×，边际比 1.41×，固定往返
> ~66µs）；确定性回放 18000 帧**双档 PASS**；毒脚本 60 帧自动禁用、引擎不崩；示例脚本托管分配
> **硬 0**（bench 关 tiered + 导出指针缓存，ADR-010 D6 度量前提）；布局护栏 27/27 + blit
> roundtrip + PCG32 golden；断点通路（诊断 IPC + Lemon.Domain 线程名 + mac-debug 全量测试）。
> 卸载自检以否定性结论落账：本机 .NET 10.0.12 活线程触碰 ALC 即永久 pin（ADR-010 修订，
> M4 热重载走整域重建）。架构坑全记录：ScriptHost 块缓冲 deque 不连续（C# 线性步进跨块 =
> 野指针，10k 过/15k 崩）/ reserve 公式必须含末块补齐 / GetExport 每调在托管侧分配 /
> C# 静态字段文本序初始化——均已在代码注释与 DevLog 留档。

### M4 编辑器 v1（6–8 周）

面板框架 → Hierarchy/Inspector/AssetBrowser/Console → SceneView（相机/Gizmo/拾取）→ Play 沙盒 + Undo → C# 热重载（文件监视 + StateBag 迁移 + ≤2s，ADR-010 自 M3 移入）→ 模式栈/焦点仲裁（Editor-RPG2D 拷贝）→ 资产导入器 + manifest → 新建项目向导（blank 模板）。
**验收**：总表判据（纯编辑器搭出刷怪场景）+ 编辑器冷启动 < 2s + Play 进出 < 0.5s/0.3s。
> **✅ 2026-09-20 完成**（判据数据见总表行）。收官追加两轮：**M4.6 可用性加固**（会话闭环 +
> 编辑效率 + 交互冒烟，[M4.6 计划](../Plans/M4/2026-09-20-m4.6-usability.md)）与
> **M4.7 UI 精美化**（P0/a/b/c/d 全批次，[M4.7 计划](../Plans/M4/2026-09-21-m4.7-ui-polish.md)），
> 手测修复归档 [Reports/2026-09-21-m4.7-handtest-fix-summary.md](../Reports/2026-09-21-m4.7-handtest-fix-summary.md)。阶段正式关闭仅剩
> 真人验收动作：30 分钟零文档走查 + 录屏（清单见
> [Editor-Manual-Test-Guide.md](./Editor-Manual-Test-Guide.md) §5）。

### M5 玩法 + vs-survivor 模板（6–8 周）

技能/弹幕/命中盒/击退 → 拾取磁吸/经验/升级三选一 → 导演波次 → HUD（ImGui + 位图数字）→ 存档 → 默认素材包第一批 → 模板整合。
**验收**：10 分钟完整一局；压测 B 达标；"Play 中调参→改动回灌"或显式禁用提示（ADR 记录）。

### M6a 玩法完善 + 幸存者产品化（4–6 周，2026-09-24 自原 M6 拆出）

> **并行游戏线（本里程碑的成立前提）**：用户幸存者游戏即刻开工——`demo/svr-test` 为工作项目，M6a 的 GUI 级验收即该游戏；游戏侧撞到的引擎卡点逐条 DevLog 登记，按下方批次回灌（"后置项随消费者"纪律的延伸：消费者先跑起来，引擎批次追着喂）。

**WBS（三批依序 + 批③ 迁出；批文件开批时落 `Plans/M6a/`）**：

1. **批⓪ 架构地基**（先行——晚做返工面最大）：`scripts[]` 实体多脚本（M5 余项：ScriptBox 多实例 + `.scene` schema + Inspector 列表 + SDK `AddComponent<LemonBehaviour>` 路由，模板去单脚本规避）；**sprite 引用 GUID 化**（2026-09-23 登记④：`SpriteRenderer` 尾加 `spriteGuid` 字段——零重录口径——+ 场景加载期解析为运行时 spriteId，作者面（Inspector 槽/模板生成器/拖拽）只写 GUID；manifest 丢失/重排导致的引用错位**类别**消除）。**✅ 2026-09-24 完工**（[批文件](../Plans/M6a/2026-09-24-b0-multiscript-guid.md)：多脚本 schema v2 + 同类型唯一 + SDK 双路由（含 op5 DetachScript）+ 模板三拆（移动/战斗/HUD）+ `--smoke-guid` 三难回归第 14 步 + bench-survivor 82fps 不回归）。
2. **批① 表现与打击感**：Animator 状态控制 `Play()`/`Pause()`/`CrossFade()`（M5 余项——受击/攻击/死亡状态切换）；位图数字页 + 伤害飘字通道 + 世界空间血条（恒走 sprite 管线、池化，06 §8 恒定原则）。**✅ 2026-09-24 完工**（[批文件](../Plans/M6a/2026-09-24-b1-combat-feel.md)：Animator2D 尾加换段队列（FIELD_RT 零重录）+ `Lemon.Anim`（Play/Queue/CrossFade/Pause/Resume，纯字段写零 C ABI）+ World 级 `FxChannel`（飘字 256/血条 128 池化）+ `Lemon.Fx` + GameView sprite 管线消费 + 模板受击接线；smoke-anim/smoke-template 扩断言、回归 14/14、bench-survivor fx 饱和 fps=78）。
3. **批② 内容生产**：**数值配置外置**（CSV/JSON 表资产导入 + Inspector 表格查看/微调双轨；与"波次表编辑器"合并评估——表格式编辑器 vs 外置表导入 vs 双轨，开工时 ADR 定形；2026-09-23 登记①）；**AnimationEditor 面板最小版**（时间轴 + 帧序/时长/循环，05 §7 既列——登记②；最小版不足则手写 `.clip` 过渡，可裁剪）；新技能作者路径数据化（登记③）；**存档分档**（slot_N/settings/meta 三通道，06 §10 注记）。
4. **批③ 产品壳 = RmlUi 地基 + 屏幕层**（[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)，2026-09-28 触发条件成立后重定义；原"通用模态面板通道（Cards 泛化）"退役）——**2026-09-29 整体迁出独立为 M6b**（动因：M6a 三线并进混乱，用户拍板；批文件与子批号 ③a–③e 随迁沿用）：③a–③c 已于 M6a 期内 done（渲染地基/字体资产/C# API 波1 机制——③c 收口 smoke-uirml 全链 + script-tests 1699 + 回归 15/15）；余量（③d 前置 UIDocument 场景挂载 → ③d 六屏模板迁移 + L2 默认皮 + 流程状态机档1 → ③e 图鉴；LoadScene 档2 评估）全部归 [M6b](../Plans/M6b/M6b.md) 执行与验收。

**登记项（观察，不扩 scope）**：手柄输入（Steam 发布目标确认 → M6b 追加最小 Gamepad 输入位，随 UI 线迁出）；本地化（v1 中文单语即可，06 §9 Source Generator 移 v1.1）。

**验收**：①用户幸存者项目**全流程零 C++ 可玩**：主菜单 → 一局（≥10 分钟，三选一/Boss）→ 死亡结算 → 重开/回菜单（UI 屏部分由 M6b 承接）；②新增 1 武器 + 1 敌人变体仅靠 prefab + C# + 配置表（引擎零改动演示）；③动画状态 Play 中可切（受击/攻击/死亡，模板内可复现）；④飘字/世界血条开启下 bench-survivor ≥ 45fps（或逐项开销数字入 09 §6.10）；⑤GUID：资产改名/移动 + 删 manifest 重开，场景引用不错位（smoke 断言）；⑥模板 PlayerBehaviour 拆移动/战斗/HUD 三脚本 + `scripts[]` roundtrip；⑦回放：零重录（尾加字段口径）或按 09 §7 推论显式声明重录。

> **M5 收口后登记（2026-09-23 用户反馈轮；2026-09-24 重排后已并入上列批次，原文保留）**：①**数值配置外置**——波次/技能/掉落
> 走 CSV/JSON 配置表资产（Excel/Numbers 编辑 → 导入，Inspector 表格转查看/微调
> 双轨），替代"在 Inspector 里逐格填 16 波"的作者路径；与"波次表编辑器"合并
> 评估（表格式编辑器 vs 外置表导入 vs 双轨，开工时 ADR 定形）。②**AnimationEditor
> 面板**（05 §7 既列）随 TD/VS 模板帧动画需求开工——当前动画创建 = 精灵网格切片
> + `.clip` 资产（批③通道），无时间轴编辑 UI。③新技能作者路径数据化（当前 =
> C# behaviour + prefab + GameMain 注册三件套纯代码面）。④**sprite 引用 GUID 化**
> （批④后修③的治本项）：`SpriteRenderer.spriteId` 是场景/prefab 里最后一个
> "数字资产引用"（clipId/prefabId 已是 GUID 低 32 位）——数字号依赖扫描序/
> manifest 记账，GUID 依赖资产自身身份。方案 = 组件**尾加** `spriteGuid` 字段
> （加字段零重录口径）+ 场景加载时解析为运行时 spriteId（DB 查表），作者面
> （Inspector 槽/模板生成器/拖拽）只写 GUID；spriteId 降级为运行时缓存。残余
> 风险消除：manifest 丢失/损坏后"确定性重排"在资产集变化过（导入过新素材）的
> 项目上仍可能与旧场景引用错位——GUID 化后此类问题类别整体消失。

### M6b 游戏UI产品壳（3–4 周，2026-09-29 自 M6a 批③ 独立）

五子批（子批号沿用；批文件与总览页落 `Plans/M6b/`，[M6b.md](../Plans/M6b/M6b.md)）：③a 渲染地基 ✅ → ③b 字体与资产通道 ✅ → ③c C# API 与波1 机制 ✅（均 M6a 期内完成）→ **③d 前置 UIDocument 场景挂载**（planned：Unity UIDocument 同构粒度——一组件挂一 `.rml` + 进 Play 自动装载双通道 + EnterPlay 归位；[批文件](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)）→ ③d 模板迁移（卡片/对话/HUD/主菜单/暂停/设置/结算转文档 + L2 最小默认皮 + smoke 随迁）→ ③e 图鉴/收集模板（波1 全量消费者）；流程状态机**档1 = 单场景零引擎改动**，**档2 = LoadScene 独立评估**（档1 不够用再开工，ADR 定夺）。
**验收**：六屏全走 `.rml` + UIDocument 挂载；L2 换皮零代码；图鉴 500 条 ADR-008 预算内；与 M6a 验收① 合流（svr-test 全流程 UI 层零 C++）。
> **2026-09-30：③e 图鉴迁出至 M9**（用户拍板：游戏玩法内容，与 TD 模板同批消费更贴）；M6b 以 ③d-2 + 真人验收收官，验收中"图鉴 500 条预算内"随迁 M9。

### M6c 音频系统（2–3 周，原 M6.5 前移 → 2026-09-29 自 M6b 改号）

miniaudio 后端（00 选型表既定；引入时登记 `THIRD_PARTY.md` + 07 矩阵行）→ 音频资产类型（`.wav`/`.ogg` 导入、GUID/`.meta`、烘焙入 `.baked`——06 §2 类型表补行）→ 2D 声源（距离衰减 + 声像；一次性 SFX + BGM 循环，解码在音频线程/预解码）→ C# `Lemon.Audio`（Play/Stop/音量/分组，批量边界纪律同 04）→ 编辑器集成（AssetBrowser 试听、AudioSource 组件 Inspector 槽、Play 内混音面板最小版）。
**验收**：总表判据 + 模板一局全程有声 + 无音频设备不崩（CI 无头可跑）。
> **2026-09-30 开工**：实施计划 [Plans/M6c/M6c.md](../Plans/M6c/M6c.md)（批⓪–批④ 五批拆解，12–16 工作日）；音频架构与 `.baked` 容器 v1 定形 [ADR-015](../ADR/ADR-015-Audio-System-And-Baked-Format.md)（D1–D4 拍板后批⓪ 动工）；事件流水 [DevLog](../DevLog/2026-09-30-m6c-kickoff-design.md)。
> **2026-10-01 收官（真人验收过）**：批⓪–批④ 全部落码（两日完成，远快于预估——miniaudio 后端成熟 + ADR-015 前置定形）。四判据机器面全过：100 并发 0.0092ms（批⓪）/ 无设备不崩 + `LEMON_AUDIO=off` 回归步（批③）/ 模板全程有声——CC0 七件实装 + `aud(mount=7 pause=5 resume=6)` 冒烟位（批④）/ bench-survivor fps=82 ≥ M6b 基线 78 零降级。真人验收清单六条 2026-10-01 过（初步走查 + 暂停热修复测；[验收记录](../DevLog/2026-10-01-acceptance-m6c-b4.md)）——**里程碑收官，下一档 M7a**。
> **2026-09-30 排序确认（用户拍板）**：音频先于 M7 出包主体开工（对调原建议的第 1/2 批）——.baked 音频资产类型随本里程碑定形，M7 packager 一次做全类型。
> **2026-09-24 登记**（[全栈审查](../Reports/2026-09-24-code-review-546a755.md) §7 缺口）：此前 M0–M8 无任何音频引擎侧条目——00 选型表仅一句 miniaudio、风险 #6 仅素材侧提及，属规划外缺口。范围裁剪（不做 DSP 图/中间件/3D 空间化）与砍单顺序 §4 一致。
> **2026-09-24 前移改号**：原排 M6 与 M7 之间（M7 packager 依赖音频资产类型先行，该依赖不变）；重排后提前至当时 M6b——幸存者产品（M6a 产出）与 TD 模板两个消费者都在其后拿到声音。**2026-09-29 再改号 M6c**：UI 线独立占用 M6b（见文首重排注记）。

### M9 Tilemap + tower-defense 模板（5–7 周，2026-09-30 自 M6d 后移；原 2026-09-29 自 M6c 改号）

Tilemap 数据 + chunk 烘焙渲染 → 碰撞层 → 自动瓦片 + TilePalette/笔刷 → FlowField + A* → 放置状态机（塔防摆塔）→ TD 模板整合 + 图鉴/收集模板（自 M6b ③e 迁入）。
**验收**：TD 10 波通关；千怪流场 ≤ 2ms；自动瓦片 47 变体正确；图鉴 500 条 ADR-008 预算内（随迁判据）。
> **2026-09-24 重排注**：自原 M6 拆出（通用件归 M6a/M6b）；"波次表编辑器"并入 M6a 批② 配置表 ADR 定形，本里程碑只消费其产出；bench-survivor"直接用默认素材"接轨（06 §7 注记）随本里程碑 tileset 素材与模板打包落地。
> **2026-09-30 后移注**：Windows + 独立二进制 + 出包优先（[评审建议书](../Reports/2026-09-30-engineering-recommendations.md) R1——"能跑起来的独立 demo"边际收益最高）。历史文件中"挂 M6d"字样按 M9 读取。

### M7 发布管线（4 周，2026-09-30 拆 M7a/M7b）

**M7a 独立运行时 + 最简出包（先）**：`Engine/Assets` 运行时资产层（stb_image 解码换 TU 编入 `lemon-engine`、图集烘焙 `.baked` 读写、GUID/manifest 运行时只读——[评审](../Reports/2026-09-30-architecture-and-defect-review.md) §5.4：当前运行时零 PNG 解码能力，资产库住在 lemon-editor-core，本里程碑的真实前置不是打包脚本）→ `Engine/Entry/GameEntry` + `add_executable(lemon-game)`（ADR-005 同源双入口兑现；Play 从"运行游戏的唯一方式"降级为编辑器特权）→ packager 最简形态（目录拷贝 + manifest，不做压缩/加密/增量）。缺陷第二批（数据完整性：D6/D7/D8/M21/M22–M25）随本段穿插。
**M7b 发行侧（后，真包后再议）**：Steam 集成（steamworks 动态加载 + 云档 + 成就）→ .baked 资源校验 → 安装器 → 安装包回归（干净 Win 虚拟机）。
**验收**：M7a——干净 mac+Win 机 `lemon-game --project demo/svr-test` 跑满 60 帧、四屏全流程零 C++、`git clean -xfd` 后无需 `.lemon/` 缓存（[评审建议书](../Reports/2026-09-30-engineering-recommendations.md) R1 判据）；M7b——出包上传 depot 成功；干净机安装即玩；云档冲突策略生效。
> **M7a 代码面收官（2026-10-06 批⑧）**：批⓪–批⑧ 三日全落（[M7a.md](../Plans/M7a/M7a.md)）——ADR-016 + entryScene → 缺陷第二批 → `Engine/Assets` 资产读取核心 → Play 装配下沉（金回放跨版本零重录实证）→ GameEntry/lemon-game 竖切 → packager mac → 图集 `LAT1` → Windows 真机收口（W1–W4/W6/W7 全过；W5 真机 GPU 待物理机窗口，不阻塞）→ 收官（CI 每日回归工具 = 本机 `Tools/ci-daily.sh` + GitHub win job（**2026-10-07 用户改拍板手动档**，定时撤/模板留档）、回归 20 步含 bench-survivor 门禁 fps≥76.5、09 §9 门禁分流三条全清、文档五区落账）。判据机器面：mac 干净包四屏零 C++ fps=428（批⑤）+ Win lavapipe 包双击即玩（W4）+ manifest 回退扫描（批②④）。模板真人验收两轮过（批④⑤）；**svr-test 主项目真人总成 V1–V3 ✅ 2026-10-07 用户过**（批⑧ 清单，[验收记录](../DevLog/2026-10-07-acceptance-m7a-b8-v1-v3.md)）——**M7a 全闭**。M7b 移交清单见 [批⑧ 批文件](../Plans/M7a/2026-10-06-b8-closeout.md) §2。
> **开工前置（Gate C，2026-09-24 登记——[全栈审查](../Reports/2026-09-24-code-review-546a755.md) F-13/F-11）**：① CI 落地（09 §9：macOS runner 先行，push 全量逻辑测试 + 每日编辑器回归与性能基线门）；② Windows 编译阻断项清零（07 §3.6 清单——"补一个 win preset 就能编"不成立）。**2026-09-30 第 0 批处置**：macOS push 门禁 workflow 落 `.github/workflows/ci.yml`（每日回归/性能基线门禁与 Windows runner 仍待）；07 §3.6 五条阻断全数清 + `win` preset 入 CMakePresets——**真机 Windows 编译验证仍待首次**（macOS 侧只能保证不回归）。明细见 [DevLog](../DevLog/2026-09-30-b0-gate-c-and-defect-batch1.md)。**两项尾巴批⑦⑧ 补齐（2026-10-05/06）**：真机编译 W1/W2 ✅（VM 全量构建 + ctest 4/4）；每日回归/性能门禁工具 ✅（批⑧：`Tools/ci-daily.sh` 一键 + 回归第 20 步 fps 门禁；**2026-10-07 用户改拍板手动档**——定时撤、工具留，09 §9）。
> **排序约束（2026-09-30 用户拍板）**：M6c 音频先于 M7a 开工。
> **2026-10-07 重排（用户拍板）**：M7b 发行侧后移至 M9 之后（引擎与编辑器功能优先——出包能力已具备、发行未启动）；新增档位 M7c 接续 M7a 执行，新序 = M7c → M8 → M9 → M7b。见文首注记与 [DevLog](../DevLog/2026-10-07-roadmap-reorder-m7b-deferred.md)。

### M8 光照与打磨（4 周）

光照裁剪版（法线 RT + 点/锥光 + blob 阴影）→ 后处理链（bloom/冲击波/色调）→ 性能终测（压测 A + **2h soak 长时稳定**：内存曲线平/零崩溃，崩溃转 dump 体系登记）→ 文档站 v1（使用手册，yami Documentation 对标的最小版）→ incremental 模板（若余量，否则移 v1.1）。
**验收**：压测 A 全绿；光照默认关闭零成本；soak 达标。

## 3. 性能验收场景（进 `Samples/`，CI 冒烟）

| 场景 | 构成 | 判据 |
|---|---|---|
| bench-mow | 10 万实例化精灵 + 5 万粒子，无逻辑 | M1 起：≥ 60fps（基线 **107fps**，2026-09-18） |
| bench-sim | 1 万怪全系统模拟，无渲染 | M2 起：≤ 8ms/步（基线 **avg 5.10ms** 多线程 / 19.9ms 单线程诊断档，2026-09-19） |
| bench-survivor | vs-survivor 模板 + 导演拉满（capAlive=上限） | M5 起：≥ 45fps（编辑器内；口径 = `lemon-editor --bench-survivor`，09 §6.10。2026-09-22 建场基线 28~30fps FAIL → 同日性能批修视口层 O(N²) 差集 + 万级标签全画，**58fps PASS**；同日批⓪ 战斗化（玩家弹幕 20 发/s，命中/击退/击杀全链路）**86fps PASS**；同日批① 成长化（宝石 Spawner 40 颗/s + 磁吸/拾取/XP/升级全链路）**61~64fps PASS**，Pickup 密核扫描余量观察项见 09 §6.10；2026-09-23 批② 导演化（BenchDirector 3 波 ×4 条目直接出生 + WaveStart 事件，Spawner 闸 8000 让位、teamAlive 顶满 capAlive 10000）**66fps PASS**，Director avg 0.068ms）；2026-09-23 批③ 动画化（BenchMob 带 Animator2D，程序化 4 帧表经真实切片/clip 通道导入，万怪帧映射 anim 证据 10003/10003）**58~67fps PASS**，Animator avg 0.128ms；同日素材包第一批入库 `Samples/Assets/yami-dungeon/`（yami MIT，5 表 + 3 clip + THIRD_PARTY 登记） |
| 压测 A（终验） | 00 文档 §4 全量 | M8：≥ 60fps |

CI 每日跑 bench-mow/bench-sim，数字写入构建报告（性能回退 > 10% 自动标红）。
> **落地口径（M7a 批⑧ 2026-10-06，09 §9；2026-10-07 修订）**：门禁工具 = 本机
> `Tools/ci-daily.sh` 手动一键（mac 全量回归第 20 步 = **bench-survivor fps ≥ 76.5**
> （基线 85×0.9）+ bench-mow/sim 数字随报告留档 `build/ci-reports/`）+ GitHub win
> job（逻辑面）。**用户拍板手动档**：定时（launchd/workflow schedule）均不启用，
> 恢复配方留档批⑧ 批文件；GitHub macOS runner 10× 计费不跑 GPU 面——性能门禁
> 只在固定硬件（本机）数字可比。
> **排期锚点（2026-09-24 登记）**：CI 的具体口径与门禁见 [09 §9](./09-Testing.md)——落地时点 = M7 开工前（Gate C 前置）；此前本节与风险 #2/#7 对 CI 的引用均为"未排期的假设"，登记后以 09 §9 为准。

## 4. 砍单顺序（范围蔓延防线，按序砍、砍前记录 ADR）

1. **可视化事件树编辑器**（yami 已证明最大成本单点；schema 已冻结，随时可后补）
2. **2D 光照/SDF 阴影**（M8 整体；blob 阴影便宜可保）
3. **incremental 模板**（核心系统（大数/离线结算）留钩子，模板移 v1.1）
4. **运行时 UI 升级**（RmlUi/自研，v1 ImGui HUD 足够发布）——**2026-09-28 注记：触发条件已成立（用户游戏富排版 + 文本输入需求），转正式排期 [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（原 M6a 批③ 五子批，2026-09-29 迁 M6b）；本条自砍单序列退役，改由 ADR-014 D7 范围红线治理**
5. **Play 中编辑回灌**（降级为"Stop 后保留 diff 报告"）
6. **macOS 正式支持**（保持开发可用级）
7. **编辑器 C++ 插件机制**（面板注册表编译期版顶住）
8. **LoadScene 完整场景切换**（产品壳档1 单场景状态机满足 M6a 验收即后置；开工前置 ADR）

**不砍清单**（砍了就不是这个引擎）：Vulkan 内核、C# 混合模型、合批性能红线、开箱模板、GUID 资产管线。

## 5. 风险台账

| # | 风险 | 等级 | 缓解 | 触发信号 |
|---|---|---|---|---|
| 1 | Vulkan 学习曲线拖垮 M0/M1 | 高 | M0 就是专门验证；Looper 全源码对照；三角形→实例化两小步走 | M0 三周完不成 W2 |
| 2 | MoltenVK 与 Win Vulkan 行为差异 | 中 | RHI 层隔离；mac 只保开发可用；CI 双平台冒烟 | mac 特有 corruption/崩溃 |
| 3 | C# 桥开销/GC 超预算 | 中 | 三档模型可降档；Tracy 实测；Luma 同构方案已验证 | M3 验收不过 |
| 4 | 单人范围蔓延（编辑器无底洞） | 高 | §4 砍单顺序 + 每里程碑 GUI 验收 + 面板集冻结清单 | 连续两周无里程碑推进 |
| 5 | ImGui DPI/IME 细节坑 | 低 | M4 首周就做中文输入/DPI 冒烟（后置会烂尾） | 编辑器中文无法输入 |
| 6 | 素材制作拖累模板 | 低 | yami 默认素材（MIT）直接作底包（角色/怪物/弹幕/粒子/UI/音频全套），缺口 CC0/自绘补充；素材清单先行冻结 | M5 素材齐备率 < 80% |
| 7 | 存档/数据格式返工 | 中 | schema 版本化 + 迁移链从 M2 就进 CI（老档升级测试）——✅ M2 已落：.scene v1 + 迁移链骨架 + roundtrip/前向兼容单测 | 格式变更未带迁移 |
| 8 | Luma 代码耦合清理成本 | 低 | 只移植清单内文件，逐文件登记 THIRD_PARTY | 编译依赖蔓延 |

## 6. 节奏与追踪

- **周循环**：周一排 3 个以内的周目标（对齐里程碑条目）；周五 GUI 冒烟 + 事件与新基线在 `docs/DevLog/` 新增条目。
- **ADR**：凡推翻本套文档的决策写 `docs/ADR/ADR-0XX.md`（模板：背景/选项/决定/后果），文档正文加"已由 ADR-0XX 修订"标注。
- **版本**：M0 起语义化 `0.x`；M7 后 `1.0-preview` 对外可发。

## 7. 里程碑 ↔ 文档映射（实现时读哪册）

| 里程碑 | 必读分册 |
|---|---|
| M0–M1 | 01（骨架）、02（渲染）、07（移植倒排） |
| M2 | 03（ECS）、06 §2-3（资产 schema） |
| M3 | 04（脚本）、07 §1.1 |
| M4 | 05（编辑器）、06（管线） |
| M5 | 03 §8-11（导演/事件）、06 §7/§10（模板/存档） |
| M6a | 04（多脚本）、05 §7（AnimationEditor）、06 §7/§10（模板/存档） |
| M6b（UI） | 06 §8（UI 恒定原则/触发注记）、ADR-014 |
| M6c（音频） | 06 §2（音频资产类型）、07（第三方登记） |
| M9（Tilemap+TD） | 03 §7（寻路）、05 §7、06 §5 |
| M7 | 06 §6（发布） |
| M7c（引擎功能段） | 02 §7（位图文本红线）、06 §8（Fx 恒定原则）、09 §9（门禁） |
| M8 | 02 §8（光照） |

所有里程碑验收跑测前另读 [09 测试](./09-Testing.md)（bench 用法、专项开关、判读与基线）。
