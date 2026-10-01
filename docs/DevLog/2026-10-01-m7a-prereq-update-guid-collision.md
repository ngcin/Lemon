# M7a 开工前置核对更新：SpawnerBehaviour 收口 + prefab guid 低 32 位碰撞定位

- **日期**：2026-10-01（M7a 规划出台同日，接续 [kickoff DevLog](./2026-10-01-m7a-kickoff-plan.md) 的"开工前置核对"项）
- **性质**：事实核清 + 讨论基座（无代码改动）；防线定档待用户拍板后落 ADR-016 或批文件。

## ① SpawnerBehaviour 未注册——已收口

- 用户删除了 `demo/test/` 整工程（git 工作树 D×12：`Game/SpawnerBehaviour.cs`、`Game/GameMain.cs`（注册行所在）、Scenes/Assets/project.lemon 等）——该类与其注册本就住在 `demo/test`，svr-test 的 `Game/` 从未包含它。
- 复核（全仓 grep）：svr-test Game 注册面 = GameFlow/RunSweeper/PlayerBehaviour/AllyBehaviour/RedVsBlue/WaveTableLoader/DuelBehaviour/UiEcho/TweenDemo，无 SpawnerBehaviour ✓。`ats/` 工程自带类且已注册（本地定位，非验收对象）✓。编辑器冒烟路径的 `SpawnerBehaviour` 引用（`EditorAppSmoke.cpp`、`TestScript.cs`）是引擎自带的 `--script` 冒烟件，与用户项目无关 ✓。
- **残留一处**：`demo/svr-test/Scenes/ui.scene` Player 实体仍挂 `"script":{"class":"SpawnerBehaviour","guid":0}` 悬空槽（该 scene 系 M4 时代 UI 试验场，不在四屏验收流程 MainMenu→Main 内）。处置归用户：删该 script 槽或整场景退役。

## ② prefab guid 低 32 位碰撞——根因定位

**碰撞对**（svr-test，DevLog 2026-10-01 限幅器批记录的"两条告警"实证）：

| prefab | guid（高32_低32） | 与谁撞 |
|---|---|---|
| `Prefabs/Player.prefab` | `d7e57410_00000001` | `Mob.prefab`（`d7e57100_00000001`） |
| `Prefabs/Director.prefab` | `d7e57410_00000002` | `BossMob.prefab`（`d7e57100_00000002`） |

- **根因不是随机生成器**：`Engine/Core/Guid.h`（splitmix64 + random_device 播种 + 进程内单调计数）碰撞概率 2^-63 量级，设计无缺陷。真因 = **手工"前缀+小序号"式赋 guid**——模板固定 guid 家族 `d7e571_0000000X`（VsTemplateGen 8 件，06 §7"资产 GUID 不重生成"），svr-test 自建 Prefab 沿用同式换了前缀 `d7e5741` 但序号从 1 重新计数；前缀住在高 32 位、序号住在低 32 位，两家族钥匙必然重叠。
- **碰撞域（逐通道核实）**：
  - prefab：`EditorContext::BuildPlayPrefabCache`（`EditorContext.cpp:543` `const uint32_t id = (uint32_t)e.guid`）低 32 位做 map 键，碰撞时 **WARN 后静默取先登记者**（按完整 guid 生成会拿到错的 prefab）。**C# 边界已是全宽**——`Instantiate.Prefab(string guidHex)` → vtable 槽 `instantiatePrefab(const char* guidHex)` 传完整 hex 字符串，截断纯编辑器内部实现 → **键升 u64 零 vtable/SDK 成本**。
  - clip/controller/table：三引擎表（`ECS/{ClipTable,ControllerTable,TableStore}.h`）键即 u32，id 流入组件字段（Animator2D clipId 等）与 C# API 宽度 → 升 u64 触冻结面（组件 schema/vtable 槽宽），**缓议**；clip 另有 byName 字符串通道兜底。
  - sprite：spriteId 是 manifest 单调分配的独立号空间（非 guid 派生），`SpriteOf(guidHex)` 全宽解析，**免疫**。
- **数据修复**：改 Player/Director 两个 `.prefab.meta` 的 guid 为随机值 + 同步改引用处（场景/脚本内 prefab guid 引用）。归用户或代改（待定）。

## 防线候选（定档讨论中）

| 档 | 内容 | 成本 | 建议落点 |
|---|---|---|---|
| A 响亮化 | 装载期低 32 位碰撞 = 红字拒绝进 Play（现 WARN 后静默用错） | ~0.5 天 | 若 B 落则此档自然消失（碰撞语义不存在了），仅留完整 guid 重复检查 |
| B prefab 键升 u64 | `playPrefabCache_` 键 u32→u64（搬运批③ 正好动此代码） | ~0.5 天（含回归） | **批③** |
| C 生成/扫描期唯一性 | 编辑器赋 guid 时查项目内低 32 位唯一、撞则重摇；Rescan/AssetIndex 体检加"低 32 位碰撞"红字 | ~1 天 | **批②**（AssetIndex 正在此批建） |
| D 重生成 GUID 入口 | 编辑器右键"重新生成 GUID"（Unity 同款）+ 引用重写或报红手改 | ~1–2 天 | 登记项（"家族前缀"若是刻意用法则升优先级） |

B+C 组合后：prefab 通道彻底免疫（键全宽）；三表通道由"项目内低 32 位唯一"不变量 + 体检红字防护；手工赋值撞了会在扫描期被红字拦下。三表 u64 化（触冻结面）仅在真实碰撞再现时再议。

## 落账

[M7a.md](../Plans/M7a/M7a.md) 现状盘点 #9 + 批⓪ 开工核对两处已更新（本条目为事实记录）。
