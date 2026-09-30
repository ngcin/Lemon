# 缺陷评审复核：16 条逐行核实，14 成立 2 证伪

- **日期**：2026-09-30
- **对象**：[架构分析与缺陷评审报告](../Reports/2026-09-30-architecture-and-defect-review.md)（48 条）中标 ✅"主审人亲手实证"的全部 12 条（高 7 + 中 5）+ 抽样 M2，共 **16 条**——即报告可信度的核心骨架。静态核实于 HEAD `32f077b`（报告基线 `3597032` 后仅一个 UI 铺量批提交，涉及代码未变，无 CHANGED 判定）。
- **方法**：5 路并行静态核查（未运行动态复现），每条沿代码路径走到原始行文本。

## 结论

| 类别 | 结果 |
|---|---|
| 成立 | **14 条**（D1–D11 全部 11 条 + M2/M6/M8/M9 中的 4 条抽样） |
| 证伪 | **2 条**（M13、M14，见下） |

报告骨架可信度高；未复核的中/低条目（M1/M3–M5/M7/M10–M12/M15–M26/L1–L14）不在此列。

## 两条证伪（从修复清单划掉）

- **M13"无效句柄 destroyQueue_ 无界增长"**：`Scene.cpp:48-61` `CommitDestroys` 每帧 `pending.swap(destroyQueue_)` 整队取走 + `registry_.valid` 过滤；`DestroyCommitSystem` 装在默认管线 Essential 阶段（`Systems.cpp:1350`）+ 编辑器 `TickEditor/TickPlay` 每 Step 后双保险（`EditorContext.cpp:1072-1077`）。无效/重复句柄只产生**帧内瞬态冗余**，"每帧 Destroy 已毁句柄 → 无界增长"不成立；`Scene.h:8` 幂等承诺（提交时校验）兑现。
- **M14"存档含当帧待删实体"**：编辑器删除路径在**报告基线之前**就已即时提交——`EditorContext.cpp:865-877` `SceneDestroyEntityTree` 后紧跟 `s.CommitDestroys()`，注释即记录该复活 bug 的发现与修复（smoke-ui 真人链路抓的）。且全仓查询层（C# GatherEntity、渲染提取）**统一不过滤** `DestroyQueueTag`、统一靠帧末提交——报告所称"别的层过滤、存档不过滤"的不不对称不存在（`Scene.cpp:29` 那句注释才是失实的）。仅 Prefab Revert 有同 tick 理论残窗（`EditorContext.cpp:823` 不即时提交），不可达。

## 成立条目的措辞修正（不影响定性）

- **D2**：六处调用点"重建成功后不重取不 continue"无条件成立；但向量越界读是**条件触发**（imageCount 恒 clamp 3，需新链图像数 ≤ 旧下标），"未 acquire 即 present"才是无条件违反。
- **D3**：回调增长为**线性**（每设备丢失每 batcher +1）非"指数"；路径应为 `Editor/Interaction/`。
- **D8**："需重启"略过重——退 Play（`HideNonEditDocuments`）可解、修好文件重载可解；**Edit 来源预览文档除外**（`:848` 豁免清场，真恢复不了）。
- **D10**：因 `firstSlot_=3`，越界起点实为**第 62 个精灵页**（槽 64）而非"第 65 个资产"。
- **D11**：真实悬垂路径是 `AssetGpuCache.cpp:143-144` 回滚分支与 `:179` ClearPages；`:86` 热重导分支被 `:90` 重绑自我修复。
- **M9**：代码事实全中（45 个 UCO 仅 5 个带 try；clamp 不对称），但仓内现调用方均未触发，属**潜在**缺陷非现役 bug。

## 新发现（比报告更近）

**D5 的指针失效窗口已被出厂模板踩线**：`Templates/vs-survivor/Game/GameFlow.cs` `PlayerCombat.UpdateBlades`（升级加刃时）在 `TickStartUpdate` 内同步调 `Instantiate.Prefab`——经 `EditorContext::InstantiatePrefabAsset` 同步 emplace 进池，恰好落在"gather 已收指针、Batch.Tick 未跑"的失效窗口内。未崩是因为 EnTT 池扩容摊销 + 刃生成低频。D5 修复优先级建议从评审第三批上提（GameEntry/独立运行时批一并做，运行时侧 Prefab 生成路径本就要在该批定形）。

## 后续

- 修复批次划分按评审 §8 执行，M13/M14 划掉；第一批 + D2 已于同日完成（[批⓪](../Plans/M7/2026-09-30-b0-gate-c-and-defect-batch1.md)）。
