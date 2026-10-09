# 批⑩：编辑器打磨——Play 态 Hierarchy 场景组显示 + DDOL 徽标 + i18n 词条 + smoke-template 双 Play 段钉板

Status: done ✅（2026-10-09 机器面。批⑩ 转正拍板 = 用户 2026-10-09（八裁决点追认同日，[裁决 DevLog](../../DevLog/2026-10-09-m7c-b8-b9-ratify-and-b10-go.md)）；双 Play 段 = 批⑨ 后修登记顺手项（[后修 DevLog](../../DevLog/2026-10-09-m7c-b9-post-fix.md) §登记与后续）。出口判据机器面全落：单测 34,716 逐位不变（引擎内核零改动）/ script-tests 1,818 / ctest 4/4 / 回归 21 步全有绿记录（首跑 20 绿 + smoke-drag 隔离三连绿 = M7a §8 注入抖动家族 09 §9 归因）/ bench-survivor 门禁 fps=82 / 构建零警告 / i18n 20 keys 对齐 / 双 Play 段三跑全绿（帧预算 3400 维持）/ 截图视觉自证（DDOL 组 Flow 带徽 + MainMenu（根 0））。**真人走查待用户**（AGENTS 行动项⑦）。[DevLog](../../DevLog/2026-10-09-m7c-b10-editor-polish.md)。下一步 = M7c 收尾转 M8）

- 日期：2026-10-09
- 关联：[M7c.md](./M7c.md) 批⑩ 行 · [SceneMembership.h](../../../Engine/ECS/SceneMembership.h)（头注「编辑器按位分组显示归批⑩」预留位）· [b9 批文件](./2026-10-09-b9-svr-test-multiscene.md)（svr-test 多场景 = 本批可视化的对象形态）· [b9 后修](../../DevLog/2026-10-09-m7c-b9-post-fix.md)（PlayResetHook = 双 Play 钉板的对象）
- 性质：批⑥–⑨ 换场体系（SceneMembership/SceneSwitcher/SDK 门面/消费者迁移）的**编辑器可见性收口**——Play 态多场景/DDOL 在 Hierarchy 不可见（平铺单列表），用户日常开发 svr-test（3 场景 + DDOL GameFlow）即刻可感；双 Play 钉板 = b9 后修「Stop→再 Play 全灭」缺陷的机器回归防线（PlayResetHook 自清链）。

## 1. 现状核实（2026-10-09 探测）

| # | 事实 | 对本批的含义 |
|---|---|---|
| 1 | `HierarchyPanel.cpp` 对 SceneMembership **零感知**：单 `ctx.ActiveScene()` 平铺根列表（每帧 `scene.Each` 收根 → 全叶 >256 走 ImGuiListClipper；带父子走递归） | 分组渲染的插入点 = 树区（`OnGui` L192–222 的 filter 分支前）；clipper 性质必须保形（bench-survivor Play 态万级平铺 = 既有受益者） |
| 2 | Play 态数据源 = `playScene_`（单 registry，`EditorContext.h:138`）；换场在组间切换、registry 不换 | 分组按实体 `SceneMembership.scene` 分桶即可，无跨 registry 问题 |
| 3 | 查询面现成：`World::SceneRecord{handle,name,path,isLoaded}`（`World.h:145`，记录序 = 装载序）/ `ActiveSceneHandle()`；`kSceneFlagDontDestroyOnLoad` **位只在根**（批⑦ D1 根位式）/ `kSceneHandleUnassigned=0`；EnterPlay 建档名 = 场景快照 name 段（`EditorContext.cpp:771`，如 "MainMenu"） | 组头文案与排序零新引擎 API；DDOL 组成员判据 = 根自身带位（根无祖先，lineage 检查退化为位检查） |
| 4 | `gameUi_` 跨 Play 常驻、Stop 装载保留（`StopPlay` 只 HideNonEditDocuments，`EditorAppScripts.cpp:304`）；uirml smoke 已有**局中 Stop→再 Play 先例**（`EditorAppSmokeUirml.cpp:200/256` 帧锚直调 StopPlay/EnterPlayProgrammatic） | 双 Play 段可直接在 `SmokeTplSample` 内驱动（同款挂点）；`uiLoadsFinal==7` 断言必须在 Stop 前快照（第二局 code-mount 会累加装载计数） |
| 5 | smoke-template 现状：3400 帧（`editor-regression.sh:139`）；流程链 flowStage 0–11 于 stage 11 收口（tomenuOk）后驻留到帧尾；帧尾采样钩子守卫 `ctx_.Playing()` | 双 Play 状态机住守卫**外层**（Stop 与再进之间的帧是编辑态）；回菜单时点待实测标定（预计 ~2950）→ 帧预算 3400→3700（后修 DevLog 预估） |
| 6 | i18n：`hier.*` 词条住 `Editor/Strings/{zh-CN,en}/hierarchy.json`（17 key，两语言同 key 对齐，`check_alignment.py` 校验）；`trFmt` 带参占位现成 | 新词条 4 key 双语同加；组头带计数用 trFmt |
| 7 | 批⑨ 真人走查复测过（26074 帧零错误）+ D1–D5 已追认；批⑩ 出口判据 = 真人走查（M7c.md 批次表） | 本批无引擎卡点、无用户裁决债；实现微裁决随走查追认 |

## 2. 实现裁决（批⑩ 方向已拍板，微裁决按推荐推进、随真人走查追认）

- **R1 组形态 = Unity 式前置 DDOL 组 + 已装载档案组**：DDOL 根置顶合成组（组头 `DontDestroyOnLoad`）——回菜单后幸存者归置准确（来源组 MainMenu 已卸载，归并显示会误导）；已装载档案组按记录序（装载序）排列，`isLoaded=false` 的记录不显示（异步 staging 期新档天然隐藏）；活动场组头 accent 着色。
- **R2 未指派根显示归并活动组**：Play 中编辑器新建实体无 membership（`ctx.CreateEntity` 不打标）——显示面归并活动场组（Unity 心智模型；生命周期本质不变：下次 Single 换场与该组同被清）。无活动档案或未知句柄 → 尾置「未分组」兜底组（防御显示，运行时装载路径不应出现）。
- **R3 编辑态零改动**：分组渲染仅 Play 态（`ctx.Playing() && filter_.empty()`）；编辑态平铺与过滤路径逐字节不变（smoke-ui/拖拽链不受波及）。Play 态过滤搜索保持平铺（跨组过滤，分组噪声）。
- **R4 clipper 保形**：组内全叶且 >256 根 → 该组走 ImGuiListClipper（bench-survivor 万级平铺的既有防线；分组不降级性能）。
- **R5 双 Play 钉板口径 = menu 再显**：Stop（快照 uiLoadsFinal）→ 再 Play → 菜单文档再显即 PASS——正是 b9 后修缺陷（Booted 用户静态跨局存活误杀新种子）的机器可断言面；GameFlow 种子在场性由菜单再显蕴含（无流程壳 = 无菜单）。

## 3. 任务清单（文件/行级）

| # | 文件 | 内容 |
|---|---|---|
| T1 | `Editor/Panels/HierarchyPanel.cpp` | `DrawPlaySceneGroups`：根收集（复用 rootCache_，同判据）→ 按 membership 分桶（DDOL 位根置顶 / 档案句柄 → 记录索引 / 未指派 → 活动组或兜底组）→ 逐组「组头（trFmt 计数 + 活动场 accent）+ 行集（组内 clipper 保形 R4）」；树区入口改三分支（R3） |
| T2 | 同上 + `Editor/Panels/BuiltInPanels.h` | `DrawNodeRow` 行尾 DDOL 小徽（drawlist 右对齐圆角小块 + "DDOL" 字样，accent）；`DrawPlaySceneGroups` 私有声明 |
| T3 | `Editor/Strings/zh-CN/hierarchy.json` + `Editor/Strings/en/hierarchy.json` | 4 key 双语：`hier.group_ddol` / `hier.group_unassigned` / `hier.scene_header`（{0} 场景名 {1} 根数）/ `hier.ddol_badge`；`python3 Editor/Strings/check_alignment.py` 对齐校验过 |
| T4 | `Editor/App/EditorAppSmokeTpl.cpp` | `TplSmokeState` 增 replay 段字段；`SmokeTplSample` 守卫外层增双 Play 状态机（0 等 flowStage==12 驻留 90 帧 → 1 Stop（先快照 uiLoadsFinal）→ 2 再 Play → 3 等菜单再显 → 4 完；失败吸收 9）；flowStage case 11 记 `tomenuAt`；verdict：replay 位入 tplOk + printf 段 `replay(stop/enter/menu2)`；uiLoadsFinal 末读仅在双 Play 未运行时（快照为准） |
| T5 | `Tools/editor-regression.sh` | 帧预算标定（预估 3400→~3700）。**实测结论：3400 维持不变**——3800/3400/3400 三跑全绿（replay 位全 YES、uidoc=7 快照生效、play-roundtrip byte-exact=YES），流程链全帧锚定无墙钟竞速（18ms 帧率下限已保 FileWatcher 窗），旧预算余量足够吞下双 Play 段（~180 帧：驻留 90 + Stop/再进 2 + 菜单再显 ~15） |
| T6 | （验证） | 构建零警告 + 单测 34,716 只增不减（引擎零改动 = 逐位不变预期）+ script-tests 1,818 + ctest 4/4 + 回归 full 21/21（smoke-template 双 Play 段首跑绿）+ bench-survivor 门禁不降（clipper 保形实证）+ zh/en 双语言 smoke（i18n 新词条两态） |
| T7 | （回写） | DevLog 新条目 / M7c.md 批⑩ 行勾销 + 段尾方向句 / AGENTS.md 状态行与待用户清单（真人走查项）/ 批文件 Status 翻 done |

## 4. 出口判据（对齐 M7c.md 批⑩ 行）

- 真人走查：编辑器开 svr-test MainMenu.scene 进 Play → 开局换 Grass 后 Hierarchy 组头切换（MainMenu 消隐 / Grass + DontDestroyOnLoad 两组可见、GameFlow 带 DDOL 徽标）→ 回菜单组头复原；中英两语言各过一遍组头文案；
- smoke-template 双 Play 段机器钉板绿（stop→replay→menu 再显入回归第 5 步判定）；
- 回归 full 21/21 + 单测 34,716 只增不减 + ctest 4/4 + 构建零警告 + bench-survivor 门禁 fps≥76.5 不降。

## Review 轮（2026-10-09 提交 b117e50 后全量自查）

- **F1（P3 健壮性，当日修）未装载记录句柄实体静默隐没**：分桶 recIndexOf 不查 isLoaded，而渲染层跳过未装载记录 → membership 句柄指向未装载记录的实体从 Hierarchy 消失，违背兜底组「防御显示」意图（R2）。合法流程到不了该态（Single 清场同帧销毁非 DDOL 实体；staging 实体在独立 registry；DDOL 先于句柄判断入组），属异常态显示防线。修 = `loadedIdx`（只认已装载记录；活动组归并同守卫），未装载/未知句柄一律落兜底组可见。验证 = 构建零警告 + smoke-template 全绿（replay 三位 YES / uidoc=7 / play-roundtrip byte-exact=YES）。
- 确认面（核过无缺陷）：trFmt 临时 string 全表达式内消费；SceneRecord 指针绘制期不建档（装载期建档、面板期只读 = World.h F4 契约注记面）；DDOL 徽标绘制点与 TreeNode 间零 item 提交（GetItemRect 系列仍指树行）；双 Play 状态机一帧一步转换/失败吸收 9/预算截断=帧锚定确定性红；第二局残留一次性标志位全被守卫（flowStage 12 无 case、layerStage 6 收口、click 冷却、催命段全 consumed、Steer 站桩注入菜单态无害）；Stop 期 EventSink 随旧世界弃（第二局计数不需要）；saves 双 Stop 幂等重写（settings 值不变——回归 saves 段实证）；编辑态路径逐字节不变；行内拖放 mid-iteration 暴露面与编辑态既有形态一致（非新增）；性能 = recIndexOf O(roots×记录数·个位) + 组内 clipper 保形 + bench fps=82 实证。
- 观察项（不动）：O1 徽标与超长实体名可能视觉重叠（SpanAvailWidth 行满宽，Unity 同款形态）；O2 "Assets/UI/main.rml" 字面量在内层 kMainDoc 与外层 replay 块重复（文件既有 kHudDoc/kHudDoc2 先例，不抽）；O3 未指派归并活动组会掩盖引擎侧漏打标（R2 已裁决维持：常见态 = 编辑器合法新建，运行时路径「零未打标」另有 smoke 断言面）。

## 5. 红线自查（开工前对齐）

- **Engine 内核零改动**（本批全部落在 `Editor/` + `Tools/`；vtable 59 / 组件 id / 系统序零动 → 金回放零重录预期，同批⑨ 口径跳过实测注明依据）。
- 编辑态 Hierarchy 行为逐字节不变（smoke-ui/拖拽/重命名链 = 既有回归面）。
- 模板 GUID 纪律：本批不碰 .prefab/.meta。
- smoke-template 断言只增不改既有位（uiLoadsFinal 语义不变——快照时点前移，断言值不变）。
