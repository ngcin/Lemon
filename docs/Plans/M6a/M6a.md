# M6a 实施计划 —— 玩法完善 + 幸存者产品化（08 §2，2026-09-24 重排）

Status: planned

> 拆分自原 M6（动因与映射见 [08 文首重排注记](../../EngineDesign/08-Development-Roadmap.md)）；里程碑总览页：每批一个文件，开工前分解到文件/行级，完工后批文件内勾销；事件流水与实测数字记 [DevLog](../../DevLog/)。

## 并行游戏线（本里程碑的成立前提）

用户幸存者游戏以 `demo/svr-test` 为工作项目**即刻开工，不待本里程碑完工**：内容厚度（武器池/敌人变体/升级池/精英与 Boss 掉落）随时堆；撞引擎卡点 → DevLog 新条目登记 → 按下方批次回灌。本里程碑的 GUI 级验收 = 该游戏全流程（08 §2 M6a 验收①）。

## 批次文件

| 批 | 文件 | 主题 | 状态 |
|---|---|---|---|
| ⓪ | [2026-09-24-b0-multiscript-guid](./2026-09-24-b0-multiscript-guid.md) | 架构地基：`scripts[]` 多脚本 + sprite 引用 GUID 化 | **done**（2026-09-24；出口判据 5/6/7 本批落账 ✅） |
| ① | （开批新建） | 表现打击感：Animator `Play`/`CrossFade` + 位图数字/飘字/世界血条 | planned |
| ② | （开批新建） | 内容生产：配置表外置 ADR + AnimationEditor 最小版 + 技能路径数据化 + 存档分档 | planned |
| ③ | （开批新建） | 产品壳：通用模态面板通道 + 流程状态机（档1 单场景零引擎改动）+ LoadScene 评估（档2 ADR） | planned |

批次顺序理由：批⓪ 先行（多脚本与 GUID 晚做返工面最大）；批①–③ 可按游戏侧卡点紧迫度微调次序（如游戏先需要配置表可提前批②）。

## 出口判据（08 §2 M6a 验收①–⑦）

1. 用户项目全流程零 C++：主菜单 → ≥10 分钟一局 → 死亡结算 → 重开/回菜单；
2. 新增 1 武器 + 1 敌人变体纯 prefab/C#/配置表落地（引擎零改动演示）；
3. 动画状态（受击/攻击/死亡）Play 中可切换；
4. 飘字/世界血条开启下 bench-survivor ≥ 45fps（或逐项开销入 09 §6.10）；
5. GUID 稳定性 smoke 断言（资产改名/移动 + 删 manifest 重开，引用不错位）——**✅ 批⓪**（`--smoke-guid` 入回归第 14 步）；
6. 模板 PlayerBehaviour 拆移动/战斗/HUD 三脚本 + `scripts[]` roundtrip——**✅ 批⓪**（模板重生成入库 + TestScriptBoxArchive）；
7. 回放零重录（尾加字段口径）或按 09 §7 推论显式声明重录——**✅ 批⓪**（m5b2 三档 mismatches=0，零重录先例三落 09 §6.8）。

## 登记项（观察，不扩 scope）

- 手柄输入：Steam 发布目标确认 → 批③ 追加最小 Gamepad 输入位；
- 本地化：v1 中文单语，06 §9 Source Generator 移 v1.1。

## 关联

- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M6a（WBS 与验收全文、文首重排注记）
- [06-Asset-Pipeline](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §7/§8/§10
- M5 余项登记原文：08 §2 M6a 文内 2026-09-23 登记块（①–④ 已并入批次）
- DevLog：[2026-09-24 M6 重排条目](../../DevLog/2026-09-24-m6-replan-survivor-productization.md)
