# 2026-09-24 M6 重排：幸存者产品化前置（路线图修订事件）

## 背景

用户要以幸存者品类开发实际产品（工作项目 `demo/svr-test`，即 M5 批④ vs-survivor 模板实例）。盘点发现原 M6（Tilemap + TD 模板，6–8 周）大半为 TD 专属件（Tilemap/自动瓦片/FlowField/A*/摆塔/波次表编辑器），却把幸存者产品所需的全部通用件一并阻塞到其后：

- M5 余项：`scripts[]` 多脚本（模板以单 PlayerBehaviour 神脚本规避）、Animator `Play`/`CrossFade`；
- 已登记未排期细目（08 §2 M6 的 2026-09-23 登记块 ①–④）：配置表外置、AnimationEditor、技能路径数据化、sprite 引用 GUID 化；
- 06 §8 注记推 M6 的位图数字/飘字/世界血条；06 §10 注记推 M6 的存档分档；
- **完全无排期**的缺口：产品壳（主菜单/暂停/设置/重开——C# 无 LoadScene，引擎无场景切换机制，此前无任何里程碑认领）。

"等 M6 全部做完再开发游戏"既不必要（TD 件与幸存者品类零交集）也不可取（内容厚度本可并行堆）。

## 决策（用户拍板）

M6 拆为三段，原 M6.5 音频前移改号：

| 新里程碑 | 内容 | 周期 | 来源 |
|---|---|---|---|
| **M6a 玩法完善 + 幸存者产品化** | M5 余项 + 登记项①–④ + 打击感 + 存档分档 + 产品壳（档1 单场景状态机零引擎改动；LoadScene 档2 独立 ADR 评估） | 4–6 周 | 原 M6 通用件 + 新登记产品壳 |
| **M6b 音频系统** | 原 M6.5 全部内容 | 2–3 周 | 前移改号（幸存者产品与 TD 双消费者都在其后；M7 packager 依赖不变） |
| **M6c Tilemap + TD 模板** | Tilemap/自动瓦片/FlowField/A*/摆塔 + TD 模板 | 5–7 周 | 原 M6 主体；波次表编辑器并入 M6a 配置表 ADR |

配套决定：

- **并行游戏线**：用户游戏即刻开工，`demo/svr-test` 为工作项目；引擎卡点 DevLog 登记、按 M6a 批次回灌；M6a GUI 级验收 = 该游戏"主菜单→一局→结算→重开"全流程零 C++。
- **ARPG 模板暂缓决策**（2026-09-24 用户）：M6c 后按 RmlUi 升级触发条件（06 §8 v1.x）再议。
- **砍单顺序**追加第 8 条：LoadScene 完整场景切换（档1 满足验收即后置）。
- **登记项**：手柄输入（Steam 目标确认后批③ 加最小位）、本地化（v1 中文单语，Source Generator 移 v1.1）。
- 累计工期 45–54 周 → **48–59 周（约 11–14 个月）**；游戏内容开发并行，日历增量小于工时增量。

## 影响文件

- `EngineDesign/08-Development-Roadmap.md`：文首重排注记、§0 总览表三行、§2 M6a/M6b/M6c 三节、§4 砍单第 8 条、§7 文档映射；
- `EngineDesign/06-Asset-Pipeline-Out-of-Box.md`：audio 行改 M6b；多脚本/位图数字血条/存档分档 → M6a；tower-defense 行/图集跨表打包/模板打包接轨 → M6c；本地化 → v1.1；场景快照入档 → M6c 后；
- `EngineDesign/03-ECS-Runtime.md`：波次表编辑器/Play-CrossFade/过渡路径注记 → M6a；TD 波次消费 → M6c；
- `EngineDesign/05-Editor.md`：数组段精细化 → M6a 批② ADR；
- `EngineDesign/07-Porting-Matrix.md`：TD 相关 7 行里程碑列 → M6c；
- `AGENTS.md`（Lemon）：当前阶段行改为 M6a 指向；
- 新建 `Plans/M6a/M6a.md`（枢纽页，批⓪–批③ 骨架，Status: planned）。

## 不变项

M7/M8 内容与 Gate C 前置（CI + Windows 阻断清零）不变；砍单顺序前 7 条不变；历史文件（Plans/M5 批次文件、ADR-011、DevLog 既有条目）中"挂 M6"字样不回改，按 08 文首映射注记读取。
