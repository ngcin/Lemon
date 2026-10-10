# M7c 段收官（批⑫）—— i18n 越窗收口 + 真人面四项过 + ① 转移交 + 文档落账

2026-10-10 · [批⑫ 批文件](../Plans/M7c/2026-10-10-b12-closeout.md) · [M7c.md](../Plans/M7c/M7c.md)（Status→done）

## 事件

用户拍板「按建议」（2026-10-10）：行动项① 血条正式素材观感复验**转移交**（同 W5 先例——不阻塞收段、素材就绪时同名替换 `Assets/bar_bg|fg.png` 补验、不设期），M7c 即刻收官；⑤+ 候选池唯一代码项（批③ i18n en 态越窗，批④ §4 登记）随收官批⑫ 收口；M8 光照与打磨成为当前活跃段（素材就绪前可开工，两不误）。

### 代码改动（批⑫ 五件，全 editor 侧、引擎零涉及）

- **T1 选帧底行自适应**（`AnimationPanel.cpp`）：meta_hint 提示按「三钮让位后剩余宽」省略截断（逐 UTF-8 码点收缩 + `…`，截断态悬停 tooltip 全文）——en 文案远宽于 zh 的固定布局右溢出根除；三钮右对齐公式不变，模态固定 footer 高预留不动。
- **T2/T3/T4 smoke 语言钉定 + 覆写**：`EditorLaunch::AnySmokeMode()` 单源枚举 + Run 装载处冒烟族钉定 zh-CN（回归不再依赖本机 `~/.lemon/editor-settings.json`——批④ §4 登记的「en 机 row 必红/zh 机全绿不可复现」根除；用户设置档不读写）+ `--smoke-lang <code>` 显式覆写旗标。
- **T5 回归 en 态锚步**：`tools/editor-regression.sh` smoke-anim 步后新增 en 态步（`--smoke-lang en` + 独立自播种夹具 `anim-en`），f80 row 锁（2026-09-28 用户截图越窗报的回归锚）在 en 态常驻——只钉不跑 en 会让该锁失效。回归 full 21→**22 步**。

### 真人面（同日，四项全过）

批⑨ 多场景全流程 + 批⑩ Hierarchy 场景组（[验收一](./2026-10-10-acceptance-m7c-b9-b10.md)）；批② 音频覆写听感 + 批③ i18n 全面板切换（[验收二](./2026-10-10-acceptance-m7c-b2-b3.md)）。行动项① 转移交后待用户清单 = ①（移交）+ ③ W5 + ④ M4.8 细化（均不设期）。

## 验证（机器面）

- 构建零警告（mac）；单测 **34,754** 直跑逐位不变（editor-only 改动）；ctest 4/4。
- **en 态修复实证**：smoke-anim `--smoke-lang en` → `row=YES`、全 verdict `=> OK`、rc=0——批④ §4 登记 en 3/3 必红项转绿；zh 态（默认钉定）`row=YES`、rc=0。
- **回归 full 22 步全有绿记录**：首跑 21 PASS（新增 en 态锚步**首跑绿**）+ bench 门禁一红 = **机器负载噪声定性**（09 §9 家族，批⑦⑧ 先例同款三重证）：
  1. 红跑 fps=73 的 frameMax=942.99ms = **present 933.28ms 单帧停顿**（@309）；隔离复跑 fps=75 = **sim 164.75ms 单帧停顿**——两跑尖刺段互异 = 调度停顿特征，非热路径劣化；
  2. 当时机载 load 5.29 + Chrome renderer 48% CPU（uptime/ps 实证）；
  3. stash 本批五文件 → HEAD 基线同场 **fps=85**；恢复后本批构建负载回落（load 3.67）双跑 **85/85**（frameMax 26.57/20.42ms 无尖刺）——同负载态下两构建同分，本批无性能面。
  - smoke-drag retry 1 过（既有注入抖动家族，M7a §8 登记）。

## 段账（M7c @ 2026-10-07 – 10-10，四日）

批⓪ 工程卫生（干净重建 253s→26.5s）→ ① Fx 表现升级（TTF 烘焙器/贴图血条/延迟条/飘字动效）→ ② per-资产音频参数 → ③ 编辑器 i18n（605 key 双语）→ ④ 飘字池 512 → ⑤ ADR-017 场景管理设计 → ⑥⑦⑧ SceneMembership/SceneSwitcher + SDK 门面 + LoadSceneAsync 分帧管线（金回放跨版本三档零重录；20k 实体压测激活帧 13–16ms vs 同步 373+ms）→ ⑨ svr-test 多场景迁移 + 模板随迁 → ⑩ Play 态 Hierarchy 场景组 → ⑪ 引擎评审修复 65 项（万 Flee 场 12.6×）→ ⑫ 收官。出口：单测 34,402→34,754 / script-tests ~1,802→1,834 / 回归 20→**22 步** / vtable 47→59 / bench 门禁维持 fps≥76.5（负载平态 85）。

## 下一步

**M8 光照与打磨**（08 §3：光照裁剪版 → 后处理 → 压测 A + 2h soak → 文档站 v1 → incremental 模板若余量）；批⑪ 设计债登记块随段按触发条件消费（M4 批量上传 Windows 验证在压测 A 前）。
