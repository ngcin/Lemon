# M7a 批⑥ 验收热修 — 包形态切片动画精灵不渲染（两修：.meta 保留入包 + 号域平移随迁）

- 日期：2026-10-05
- 触发：用户真人走查包形态（按批⑥ 验收清单「切片动画正常」项）——截图实抓：HUD/整图精灵（子弹/宝石）正常渲染，**玩家与怪物（sheet 切片动画精灵）全部不可见**。
- 批文件：[Plans/M7a/2026-10-04-b6-atlas-baked-v1.md](../Plans/M7a/2026-10-04-b6-atlas-baked-v1.md) §7 热修追记
- 状态：done（单测 +10 → **34336**；双形态包端到端复验切片登记复活、visible 9→25/26）

## 1. 根因（两因叠加，主因批⑥ 新引入）

1. **主因（批⑥ 引入）**：packager sprite 排除集把 **`.meta` 一并排除出包**——而切片的像素几何真源（`gridCols/cellW` importer 段）在 `.meta`，`AssetIndex::LoadFromManifest` 的 `ReadGridImporter` 打不开包内 `.meta` → `gridCols=0` → `RegisterGridSlices` early return → 切片号域全部未登记（AddSpriteAt 本体号把 vector 撑到高位，切片号落空洞哨兵）→ `ResolveSpriteRefs` 把引用钳到 `sliceBase` → `IsValidSprite=false` 渲染侧过滤 → 不显示。整图精灵（bullet/gem 无切片）不受影响——与截图症状逐点吻合。
2. **副因（批⑤ 起潜伏，本批顺手排雷）**：`LoadFromManifest` 的切片块账防线「`sliceBase < spriteIdBase_` → 清零」——packager 与运行时 spriteIdBase 不同时（**无编辑器账的项目**：packager fallback 自 base=2 记账，lemon-game 程序化页后 base≈100+）全部号低于基线 → 本体重派 + 切片块清零 → 同样灭动画。当前模板自带 `.lemon/manifest.json`（号域 104..135 恰与运行时同基线）掩盖了此雷；任何新项目直接出包即踩。

## 2. 修复

- **`Tools/packager/main.cpp`**：排除集只收 sprite 源本体（`.png/.jpg/...`），**`.meta` 保留入包**（guid/网格几何真源随包；其余资产的 .meta 本就随行走，sprite 无例外理由）。
- **`Engine/Assets/AssetIndex.cpp` `LoadFromManifest`**：本体重派时**低域切片块随本体连号重发**（结构 = 本体后紧跟连号块，ScanFallback 发号序同款）而非清零——几何真源在 .meta、号只是进程内派生号，整体平移无害。原防御保留收窄：保号条目的块「越 manifest 上界（账不自洽）或低域怪块」才清零转全幅。实现注记：块随迁必须**与本体同循环**发号——首版「先发完所有本体再补块」会把后续条目本体号插进块前（单测 `slice block follows body contiguously` 阴性抓出后修正）。

## 3. 测试与复验

- 单测新增 `TestAssetIndexSliceRebase`（+10 → **34336 checks OK**）：低域块随本体连号重派（base=100 对 base=2 记账）+ 健康块保号 + 越上界坏账清零三态；测试自身首版有 ofstream 存活期内未 flush 的夹具 bug（Open 读到空档走 fallback），`f.close()` 后定界——测试 bug 非实现 bug，探针程序同夹具直证。
- 包形态端到端双路复验（vs-survivor 模板）：
  - **有 manifest 路径**（模板原样）：包内 0 PNG / 28 meta；切片登记复活（boss 107..114 / hero 116..124 / monster 126..133，manifest 原号域保号分支）；`visible 9 → 25`（与 dev 路径一致）；RESULT `atlas=1 …fps=1086.5 => OK`。
  - **无 manifest 路径**（删 `.lemon/manifest.json` 模拟新项目）：packager fallback base=2 记账（manifest.pkg.json first id=2）→ 运行时重派（hero 本体 108、块 109..117 连号）；`visible=26`、RESULT OK——第二颗雷实弹排掉。
- 回归 full 复验（pkg-smoke 含 LAT1 断言）：见批文件勾销追记。

## 4. 交底

- 包内磁盘面微增：7 个 sprite `.meta`（~2KB 级）回包——批⑥ 对表「磁盘 2.6× 膨胀」口径不变（PNG 本体仍排除）。
- 用户侧：**旧包不含 .meta，需重新出包**（`lemon-packager --force`）才生效；已出包数据无迁移面。
