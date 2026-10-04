# M7a 批⑥ review 轮 — 三实锤修复（guard 相等漏拒 / 块账回绕 / LAT1 写侧自洽）

- 日期：2026-10-05
- 范围：批⑥（LAT1）+ 2026-10-05 验收热修全部改动面逐文件复审
- 批文件：[Plans/M7a/2026-10-04-b6-atlas-baked-v1.md](../Plans/M7a/2026-10-04-b6-atlas-baked-v1.md) §8
- 状态：done（单测 +10 → **34346**；回归 full 复验；三组阴性验证过）

## 1. 实锤三件（全修）

| # | 级 | 缺陷 | 根因 | 修 |
|---|---|---|---|---|
| ① | 高（毁项目） | packager `--out == --project --force` 直接 `remove_all` 项目本体 | guard 的 `IsUnder` 把 `rel == "."`（相等）排除在互含外——只防了真包含，漏了相等 | 相等前置独立红字拒；两路径 `weakly_canonical` 正规化后再判互含（防 symlink 项目根：out 经 link 指进项目内 lexical 判不出） |
| ② | 中（崩装载） | `AssetIndex` 块账算术 u32 回绕：`sliceBase=0xFFFFFFF0 + count=0x10` 精确回绕 0 绕过防御 → `SetSpriteAt` 以巨值 resize → bad_alloc 崩 | `sliceBase + sliceCount` 全链 u32 加法（idCeiling 派生/块防御/FindBySpriteId 区间/ExportManifest 四处）；且防御位置在随迁消费**之后**——坏 count 先在随迁循环爆 | **sane 域收口提前到一切消费之前**：`idCeiling` 一律钳 `kMaxSpriteIdSanity=1<<22`（LBA1 sane-上限同语义，vector 巨分配上界 168MB）；块 `count>4096`（编辑器网格上限同域）或 `base+count`（64 位）越界即清零；巨号 spriteId（> 钳后 ceiling）判坏账重派；四处算术 64 位化。修正过程中自抓两处：idCeiling 派生 u64→u32 窄化 + 丢 `max(base, declared)` 下限（手写 `nextSpriteId:1` 会把重派号发进程序化页域） |
| ③ | 中（坏包） | `WriteBakedAtlasFile` 写侧不校验 build 自洽——坏 build 可写出「写盘成功但 Load 永拒」的包 | 只查了计数/页尺寸/payload 域，不查像素尺寸与页尺寸一致/条目页号界内/矩形界内/guid 非零唯一 | 写侧补全（判据与 Load 同源）；LBA1 review #21「产物永久不可载」教训：烤制期直白拒绝优于事后拒载 |

## 2. 轻微两件（修注释/交底）

- `AtlasStore.h` 契约注释谎报：`outRegistered` 写「整图+切片」实为整图数——改注释（整图数即「索引精灵全覆盖」判据面，语义更有用）。
- `AtlasStore::RebuildAll` 忽略 `Load` 返回值——`TextureStore::RebuildAll::LoadAll` 同语义先例（重建期单点失败不弃整局；包损坏在首次 Load 已被 GameEntry 响亮拦下），注释交底不改行为。

## 3. 复审干净面（抽查过、无问题）

- `PackAtlasPages` 装箱算术：u16 坐标域（虚拟 4096 封顶）、Align4 不越界、专属页判定、`rgba.size()` 校验 size_t 乘法；确定性排序全序（guid 唯一由 packager 前置校验保证）。
- `LoadBakedAtlasFile` 校验完备（魔数/版本/头长/计数/页尺寸域/payloadBytes 64 位对账/文件尺寸全等/条目界内/guid 唯一）；载荷切片 off 累加与文件尺寸全等互锁。
- GameEntry 生命期：`bakedAtlas`/`textures` 栈对象长于 recreate 回调；LAT1 失败响亮退出不回退；装配序（设备先于 ScriptHost）未破。
- packager 排除集与清单对账互锁（排除件不拷不记，对账双向过）。
- `RegisterGridSlices` 抽函数 dev 路径 (0,0) 等价；u32 乘法域（gridCols≤4096 × cellW≤65535）不回绕。
- 热修随迁逻辑：健康块保号不与重派号撞（重派 ≥ idCeiling > 健康块上界）；随迁块进 `taken` 防后续撞。

## 4. 门格

- 单测 **34346 checks OK**（+10：回绕巨号不崩/巨号重派 sane 域/回绕块清零/区间查无 + 写侧四拒：像素尺寸不符/页号越界/矩形越界/重复 guid 且不留文件）。
- 测试自抓两件（非实现 bug）：①热修轮 rebase 测试 ofstream 未 flush；②本轮断言边界错（`idCeiling` 钳 kMax 后重派号贴上限，`< 1<<22` 应为 `< 1<<23` 数量级语义）。
- packager 阴性三态：相等拒 / symlink 绕过拒（out 经 link 指进项目内）/ 互含拒；正常出包不受影响（selfcheck OK）。
- 回归 full 复验（pkg-smoke 含 LAT1 断言）：见批文件 §8 追记。
