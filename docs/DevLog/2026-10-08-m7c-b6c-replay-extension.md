# M7c 批⑥c：回放扩展收口（多次换场/DDOL 轨迹用例 + 金回放零重录终验 + 03 注记）

- 日期：2026-10-08
- 关联：[批文件](../Plans/M7c/2026-10-08-b6c-replay-extension.md) · [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) · [⑥a DevLog](./2026-10-08-m7c-b6a-scene-membership.md) · [⑥b DevLog](./2026-10-08-m7c-b6b-scene-switch-orchestration.md)
- 性质：批⑥ 收口切片——纯测试 + 文档 + 验证批，**零引擎代码改动**；完工 = 批⑥ 整体收口。开工裁决（用户拍板）：金回放录档基线 = m7c-b4（96dfea8）；⑥c 收口后停下汇报，批⑦ 单独开工对齐。

## 实测数字（出口判据全过）

| 项 | 值 | 判据 |
|---|---|---|
| lemon-tests | **34,560 checks 全绿**（+36 = ⑥c 两测） | 基线 34,524 只增不减 |
| ctest | 4/4 | — |
| 回归 full | **21/21 首跑全绿**（含 scene-smoke 步） | — |
| bench-survivor | 门禁 **fps=87/88**（两跑；playerHp=275793 逐位一致） | ≥76.5 不降 |
| 金回放跨版本三档 | **mismatches=0 ×3**（详见下节） | 零重录 |
| 构建 | 零编译警告 | — |

## 金回放零重录终验（跨版本三档）

- **形态**：`git worktree` 检出 **96dfea8（m7c-b4，⑥a 前最后一个引擎提交）**独立构建 → 录档 → 工作树构建（含 ⑥a+⑥b 全部改动）回放——批⑥ 引擎 delta 一次全覆盖，正好对齐"批⑥ 金回放零重录"收口判据。
- **三档结果**：sim-st（`--n 10000 --frames 18000 --threads 1`）`replay=PASS mismatches=0`；sim-mt（`--threads 4` 回放 st 档 = 并行确定性，m6c-b2 先例形态）`replay=PASS mismatches=0`；bench-script（`--frames 1800`）`回放 PASS (mismatches=0)`。终态 alive=8249/created=16003/destroyed=7754 录放两侧逐项一致（与 m6c-b2 时代金档同值 = 基线连续性旁证）。
- **结构性依据兑现**：vtable 49 槽不动 / 组件 id 不动 / 系统 21 尾插不消费 RNG（⑥b）+ membership 不入哈希流（⑥a 查② 定案，TestStateHashIgnoresMembership 钉住）——零重录预期成立的充分条件全部落地，本终验为机械证明。
- **操作性发现（跨版本验证标准姿势）**：worktree 全新 configure 时 CPM 未自动吃 `~/.cache/Lemon-CPM` 共享缓存 → 在线 clone 挂网络（本机代理时开时关坑，CPU 0% 挂死状）；显式 `CPM_SOURCE_CACHE=~/.cache/Lemon-CPM` 后 configure 45.5s、build 662 目标（ccache 预热）。后续跨版本验证照此办理。

## 新增用例（SceneTests，+36 checks）

- `TestSceneMultiSwitchDDOLTrajectory`：四跳（Grass→Volcano→**Grass 同名重装**→Cave）——DDOL 幸存者句柄/flags/来源组句柄全程稳定、每载一档句柄不复用（重装同名场景不复活旧句柄）、旧组清场余量精确（DDOL 来源组余 1、其余组全清）、每跳零孤组、档案 isLoaded 逐跳翻转、四档案落位。
- `TestSceneSwitchDeterministicTrajectory`：孪生世界锁步 20 帧（同初始态 + f4/f12 两次脚本化换场 + 初始 DDOL 标记 + 每帧 Fx 灌脏）——逐帧 ComputeStateHash 双侧一致 + 场景身份轨迹（逐帧 active 句柄序列）一致 + DDOL 轨迹一致——"同请求序列 ⇒ 同状态轨迹"（引擎面回放确定性证明；C# LoadScene op 入回放流归批⑦，⑥c 不进 bench-sim 录放流——跨版本录档侧是旧构建，新代码路径进不了档）。

## 文档收口

- [03-ECS-Runtime](../EngineDesign/03-ECS-Runtime.md)：§2 落地注（ADR-017 D1——Scene 执行边界 → 数据分组语义修订 + membership 三不入纪律 + **OnDestroy 池序落账**（ADR"逆创建序"措辞按确定性意图收口，改池序 = 金回放重录红线）+ 换场确定性）；§12 补"换场与回放"条目；§6.8 零重录先例四（**跨构建版本形态首例**——历先例均为同构建现录现放）。
- [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md)：换场帧协议 ① 行内修订注（池序 = 确定序，指向 03 §2 落地注②）。

## 批⑥ 整体收口

⑥a 数据面 ✅ + ⑥b 换场编排 ✅ + **⑥c 回放扩展 ✅（本条）**——批⑥ 出口判据全落：单测 34,435→**34,560**（三切片 +46/+43/+36）/ ctest 4/4 / 回归 full **21/21** / bench 门禁 87/88 / **金回放零重录（跨版本三档 mismatches=0）** / smoke-scene 单跳绿。下一批 = 批⑦ SDK 门面（开工前与用户对齐：vtable 尾加 = 金回放敏感面 + 前置四项——⑥b 登记两项（编辑器 Play 世界 `Switcher().SetHooks` 装配、`instantiatePrefab` 钩子注册者子树打标责任）+ ⑥c review 两项（组 0 口径不对称注记、DDOL 根下后挂子实体语义裁决））。

## Review 轮（收口后同日，全量未提交 delta）

无阻断缺陷。当日修 F3（`SceneSwitcher.h` newHandle 头注注释漂移，零行为差异）；F1/F2 登记 [M7c.md](../Plans/M7c/M7c.md) 批⑦ 前置③④；F4（SpawnPrefab 每次 CollectTree 堆分配——实测噪声级）/F5（ddolSurvivors 命名细微）观察不动。明细见 [批文件](../Plans/M7c/2026-10-08-b6c-replay-extension.md) Review 轮节。
