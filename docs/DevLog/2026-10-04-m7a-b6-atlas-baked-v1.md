# M7a 批⑥ — 图集 .baked v1（LAT1）落地：容器定稿 + packager writer + 引擎 reader

- 日期：2026-10-04
- 批文件：[Plans/M7a/2026-10-04-b6-atlas-baked-v1.md](../Plans/M7a/2026-10-04-b6-atlas-baked-v1.md)
- 设计基准：[ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md) M5（LAT1 定稿表本批追记）/ [ADR-015](../ADR/ADR-015-Audio-System-And-Baked-Format.md) M2 家族口径 / [06 §5](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)
- 状态：代码面 done（真人验收归批⑧ 总成——包形态可玩面与批⑤ 同链路，pkg-smoke 机器面已覆盖）

## 1. 做了什么

`.baked` 容器家族第二员 `LAT1`（图集）定形并接入出包/运行时全链：

- **writer（单源引擎）**：`Engine/Assets/AtlasBake.{h,cpp}`——`PackAtlasPages`（shelf
  行式装箱，确定性排序 h desc → w desc → guid asc = 同输入同字节）+ 容器读写
  （32B 头 + 按页尺寸表 + 18B 紧排条目 + RAW RGBA 载荷；tmp+RenameReplace 原子写）
  + `BakeProjectAtlas`（Sprite 条目 stb 解码 → 装箱 → 写盘）。几何要点：虚拟 4096
  页 + 精灵间 2px gutter + 页右/下裁剪到用到 extent（4px 对齐）——**小项目内存/
  磁盘不按整页 64MiB 计**；>4096 任一边 → 专属页；>16384 拒烤（GPU 域）。
- **reader**：`Engine/Assets/AtlasStore.{h,cpp}`——`Load`（页位图直传 UploadTexture
  + RegisterAtlas）+ **纯登记核 `RegisterAtlasSprites`**（独立于 RHI 可单测）：LAT1
  guid ↔ 索引 Sprite 双向对账（失配 = 包与账不一致，红字 fail-stop 不静默缺精灵）+
  manifest 记账号 `AddSpriteAt` + `.meta` 网格切片子矩形派生。
- **切片登记共用件**：`TextureStore::RegisterSlices` 抽自由函数
  `RegisterGridSlices(atlas, e, slot, ox, oy, imageW, imageH)`——dev 整页导入（0,0）
  与 LAT1 页内偏移（entry.x/y）两消费点同源，零语义漂移。
- **GameEntry**：`<root>/.lemon/baked/atlas/atlas.baked` 在场 → LAT1 链路（失败 =
  包完整性事故响亮退出，不回退 PNG）；缺场 → TextureStore 现路径不变（dev/编辑器
  零改动，D4 默认 packager 专用 = 金回放零影响）。RESULT 增 `atlas=%u` 位。
- **packager**：图集现烤入包（音频现烤同位）+ **sprite 源+.meta 排除出包**
  （"包内 sprite 链路全走图集 .baked"出口判据的实做面）+ 自检 RESULT 增
  `atlasPages/atlasSprites` + essential 清单收 LAT1；顺手两清（批⑤ review 登记）：
  `--out`/`--project` 互含 guard（双向红字，--force 不再可能误删项目）+
  `ResolveRef` 死条件（`"/@"` 是 `'/'` 前缀子集）。

## 2. 设计裁决（开工研读 → 定稿）

1. **容器以 guid 为键、spriteId 不入容器**：manifest 记号账、LAT1 记几何账，两条
   账由 packager 同轮生成保证一致；装载期 guid join——绕开"lemon-game 程序化页
   数与 packager 基线不同导致 spriteId 数值不同"的既有口径差（批⑤ manifest.pkg.json
   同款机制的进一步加固）。
2. **页尺寸按页存表（草案单值 → 定稿 per-page）**：页裁剪与 oversized 专属页在
   单值 schema 下不可表达；`payloadBytes` 落 28 并在读取侧 64 位域对账（回绕防线，
   LBA1 review 2026-10-01 先例直接复用）。
3. **RAW RGBA 免解码**（ADR 草案倾向 → 定稿）：启动判据优先；磁盘膨胀模板实测
   2.6×（见 §4），压缩归 M7b 登记项。
4. **包内 sprite 源排除**：消费链全走 AtlasRegistry（贴图桥 guid:/路径两协议、
   hooks、切片）经读码实证无旁路 → 源文件入包 = 纯死重；排除后包完整性 =
   manifest.pkg.json + LAT1 同为要件（删 = 破包，RegisterAtlasSprites 双向红字）。

## 3. 门格

- 单测 **34326 checks**（批⑤ 34232 → +94）：四件新增——`TestBakedAtlasContainer`
  （roundtrip 全等 + 坏魔数/版本/头长/payloadBytes 对账/entryCount/零页尺寸/条目
  越界/重复 guid/截断/尾多出/零条目拒载面）、`TestAtlasBakePack`（两轮字节全等
  确定性 + oversized 专属页 + 2040²×5 分页 + 同页不重叠 + 界内 + 页像素逐字节
  对位 + 零尺寸/超 GPU 域/载荷不符阴性）、`TestAtlasStoreRegister`（manifest 号账
  登记 + 整图/切片 UV 数学（u0=x/pageW 直核）+ 失配双向拒载）、`TestBakeProjectAtlas`
  （PNG 播种 → 烤 → 读回全等 + 无 sprite 项目跳过形态）。
- ctest 3/3；回归 full **19/19**（pkg-smoke 增 `atlas=[1-9]` 断言——LAT1 链路机器
  面锁死；game-smoke dev 路径 atlas=0 零扰动）。
- 手工包验证：自检 OK（atlasPages=1 atlasSprites=7 closureMiss=0 …）；包内零
  PNG/JPG；包体零参 `--smoke` RESULT `atlas=1 …fps=1319.7 => OK`。
- `--out` 互含 guard 双向阴性验证过（out 在项目内 / 项目在 out 内皆红字拒出包，
  `--force` 未删项目文件）。
- bench-survivor（编辑器路径零改动面）：见批文件勾销记录。

## 4. 启动对表（vs-survivor 模板同夹具 A/B；出口判据"启动对表"项）

| 指标 | dev（PNG 逐文件） | 包（LAT1） | 备注 |
|---|---|---|---|
| 纹理槽占用 | 9（程序化 2 + 7 页） | **3（程序化 2 + 1 页）** | kMaxTextureSlots=256 消费面余量 ↑ |
| 解码面 | 7× stbi PNG 解码 + 7 次上传 | **0 解码 + 1 读（124KiB）+ 1 上传** | 运行时零解码兑现 |
| 磁盘（精灵面） | 48,261B PNG + 7 meta | 126,630B atlas.baked | RAW 膨胀 2.6×（压缩归 M7b） |
| 常驻内存 RSS | 106,496,000B | 106,377,216B | 持平（CoreCLR 占大头） |
| 启动墙钟（--frames 2，3 跑中位） | 0.77s | 0.77s | 持平——模板 7 张小图解码本在噪声下；大批量项目收益随精灵数线性 |
| 900 帧 smoke fps | 1239.9 | 1319.7 | 同量级，60fps 判据远超 |

（页裁剪效果：固定 4096 页方案单页 64MiB；裁剪后模板单页 124KiB。）

## 5. 登记项（不扩 scope，随批文件 §5）

- MaxRects 全量升级（shelf 对同高族像素风浪费可接受；异构大项目再评估）；
- 页载荷压缩（zstd/LZ4，M7b 资源校验批——磁盘 2.6× 膨胀的正式解）；
- 多图集组（06 §5 "图集组"口径；v1 单图集）；
- LAT1 流式装载（v1 整读峰值 = 文件 ×2；>100MiB 图集再优化）；
- 未索引散装 png 照拷入包（死重无害；编辑器侧既有纪律管辖）。
