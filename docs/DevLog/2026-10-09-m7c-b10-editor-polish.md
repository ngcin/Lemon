# M7c 批⑩：编辑器打磨——Play 态 Hierarchy 场景组 + DDOL 徽标 + smoke-template 双 Play 钉板

- 日期：2026-10-09（裁决落账同日开工；批⑩ 转正 = 用户拍板，[裁决 DevLog](./2026-10-09-m7c-b8-b9-ratify-and-b10-go.md)）
- 关联：[批⑩ 批文件](../Plans/M7c/2026-10-09-b10-editor-polish.md) · [b9 批文件](../Plans/M7c/2026-10-09-b9-svr-test-multiscene.md) · [b9 后修 DevLog](./2026-10-09-m7c-b9-post-fix.md)（双 Play 钉板对象）· [SceneMembership.h](../../Engine/ECS/SceneMembership.h)（「编辑器按位分组显示归批⑩」预留位）
- 性质：批⑥–⑨ 换场体系（SceneMembership/SceneSwitcher/SDK 门面/消费者迁移）的**编辑器可见性收口**——Play 态多场景/DDOL 此前在 Hierarchy 不可见（平铺单列表）；双 Play 钉板 = b9 后修「Stop→再 Play 全灭」缺陷（PlayResetHook 自清链）的机器回归防线。

## 实现面

- **T1 场景组渲染**（`HierarchyPanel.cpp` `DrawPlaySceneGroups`，仅 `ctx.Playing() && filter_.empty()` 走入；编辑态/过滤态路径逐字节不变）：根收集复用 rootCache_ 同判据 → 按 membership 分桶——**DDOL 根置顶合成组**（位在根，批⑦ D1 根位式；根无祖先 = 位检查即 lineage）→ **已装载档案组按记录序**（`SceneRecord.isLoaded=false` 不显示 = 异步 staging 期新档天然隐藏；活动场组头 accent 着色）→ 未指派根显示归并活动组（Play 中编辑器新建实体无打标——Unity 心智模型，仅显示面语义）→ 未知句柄兜底组（防御显示）。**clipper 保形**：组内全叶 >256 走 ImGuiListClipper（与编辑态平铺同阈——bench-survivor 万级 Play 态受益者不降级）。
- **T2 DDOL 行徽**：`DrawNodeRow` 行尾右对齐圆角小块 + "DDOL" 字样（accent 半透底 + accent 字，drawlist 直绘零布局扰动）；过滤态平铺同样生效。
- **T3 i18n**：`hier.group_ddol` / `hier.group_unassigned` / `hier.scene_header`（{0} 场景名 {1} 根数，trFmt）/ `hier.ddol_badge` 四 key 双语同加（hierarchy.json 17→20 key），`check_alignment.py` 20 keys 对齐过。
- **T4 双 Play 钉板**（`EditorAppSmokeTpl.cpp`）：`replayStage` 状态机住 `SmokeTplSample` 守卫**外层**（Stop 后再进 Play 前的帧是编辑态，Playing() 守卫内不可达；帧尾钩子直调 StopPlay/EnterPlayProgrammatic = uirml 冒烟 frame 200/203 局中往返同款先例）——0 等流程链收口（flowStage==12）驻留 90 帧 → 1 Stop（**先快照 uiLoadsFinal**：gameUi_ 跨 Play 常驻装载保留，第二局四屏预装会累加、uidoc==7 断言以第一局末态为准）→ 2 再进 Play → 3 等菜单再显（b9 后修缺陷面：守卫自毁 = 无流程壳无菜单——菜单再显即 PlayResetHook 自清链端到端机器证）→ 4 完；任一步失败吸收 9。verdict 增 `replay(stop/enter/menu2)` 位入 tplOk；uiLoadsFinal 末读仅在双 Play 未运行时。

## 帧预算标定

预估 3400→~3700（b9 后修登记），**实测 3400 维持不变**：3800/3400/3400 三跑全绿（replay 位全 YES），流程链全帧锚定无墙钟竞速（18ms 帧率下限已保 FileWatcher 窗），旧预算余量足够吞下双 Play 段 ~180 帧（驻留 90 + Stop/再进 2 + 菜单再显 ~15）。

## 验证

- **引擎内核零改动**（全部落 `Editor/` 三文件 + `Editor/Strings/`；vtable 59/组件 id/系统序零动）→ 单测 **34,716 逐位不变** / script-tests **1,818** / 金回放零重录预期（同批⑨ 口径跳过实测注明依据）；
- ctest 4/4；回归 full **21 步全有绿记录**——首跑 20 绿 + smoke-drag 一红（回归内 retry 双红 = updates=34/scale 2.57 异常态），**隔离复跑三连绿**（updates=28/scale 1.61 稳态）= M7a §8 已登记注入抖动家族（smoke-drag/smoke-ui 同族），09 §9 环境归因非本批回归；本批改动面（Hierarchy Play 分支 + smokeTemplate 专属状态机）与 smoke-drag 注入链零交集；
- bench-survivor 门禁 **fps=82 ≥ 76.5**（分组渲染不拖慢 Play 态面板——clipper 保形实证）；
- 构建零警告（变更 TU 强制重编复核）；
- **截图视觉自证**（smoke-template 末帧 = 双 Play 第二局菜单态）：DDOL 组 = Flow 实体（带蓝色 DDOL 徽标）、`MainMenu（根 0）` 组头——换场清场语义（run 实体全灭、DDOL 幸存者独存）在编辑器的准确呈现。

## 待用户

- 真人走查（AGENTS 行动项⑦）：编辑器开 svr-test `MainMenu.scene` 进 Play → R 开局换 Grass 后 Hierarchy 组头切换（MainMenu 消隐 / Grass + DontDestroyOnLoad 两组、GameFlow 带徽标）→ 死亡/重开/Esc 回菜单组头复原 → 火山同链；Window→语言 切 English 过一遍组头文案（DontDestroyOnLoad (persists) / ({1} roots)）。
