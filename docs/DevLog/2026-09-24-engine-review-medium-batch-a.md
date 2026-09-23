# 2026-09-24 Engine-Review Medium 批A：Renderer 卫生（M4/M5/M6/M7/M8）

[2026-09-23 引擎评审报告](../Reports/2026-09-23-engine-review.md) 14 条 Medium 排查轮的第一批。
当日先做了**全量核实**：14 条逐条对当前 HEAD 源码复核（评审后 H1/H2 修复动过
Renderable/Particles，行号有漂移），结论 = 13 条完整成立 + M8 粒子半边已被 H2 顺带
覆盖（精灵路径仍在）。本批修 Renderer 侧 5 条，**全部零行为语义变更**（防御/修错路径），
金回放/场景档零扰动。

- **M4 Quality 构造**：`QualityManager(QualityTier start)` 原只初始化 `tier_`，
  `params_` 走默认成员初始化恒按 High（粒子预算 100000）；且 Low 档 `Update` 提前
  return 永不自愈——非 High 启动档预算长期错。修 = 构造器补 `params_(TierParams(start))`。
- **M5 批键位宽**：`SpriteBatchKey.blend:4`/`filter:2` 位宽存得下越界值（0–15/0–3），
  而 SpriteBatcher 只有 4 条管线/2 个采样器槽；`RenderableDesc.blend` 是裸 uint8_t、
  来自场景序列化 = 数据可驱动，`Record` 端 `pipelines_[blend]` 直接越界读。修 =
  **入口钳制**（`Create` 对 desc.blend/filter 钳 &3/&1 + WARN 一次）+ **消费端断言**
  （`Record` 加 `blend<4 && filter<2` 兜底）。不在 `MakeBatchKey` 钳——槽查找比较
  `key.blend == desc.blend`，键内钳会造成同键不同槽的假分裂。
- **M6 Create 不失效提取缓存**：`Destroy` bump `simVersion_` 而 `Create` 不 bump，
  暂停态（无 BeginSimTick）生成的实体在缓存命中帧不可见。修 = `Create` 补
  `++simVersion_`（每帧 BeginSimTick 本就 bump，正常流零额外成本）。
- **M7 键表上限仅断言**：`slots[kMaxSpriteKeys=64]`/`[kMaxParticleKeys=16]` 超限
  只靠 `LEMON_ASSERT`，Release 关断言即栈越界写；且 16 上限按 8 精灵×2 层即达。
  修 = 两处断言改**软丢弃**（`stats_.droppedSprites`/`droppedParticles` 尾加计数，
  超限精灵/粒子不渲染不崩溃）+ 粒子键表容量 16→32（留一倍余量）。
- **M8 空洞 sprite 未过滤（精灵路径）**：`Extract` 原只查 `spriteId > SpriteCount()`
  上界，`AddSpriteAt` 中间空洞号（哨兵 atlasIndex）落在界内漏过 → bindless 采样
  不存在槽。修 = 换 `atlas.IsValidSprite`（0/越界/空洞三态全检，与 H2 修过的粒子
  路径同语义）。**性能插曲**：首版实测 bench-mow 123.6fps vs HEAD 128.7fps
  （-4%）——`IsValidSprite` 原定义在 Atlas.cpp，10 万精灵每帧 10 万次跨 TU 调用；
  把哨兵常量 `kHoleAtlasIdx` 提到 Atlas.h、函数头文件内联后收回（124.6~127.2fps，
  与 HEAD 同噪声带）。

**回归**：engine-tests **13185 checks OK**（基线 13158 + 新增 27：TestQuality 扩
非 High 启动档三断言 + `TestRenderableSanitizeCacheAndHoles`（钳制/暂停态缓存/
空洞/键表满四段）+ `TestParticlesKeyOverflow`）；bench-mow PASS（124.6~127.2fps、
批数恒 4、判据 ≥60）；bench-sprites 159.1 / bench-particles 267.5 无回退；
spike-01 `--validate` exit OK；`editor-regression.sh quick` **6/6**。

后续批次：批D（M16/M17 序列化与平台）→ 批B（M10-M13 ScriptHost，含 M11 行为
变更需金回放双档验证）→ 批C（M14/M15 C# 域生命周期与事件）。
