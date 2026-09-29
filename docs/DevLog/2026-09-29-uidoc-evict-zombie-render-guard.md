# 2026-09-29 · UIDocument 删除逐出——僵尸渲染双防线（批③d 前置后续）

## 事件

真人验收②二轮：用户截图实证**红字「UIDocument：资产缺失或非 .rml（guid b4147ff…）」与
.rml 面板同屏共存**（Play 中、HUD 计时 0:03 = 活帧）。一轮的"RtUi HUD 误认"解释被推翻，
按"删除资产后文档仍渲染（僵尸）"立案。

## 排查（静态全链，均无洞）

| 环节 | 结论 |
|---|---|
| 装载/逐出键 | 三装载点（通道 A / 双击 / 播种）与逐出点统一用 relPath——键一致 |
| `Impl::UnloadDoc` | 有 `doc->Close()`（= context 摘除销毁），非只 Hide |
| `missing` 置位 | 仅 `AssetDatabase::Rescan()` 墓碑分支（同帧必推 `cs.removed`）——无旁路写 |
| Play 路径裸 Rescan | 无（`db.Rescan()` 裸调用仅在 MakePrefabFrom/模板生成/AssetDatabase 内部） |
| 模板 C# | 无 `UI.Show(.rml)`——通道 B resolver 现载排除（resolver 对墓碑返回 false） |

## 机器复现（关键补洞）

既有 smoke 断言 `del=1` 只查 **docs 簿记**（`HasDocument==false`）——**簿记清 ≠ RmlUi
上下文真摘除 ≠ 画面清**。本轮加 `evict2` 双形态 + 像素级断言（帧门 260→400）：

- **形态 A**（Play 中删显示中文档）：212 删 editprev.rml（seed=删前 `IsDocumentShown` 快照）
  → watcher 500ms 轮询 → 逐出 → **253 帧 gameRT 上面板色 #604080 计数必须 ≈0**。
- **形态 B**（编辑态删 → 重进 Play）：302 双击通道重装载 → 304 编辑态删 → watcher 逐出 →
  345 `TryEnterPlay` → 簿记（`HasDocument==false`）+ 348 帧像素双断言。
- 复位链：258 复种 dyn（三局 C# UiRefill 依赖，防 resolver 落空记契约错）+ editprev；
  352 复种 + 394 双击重装载 → 终帧 hasDocC/cPrevN 回归原口径。

**结果：脚本/无脚本 ×3 连跑全绿（`evict2(seed=1 live=0 edit=1 pix=0) => OK`）**——当前
构建的机械链在两种用户形态下都干净。用户观察最可能来自（按可能性序）：旧构建（逐出代码
672d436 之前）、或会话内不可见的项目状态（多次删/建/重启的墓碑交错）。

## 防御纵深（F-B，代码级保证）

`MountSceneUiDocuments` 缺失分支（红字后）追加 `UnloadDocument(墓碑 relPath)`：声明缺失
时，同名残留装载**进 Play 即强制逐出**——无论 watcher 逐出是否达成/竞态如何，"本屏不
装载"的语义现在覆盖**画面**层面。常规路径 docs 已无该名 = no-op 静默；命中时留观测日志
「缺失声明的残留装载已逐出」。

## 断言语义修正（时序搬迁的连带）

三局重进 Play 使通道 A 重装主文档 → `InvalidateContainers` 清 210 帧负面 op 建的容器 →
终帧活读 negN 恒 -1。改 **251 帧快照**（`smokeUiNegP2`）——断言本义不变（负面 op 在其
局内确实建了行），与 dynText/hasDocB 的既有"删除播种后终帧读恒空改快照"口径一致。

## 验证

- smoke-uirml：无脚本 OK；脚本 ×3 连跑 OK（`items=2/1 contract=1 ev=c1r4 … evict2 全位`）
- 回归 full：见提交信息（15/15）
- ctest：3/3

## 关联

- [批文件·后续一轮落账](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)
- [上一条：③d 前置主体](./2026-09-29-m6b-b3d-pre-uidocument-scene-mount.md)
