# 批⑥c：回放扩展小批——多次换场/DDOL 轨迹用例 + 金回放零重录终验 + 03 分册注记收口

Status: done ✅（2026-10-08 全过——单测 **34,560**（+36）/ ctest 4/4 / 回归 full **21/21 首跑全绿** / bench 门禁 **fps=87/88** / **金回放跨版本三档 mismatches=0**（96dfea8 worktree 录档 → 工作树回放；sim 终态 alive=8249/created=16003/destroyed=7754 逐项一致）/ 构建零警告——**批⑥ 整体收口**（⑥a+⑥b+⑥c），[DevLog](../../DevLog/2026-10-08-m7c-b6c-replay-extension.md)。操作性发现：worktree configure 须显式 `CPM_SOURCE_CACHE=~/.cache/Lemon-CPM`（否则 CPM 在线 clone 挂代理坑））

- 日期：2026-10-08
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md)（换场帧协议 / 确定性契约）· [b6 批文件](./2026-10-08-b6-scene-membership-core.md)（查② 哈希流口径）· [b6b 批文件](./2026-10-08-b6b-scene-switch-orchestration.md)（设计定案② 池序偏差落账承诺）· [M7c.md](./M7c.md) 批⑥ 行
- 性质：批⑥ 收口切片——**纯测试 + 文档 + 验证批，零引擎代码改动**（金回放零重录预期的充分条件：vtable 49 槽 / 组件 id / 系统注册序全不动）。
- 开工裁决（用户拍板 2026-10-08）：① 金回放录档基线 = **m7c-b4（96dfea8）**——正好覆盖批⑥ 全部引擎 delta（⑥a+⑥b），对齐"批⑥ 金回放零重录"收口判据（备选 m7a-b2 全谱系/双跑未取）；② ⑥c 收口后停下汇报，批⑦ 开工前单独对齐（vtable 尾加敏感面 + ⑥b 登记两项前置）。

## 形态定案（开工前）

- **多次换场轨迹用例 = 单测孪生世界锁步**，不进 bench-sim harness：跨版本金回放的录档侧是旧构建，旧构建没有新代码路径——把换场塞进 bench-sim 录放流自相矛盾；换场进真正的回放流（op 记录）归批⑦ C# LoadScene op（b6 批文件查② 既定口径）。⑥c 的"回放轨迹"证明 = 同请求序列 ⇒ 同状态轨迹（引擎面确定性）。
- **smoke-scene 维持单跳**：四跳全链归批⑦（C# op 路径 + 事件序断言），⑥c 不预造夹具避免重复。

## 任务清单（文件/行级）

| # | 文件 | 动作 |
|---|---|---|
| T1 | `tests/engine/SceneTests.cpp` | 两测：① `TestSceneMultiSwitchDDOLTrajectory`——四跳（Grass→Volcano→Grass 同名重装→Cave）单世界轨迹：DDOL 幸存者句柄/flags/来源组句柄全程稳定、重装同名场景发新句柄（旧句柄不复活、每载一档）、每跳零孤组、档案 isLoaded 逐跳翻转；② `TestSceneSwitchDeterministicTrajectory`——孪生世界锁步（同 seed + InstallDefaultSystems + 同脚本化帧序列：两次换场 + 初始 DDOL 标记 + 每帧 Fx 灌脏），逐帧 ComputeStateHash 双侧一致 + 场景身份轨迹（逐帧 active 句柄序列）一致 + 终态 DDOL 轨迹一致 |
| T2 | `docs/EngineDesign/03-ECS-Runtime.md` | §2 Scene 语义修订注（执行边界 → 数据分组，ADR-017 D1 落地注记）+ OnDestroy 通知序 = 池序（确定序）落账（ADR"逆创建序"措辞按确定性意图收口——改池序 = 战斗销毁 RNG 消费序变 = 金回放重录红线）+ membership 三不入纪律（不入注册表/序列化/StateHash）；§12 补换场确定性一行（引擎面同请求序列 ⇒ 同哈希轨迹；op 入回放流归批⑦） |
| T3 | `docs/ADR/ADR-017-Scene-Management-And-LoadScene.md` | 换场帧协议 ① 行内修订注（"逆创建序" → 池序 = 确定序，指向 03 §2 落账） |
| T4 | （执行项，非代码） | 金回放跨版本三档终验：`git worktree` 检出 96dfea8 独立构建（CPM 缓存 + ccache）→ 录三档（bench-sim st `--threads 1` / mt 默认线程 / bench-script）→ 工作树构建（含 ⑥a+⑥b）回放三档 → `replay=PASS mismatches=0` ×3；worktree 清理 |
| T5 | （复验） | 构建零警告 / lemon-tests 全绿 checks 只增不减（基线 34,524）/ ctest 4/4 / 回归 full 21/21 / bench-survivor 门禁 fps ≥76.5 |
| T6 | （回写） | DevLog 新条目 `2026-10-08-m7c-b6c-replay-extension.md` / M7c.md 批⑥ 行收口（⑥c ✅ = 批⑥ 整体收口）/ AGENTS.md 状态行 / b6、b6b 批文件 Status 行补 ⑥c 勾销 |

## 红线自查（零引擎代码改动的直接推论）

- vtable 49 槽不动、组件 id 不动、系统注册序不动 → 金回放零重录预期成立的充分条件；
- membership 不入哈希流（⑥a 查② 定案 + TestStateHashIgnoresMembership 钉住）；
- `NotifyPendingDestroys` / `CommitDestroys` / `Destroy` 本体零触碰（⑥c 只读消费）。

## 出口判据（批⑥ 整体收口）

T1 两测全绿 + checks 基线 34,524 只增不减 + ctest 4/4 + 回归 full 21/21 + bench-survivor 门禁不降 + **金回放跨版本三档 mismatches=0**（b4 录档 → 工作树回放）+ 构建零警告。

## Review 轮（2026-10-08 收口后，全量未提交 delta = ⑥b+⑥c）

结论：无阻断缺陷；抽查确认面 = Execute 重入安全 / notify→commit 序（OnDestroy 恰好一次双保险）/ 档案收口先于 afterBuild / hooks lambda 生命周期 / UnloadDocumentsByOrigin key 指针稳定（unordered_map 节点稳定 + 卸载期无插入）/ ValidateParse 零副作用 / playWorld_ 每会话重建档案不累积 / EcsTests 系统序断言真实 / 三处 spawn 消费无绕打标残留 / 纪律面（Vulkan·第三方·vtable·RNG）全守。处置五项：

- **F3（当日修，注释一行）**：`SceneSwitcher.h` newHandle 头注"ParseFailed 时 = 0"以偏概全——BuildInto 极端失败子路径已建档非 0；改准确措辞（零行为差异）。
- **F1（登记批⑦ 前置③）**：组 0 口径不对称——`CountSceneGroup(0)` 计无组件实体 / `QueueDestroySceneGroup(0)` 不收；正规路径零未打标 + smoke orphan 断言可抓 = 暗坑非活洞。
- **F2（登记批⑦ 前置④）**：DDOL 根下后挂子实体换场被清父幸存（Unity 随根幸存）——`Instantiate.Prefab` 落点设计时裁决是否继承父组。
- **F4（观察项不动）**：`SpawnPrefab` 每次 CollectTree 堆分配——sim avg 5.851→5.853ms 噪声级、bench-survivor 87/88 属当日方差带；批⑧ 大规模装载压测显现再优化。
- **F5（命名细微不动）**：`rep.ddolSurvivors` = 全场景 DDOL 计数（多跳累积），头注语义自洽。
