# 2026-09-23 · M5 批③：表现层（clip .asset + Animator 帧映射 + 网格切片 + 素材包第一批）

T1–T5 全完（分解见[批③计划](../Plans/M5/2026-09-23-b3-presentation.md) §16–§20）。核心决策六条（D1–D6）：clipId = `.clip` 资产
GUID 低 32 位（prefabId 同款约定）；引擎侧 `ClipTable`（World 持有，纯 id 数据零
GPU 依赖）；`.clip` JSON 帧引用 = 精灵表 GUID + 切片号（manifest 重排不断链）；
`.meta` importer 段网格切片（**frames 作者显式声明**——DB 零解码记账连号块，
yami .anim hframes 同款）；帧号纯函数 `min(n-1, time*fps)`（回放确定）；**无 clip
路径 = M2 旧算术逐位保留**（金回放零重录的机制保证——批②勘误教训的前置规避：
本批零新组件、零布局改动、零字段位变化）。

- **引擎**：`ECS/ClipTable.{h,cpp}` 新文件；AnimatorSystem #13 帧映射（playOnStart=0
  冻结、loop0 钳末帧、负速钳 0、无 SpriteRenderer 纯计时）；`AtlasRegistry::SetSpriteAt`
  覆盖式登记（切片热重切）。
- **资产链**：`AssetType::Clip`（.clip）+ `.meta` importer 段解析（SyncMeta 每扫重读
  = 热改即生效）+ Rescan 连号切片块记账（manifest 扩 `slice:{base,count}` 键，旧档
  缺键兼容）+ AssetGpuCache 切片登记/热重切（网格越界红字不切宁缺勿错）。
- **编辑器**：EnterPlay `BuildPlayClipCache`（坏 clip 红字跳过回退 M2）；Inspector
  `FieldHint::ClipRef` clip 槽（下拉/拖入 kind 4/右键清空）；AssetBrowser clip 类型。
- **素材包第一批**：`Samples/Assets/yami-dungeon/`（hero×2 9 帧 16px 格 / monster×2
  8 帧 / boss 8 帧 32px 格——帧数与 yami `.anim` hframes 逐表核对）+ hero-walk /
  monster-walk / boss-idle 三 clip + README；`THIRD_PARTY.md` 登记（MIT，80KB）。
- **测试**：engine-tests 13127 → **13145**（+18，TestVerifyAnimatorFrameMapping：帧界
  tick 8/22、回绕 tick 23、loop0 钳末帧、playOnStart 冻结三态、半速、负速钳 0、无
  渲染器不炸、未知 clipId 回退 M2、roundtrip、孪生 300 tick StateHash 相等）；script
  -tests **1460 不变**（C# 零改动，Animator2D 镜像本就同步）；新冒烟 `--smoke-anim`
  （程序化 4 帧表必验 + yami 表在场即验真素材链；editor-regression 11→**12 步**）。
- **验证**：ctest 3/3；**金回放零重录**（m5b2 三档 replay mismatches=0——判据 2 机制
  保证兑现）；bench-survivor 动画化三跑 **58/67/66 fps PASS**（alive 10435、anim
  切片命中 10003/10003、三跑逐位一致；Animator avg **0.128ms**）；editor-regression
  full **12/12**（首轮 smoke-ui rename=0/1 与 final overlay 两步时序飘忽，直跑各 ×3
  全绿后重跑全量过——09 §8 已登记同族）。
- yami 真素材链人工核验（--smoke-anim + 素材拷入临时项目）：5 表切片全登记
  （hero 105..113 等）+ 4 clip 建表 + 双链断言 OK。
