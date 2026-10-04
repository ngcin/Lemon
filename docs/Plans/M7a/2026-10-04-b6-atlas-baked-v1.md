# M7a 批⑥ 图集 `.baked` v1 —— LAT1 容器 + packager writer + 引擎 reader（2026-10-04 开工）

Status: done 2026-10-04（门格与勾销记录见 §4；[DevLog](../../DevLog/2026-10-04-m7a-b6-atlas-baked-v1.md)；输入 = [ADR-016](../../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md) M5 定稿表 / D4 拍板 / [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md) 家族口径）

## 0. 开工研读结论（2026-10-04，读码实证）

| # | 事实 | 对批⑥ 的含义 |
|---|---|---|
| 1 | 精灵消费链全走 `AtlasRegistry`：spriteId→SpriteInfo(atlasIndex,uv) 是唯一事实源；GameEntry 贴图桥（guid:/路径两协议）与 hooks 都经 `ResolveAtlasSprite`（`GameEntry.cpp:513-538`） | LAT1 只需在装载期把页/矩形登记进注册表，**零渲染侧改动**；编辑器不动（dev 一文件一页维持）= 金回放零影响 |
| 2 | `UploadTexture` 载荷紧排（`VkBufferImageCopy` 无 rowLength，`RHI.cpp:1026`）+ `CreateTexture` 任意尺寸 | RAW RGBA 页位图直传可行；页可裁剪（非 2 幂合法） |
| 3 | 模板精灵实测 7 件、16×16..256×48（`Templates/vs-survivor/Assets`）；固定 4096 页 = 64MiB/页浪费 ~99% | **页裁剪**必做：虚拟 4096 装箱 → 实际页 = 用到 extent 取 4px 对齐（模板一页 ~260×52 ≈ 54KiB） |
| 4 | 切片 = 条目级子矩形：`TextureStore::RegisterSlices`（`TextureStore.cpp:32-51`）按 IndexedEntry 网格派生 `sliceBase+cell` 连号 | LAT1 条目存整图矩形即可；切片几何 reader 从 index 网格重派生——容器不存切片 |
| 5 | spriteId 记账在 manifest（`ExportManifest` schema 单源）；guid 是资产真源 | **容器以 guid 为键**（spriteId 不入容器）——manifest=号账、LAT1=几何账，packager 同轮生成保证一致 |
| 6 | LBA1 先例：32B 头 + static_assert + fwrite struct；review 加 sane 上限防 payloadBytes 回绕（2026-10-01） | LAT1 家族同款纪律：32B 头 + 64 位域校验 + 载荷/尺寸上界 |
| 7 | packager `CopyTree` 现只支持顶层跳过集（`main.cpp:207-224`） | 排除 sprite 源+.meta 需加相对路径过滤参数 |
| 8 | stb 三实现 TU 已在 lemon-engine（`Engine/Assets/StbImage.cpp`，include PRIVATE） | writer（解码）落引擎可单源；单测目标需补 stb include 才能播种 PNG |
| 9 | 批⑤ review 登记两顺手项：`--out` 落 `--project` 内自拷贝风险 + `ResolveRef` 死条件（`"/@"` 被 `'/'` 吞，`main.cpp:181`） | 本批顺手清（M7a.md §8 ①②） |
| 10 | AudioMount 烤制路径常量风格：`<root>/.lemon/baked/audio/<hex>.baked`（AudioMount.cpp:19） | LAT1 落位 `<root>/.lemon/baked/atlas/atlas.baked`（单图集 v1；多图集组归后续）——路径常量共享防两写点漂移 |

## 1. LAT1 v1 容器定稿（小端；ADR-016 M5 草案 → 本表为准）

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | magic | `"LAT1"`（Lemon Baked Atlas v1） |
| 4 | 2 | version | u16 = 1 |
| 6 | 2 | headerSize | u16 = 32（自校验，对齐 LBA1 习惯） |
| 8 | 2 | pageCount | u16 页数（≥1） |
| 10 | 2 | flags | u16 = 0 预留（采样/旋转策略归消费侧渲染配置，不入容器） |
| 12 | 4 | pageWidth | u32 虚拟装箱页宽（=4096；参考值，oversized 专属页不改变此字段） |
| 16 | 4 | pageHeight | u32 同上 |
| 20 | 4 | entryCount | u32 条目数（≥1） |
| 24 | 4 | reserved | u32 = 0 |
| 28 | 4 | payloadBytes | u32 Σ页 w×h×4（>4GiB 拒烤红字——压缩/分卷归 M7b；读取侧 64 位域校验防回绕，LBA1 review 先例） |
| 32 | 8×pageCount | pageDims | 每页 w u32 + h u32（**裁剪后真实尺寸**，4px 对齐； oversized 条目 = 精灵尺寸同规则） |
| … | 18×entryCount | entries | guid u64 + page u16 + x/y/w/h u16（**像素矩形 = 精灵本体，不含 gutter**；页内坐标，gutter 是装载期采样防线非几何） |
| … | Σ | payload | 页序 RGBA8 top-down 紧排（与 stb 解码行序一致，直传 UploadTexture） |

与草案的差异（定稿裁决）：条目表自 24 → 32 后（pageDims 插入）；`payloadBytes` 落 28；条目 18B 紧排（字节装配，非 struct 对齐）；**页尺寸表按页存储**（草案单值 → 裁剪页/专属页不可表达）。

**几何/装箱语义**：
- 虚拟装箱页 4096×4096；精灵间 gutter 2px 透明（线性采样防渗色）；页边缘 gutter 被裁剪舍去——clamp-to-edge 采样即边缘延展，优于透明衬边；
- 任一边 >4096 的精灵 → **专属页**（页尺寸 = 精灵尺寸 4px 对齐，位于 (0,0)）；任一边 >16384 拒烤红字（GPU maxImageDimension2D 域）；
- 装箱 = shelf 行式（排序 h desc → w desc → guid asc，**全确定性**：同输入同字节）；MaxRects 全量升级视余量（06 §5 口径，D4）；
- 写盘 `WriteFileAtomic`（音频烤制同款）；读侧整文件校验（魔数/版本/头长/计数域/尺寸域/文件尺寸全等/条目页号与矩形界内/guid 唯一非零）。

## 2. 行级分解

### A 引擎侧 writer（单源，packager 消费）

- [x] `Engine/Assets/AtlasBake.{h,cpp}`：容器结构（`BakedAtlasBuild{pages,entries,pagePixels}`）+ `PackAtlasPages`（纯函数可测）+ `WriteBakedAtlasFile`/`LoadBakedAtlasFile`（§1 字节表）+ `BakeProjectAtlas(index, dst, stats)`（Sprite 条目 stb 解码 → 装箱 → 原子写）+ 路径常量 `kBakedAtlasRelPath`。
- [x] `Engine/CMakeLists.txt` 源表两件（AtlasBake/AtlasStore）。

### B 引擎侧 reader + 装载链

- [x] `Engine/Assets/TextureStore.{h,cpp}`：`RegisterSlices` 抽自由函数 `RegisterGridSlices(atlas, e, slot, ox, oy, ew, eh)`（dev/LAT1 两 reader 共用，origin/图面尺寸参数化）。
- [x] `Engine/Assets/AtlasStore.{h,cpp}`：`Init(device, atlas, index, firstSlot)` + `Load(path)`（LoadBakedAtlasFile → 逐页 CreateTexture/Upload/Bind/RegisterAtlas → `RegisterAtlasSprites`）+ `RebuildAll(device)`（设备丢失，TextureStore 同款）+ 页数探针；**纯登记核 `RegisterAtlasSprites(atlas, index, build, firstSlot)` 独立于 RHI**（单测面）：双向对账（LAT1 guid ↔ index Sprite，缺失/类型不符红字 false）、槽容量校验、整图 AddSpriteAt + 切片 RegisterGridSlices。
- [x] `Engine/Entry/GameEntry.cpp`：`<root>/.lemon/baked/atlas/atlas.baked` 在场 → AtlasStore 装载（失败 = 包完整性响亮退出）；缺场 → TextureStore 现路径不变；recreate 回调两形态；RESULT 增 `atlas=%u` 位（页数；dev 恒 0）。

### C packager 消费路径

- [x] `Tools/packager/main.cpp`：① `--out`/`--project` 互含 guard（任一包含另一 → 红字拒出包，防 CopyTree 自拷贝/--force 误删项目——批⑤ review ①）；② `ResolveRef` 死条件清理（`"/@"` 分支删——批⑤ review ②）；③ `BakeProjectAtlas` 现烤 → `data/.lemon/baked/atlas/atlas.baked`；④ `CopyTree` 增相对路径过滤——**sprite 源排除出包（`.meta` 保留：切片几何真源，见 §7 热修）**；⑤ 自检：RESULT 增 `atlasPages/atlasSprites` + essential 清单加 LAT1（sprites>0 时）。
- [x] 无 sprite 项目：跳过烤制与排除（纯 UI 项目合法形态）。

### D 测试与门格

- [x] `tests/engine_tests.cpp`：`TestBakedAtlasContainer`（roundtrip 全等 + 坏魔数/坏版本/坏头长/截断/尺寸不符/条目越界/重复 guid 拒载 + payloadBytes 回绕防线）+ `TestAtlasBakePack`（确定性两轮字节全等 / oversized 专属页 / 溢出分页 / 矩形不重叠且在界内 / 页像素 roundtrip 与源逐字节一致 / gutter 边距）+ `TestAtlasStoreRegister`（合成 build+index → RegisterAtlasSprites：spriteId 按 manifest 号登记、切片子矩形 UV 数学、悬空 guid 双向红字）+ `TestBakeProjectAtlas`（temp 项目播种 PNG+meta → Bake → Load 全等）；`tests/CMakeLists.txt` 补 stb include（播种用）。
- [x] `tools/editor-regression.sh` pkg-smoke：包体输出增 `atlas=[1-9]` 断言（LAT1 链路机器面）。
- [x] 门格：ctest 3/3 + 单测增长 + 回归 full 19/19（game-smoke dev 路径零扰动 + pkg-smoke LAT1 路径）+ bench-survivor 零降级（编辑器路径不动）。
- [x] 对表（DevLog）：装载耗时（dev PNG 解码 vs 包 LAT1）/ 纹理槽数（2+N精灵 → 2+N页）/ 常驻内存（页字节 vs Σ精灵字节）——同夹具 A/B。

## 3. 出口判据（M7a.md §3 批⑥ 行）

- **包内 sprite 链路全走图集 `.baked`**：packager 排除 sprite 源 + lemon-game LAT1 装载 + pkg-smoke atlas 位断言。
- **60fps 复验**：pkg-smoke fps ≥ 60（既有判据位）。
- **格式字节表入 ADR-016 追记 + 06 §5 注记**（实测数字随 DevLog）。

## 4. 风险与兜底

- **RAW 磁盘代价**（PNG→RGBA 膨胀 ~2-10×）：v1 接受（判据 = 启动零解码）；压缩（zstd/LZ4 页载荷）归 M7b 登记项；模板实测数字入 DevLog 交底。
- **包内删 manifest.pkg.json 后回退扫描丢 spriteId**：包完整性语义 = manifest.pkg.json 与 Game.dll 同级要件（删 = 破包）；dev 项目不受影响（编辑器路径不动）。
- **LAT1 与 manifest 失配**（手改包）：RegisterAtlasSprites 双向红字 fail-fast，不静默缺精灵。
- **超大精灵内存**（多张 16K 专属页）：16384 上界 + payloadBytes 4GiB 上界红字；ASTC/压缩纹理归后续（04 §8）。
- **编辑器侧不切换图集页**（D4 默认 packager 专用）：像素断言/回归面零扩大，金回放零重录。

## 5. 登记项（不扩 scope）

- MaxRects 全量升级（余量评估后定；shelf 对同高族像素风浪费已可接受——实测数字为准）；
- 页载荷压缩（M7b 资源校验批）；
- 多图集组（06 §5 "图集组" 口径，v1 单图集）；
- LAT1 流式装载（整读 v1；>100MiB 图集的峰值 RAM 优化归后续）；
- sprite 源排除的逆命题：未索引 png 照拷（死重无害；校验红字归编辑器侧既有纪律）。

## 6. 勾销记录（2026-10-04 完工）

**全部勾销，一次过。** 落地形态与 §1–§2 分解一致，无范围变更。

- **代码件**：`Engine/Assets/AtlasBake.{h,cpp}`（writer：装箱+容器+项目烤制）+
  `AtlasStore.{h,cpp}`（reader：装载 + 纯登记核 `RegisterAtlasSprites`）+
  `TextureStore` 切片抽 `RegisterGridSlices` 自由函数（dev/LAT1 两消费点同源）+
  GameEntry LAT1 分支/RESULT `atlas=%u` 位/recreate 双形态 + packager 四件
  （现烤/排除/自检/两顺手清）。编辑器零改动（D4 默认 packager 专用兑现）。
- **门格**：单测 **34326**（+94，四件：容器 roundtrip+11 态拒载面 / 装箱确定性+
  专属页/分页/重叠/像素对位 / 登记核 UV 数学+失配双向拒载 / 项目烤制端到端）；
  ctest 3/3；回归 **full 19/19**（第三轮全绿——首轮 smoke-ui、次轮 smoke-drag
  抖动位轮换 = §8 既有登记项的既视形态，复跑绿 T1 口径；pkg-smoke 新
  `atlas=[1-9]` 断言过 = LAT1 链路机器面锁死，game-smoke dev 路径 atlas=0 零扰动）；
  bench-survivor **fps=85/87**（基线带 82–84 内零降级，playerHp 逐位一致）。
- **对表**（§3 出口判据项，DevLog §4 全表）：纹理槽 9→3；解码 7× PNG → 0；
  磁盘 48,261B PNG+7 meta → 126,630B RAW（膨胀 2.6× 交底，压缩归 M7b）；RSS
  106.5MB→106.4MB 持平；启动墙钟 0.77s→0.77s 持平（模板 7 张小图解码在噪声下，
  收益随精灵数线性）；900 帧 fps 1239.9→1319.7。页裁剪：固定 4096 页 64MiB →
  模板单页 124KiB。
- **包行为**：自检 RESULT 增 `atlasPages=1 atlasSprites=7`；包内零 PNG/JPG
  （`find` 实证）；包体零参 smoke `atlas=1 fps=1319.7 => OK`；`--out` 互含 guard
  双向红字阴性验证过（`--force` 未删项目）。文件数 319→305（14 件 sprite 源+meta
  出包）。
- **文档**：ADR-016 M5 草案表 → 定稿表（差异三条裁决随表）+ 06 §5 落地注记 +
  DevLog + 本勾销 + M7a.md 状态。

## 7. 验收热修追记（2026-10-05，[DevLog](../../DevLog/2026-10-05-b6-hotfix-slice-meta-and-id-rebase.md)）

用户真人走查实抓：包形态**切片动画精灵（玩家/怪物）不渲染**（整图精灵/HUD 正常）。
两因叠加：①批⑥ 排除集误把 `.meta` 一并排除——切片像素几何真源（grid importer
段）在 `.meta`，包内缺席 → `gridCols=0` → 切片号全未登记（空洞哨兵）→ 渲染过滤；
②批⑤ 起潜伏：`LoadFromManifest` 对「`sliceBase < base`」一律清零——无编辑器账
项目出包（packager base=2 记账 vs 运行时 base≈100+）必灭（模板自带 manifest 同
基线掩盖）。修：排除集只收源本体（.meta 回包）+ 本体重派时低域块**随本体连号
重发**（首版"先本体后块"被单测抓出插号断裂 → 同循环发号）。单测 +10 → 34336
（`TestAssetIndexSliceRebase` 三态：随迁连号/健康保号/坏账清零）；双路包端到端
复验：有 manifest（visible 9→25）/ 无 manifest（rebase 实弹，visible=26）切片
登记全复活。旧包需 `--force` 重出。

## 8. review 轮追记（2026-10-05，[DevLog](../../DevLog/2026-10-05-b6-review-hardening.md)）

批⑥+热修全改动面复审，实锤三修：①guard 漏 `--out == --project` 相等 case
（`--force` 直删项目）——相等独立拒 + `weakly_canonical` 防 symlink 绕过（阴性
三态过）；②`AssetIndex` 块账 u32 回绕（`0xFFFFFFF0+0x10` 精确回绕绕过防御 →
巨 resize 崩装载，四处算术族）——sane 域收口提前到消费之前（idCeiling 钳
`1<<22` + count 钳 4096 + 巨号判坏账 + 全链 64 位化；修正中自抓窄化与 idCeiling
下限两处）；③`WriteBakedAtlasFile` 写侧自洽校验缺失（坏 build = 写盘成功但
永不可载）——补判据与 Load 同源的四拒。轻微两件：`outRegistered` 契约注释
改实况（整图数）；`RebuildAll` 忽略返回值注释交底（TextureStore 同语义先例）。
单测 +10 → **34346**；复审干净面清单见 DevLog §3。
