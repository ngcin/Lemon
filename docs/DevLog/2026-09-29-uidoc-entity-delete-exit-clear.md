# 2026-09-29 · UIDocument 实体删除残留——退 Play 清场 + 归位判据升级（形态 D 收口）

## 事件

真人验收（`demo/ui-test`，模板新建项目）实报：Scene 中建 UIDocument 挂 `Assets/UI/cards.rml`
→ Hierarchy 删除该实体 → 重进 Play **cards.rml 仍显示**。

## 根因（跨 Play 残留第三形态）

同日已收口的两形态（资产删除墓碑/事件吞噬，见前两条 DevLog）均不覆盖本路径——**资产健康，
实体没了**。僵尸链：

1. 声明装载显示：通道 A `LoadDocumentFromFile` + `ShowDocument(showOnStart=1)`——**不置
   `shownDuringPlay`**（stale 位只认 ApplyOps 的 C# Show，③d 前置 §3 口径）；
2. `ExitPlay` 对 UI 文档零处理（当时有意设计"归位统一在下次 EnterPlay"——但归位判据没接住）；
3. 删实体后重进 Play：装载扫描无实体 → 不在 declared；`ResetDynamicDocuments` 走
   `!shownDuringPlay → continue`（本意 = ③b Edit 双击装载保持）→ **Scene 来源显示中文档
   撞进同一分支被放行** → 僵尸。

病根：`Doc` 记录不区分装载来源——"Edit 双击预览保持"与"场景声明装载归位"两个语义在
状态上不可区分（③d 前置审核 C5 当年以 stale 位消歧，覆盖了 C# 来源、漏了 Scene 来源）。
**R10 登记的"装载记录带来源标记"正是此坑的预言**。

## 修复（用户拍板：Unity「Stop = 运行时态归零」同构 + 对账兜底，双防线）

- **来源标记（R10 启用）**：`UiDocOrigin { Scene / CSharp / Edit }`——
  `LoadDocumentFromMemory/FromFile` 尾参（默认 Scene）；装载点三处对齐：Mount（Scene）、
  双击装载与 smoke 播种（Edit）、通道 B resolver 现载（CSharp）。重载不改来源。
- **防线一（退 Play 清场）**：`UiSubsystem::HideNonEditDocuments()`——非 Edit 来源全部
  Hide（装载保留免 IO）+ 清 stale。挂 `EditorApp::StopPlay()` 聚合（ExitPlay 成功后），
  6 处 Stop 路径（交互 ×3 / smoke ×2 / --play 收尾）统一走此口。
- **防线二（EnterPlay 归位判据升级）**：`ResetDynamicDocuments` 判据 stale 位 → origin——
  非 Edit 未声明 → Hide（装载保留）；Edit 豁免（③b「跨 Play 保持」承诺不断）。stale 位
  降为观测位。两防线幂等共存（对账范式延续）。

## smoke（形态 D，两段——各防线独立验收面）

- **一段 405-420**：405 Stop（经 StopPlay）→ `stopHide`（Scene 文档即刻不显示 + Edit
  预览豁免同帧验证）；407 编辑态删声明实体；410 第四局 → `delEnt`（装载保留 + 不显示 +
  零装载增量；断言先于当帧 C# 全模式安全）；412 `pix`（411 渲染块 #802040 计数 <5，仅
  无脚本——脚本模式 C# 帧 1 经通道 B 合法拉回 = 动态屏非僵尸）；415-420 重建实体进局。
- **二段 421-430**：421 **直调 `ctx_.ExitPlay()` 绕过 StopPlay 清场**（= 清场被跳过/失效
  窗口）→ `resetSeed`（uirml 仍 shown = 防线一确不在场）；422 删实体；424 第六局 →
  `resetOnly`（Reset 的 origin 判据必须独自清场——防线二独立面）。
- **阴性验证（双面实抓）**：禁用 HideNonEditDocuments → `stopHide=0 FAIL`（delEnt 仍过
  = 防线二互备活证）；Reset 判据回旧 stale 口径 → `reset=1/0 FAIL`（一段全绿 = 防线一
  遮蔽效应，二段独立面补位）。
- 装载计数对账：无脚本 loads=9、脚本 loads=10（第三局 C# 重灌 dyn +1）——第四/六局零装载。

## 连带

- 帧门 400 → 470（`--smoke-uirml` 下限同步）；回归新增**第 16 步** `uirml-noscript`
  （形态 D 像素防线常驻——脚本模式 C# 拉回使 pix 位不可裁决，无脚本模式独立成步）。
- 真人验收④回执通道：修复后用户可在 ui-test 复验原操作序列（挂 → Play → 删 → Play）。

## 验证

- smoke-uirml：脚本 ×3 + 无脚本 ×1 全绿（`evict3(stopHide=1 delEnt=1 reset=1/1 pix=…)`）
- ctest：3/3；回归 full：16/16（见批文件落账）
- 阴性验证：两防线分别破坏各红一次（上）

## 关联

- [批文件·后续三轮落账](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)
- [上一条：根因收口（事件吞噬 → 状态对账）](./2026-09-29-uidoc-evict-root-cause-reconcile.md)
- [僵尸渲染双防线](./2026-09-29-uidoc-evict-zombie-render-guard.md)
