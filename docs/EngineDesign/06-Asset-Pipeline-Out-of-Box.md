# Lemon 引擎设计 — 06 资产管线与开箱即用

> "开箱即用"是本引擎与纯内核引擎（Luma）的最大差异点，对标 yami-rpg-editor 的端到端体验：新建项目即见可玩 demo、默认素材全家桶、一键出包上 Steam。
> 但修正 yami 的两个结构性缺陷：①"模板即引擎"导致升级覆盖用户修改 → 改为**引擎独立安装 + 项目引用**；②编辑器/运行时双实现漂移 → 单一数据事实源（01 §1）。

---

## 1. 引擎与项目的布局（开箱体验的地基）

```
Lemon/                                  # 引擎安装目录（升级只动这里）
├── LemonEditor.exe                     # 编辑器（含运行时内核）
├── runtime/                              # 随游戏发布的引擎运行时（GameEntry + 库）
├── dotnet/                               # 随引擎分发的 .NET 运行时（脚本域）
├── Templates/                            # 项目模板（§7）
└── docs/

MyGame/                                   # 用户项目（升级永不触碰）
├── project.lemon                         # 项目清单：引擎版本锚点、场景列表、配置
├── Assets/                               # 资产（源文件 + .meta，06 §2）
├── Scenes/                               # 场景文件（.scene）
├── Prefabs/                              # Prefab 资产（.prefab）
├── Game/                                 # C# 脚本工程（标准 csproj）
├── Data/                                 # Team 表/导演波次/本地化/曲线等数据资产（.asset）
├── .lemon/                             # 编辑器状态（布局/缓存/缩略图，gitignore）
└── Builds/                               # 打包输出（gitignore）
```

- `project.lemon` 记录 `engineVersion`（语义化 + 最低兼容），编辑器启动校验并提示升级/迁移。
- **新建项目向导**（yami 流程照抄）：选模板 → 选路径 → 复制模板 → 生成项目 GUID（存档隔离键）→ 直接打开编辑器并进入 Play —— 全程零配置。
- 引擎升级 = 替换安装目录；项目数据格式带 `schemaVersion`，自动迁移链升级（03 §13）。

## 2. 资产管线：GUID + manifest（照抄 yami 数据 schema）

### 2.1 GUID 与 .meta

- 每个资产文件旁伴随同名 `.meta`（JSON）：

```json
{ "guid": "b0348bc4e21a598a", "type": "sprite",
  "importer": { "slice": "grid", "cell": [32,32], "pivot": [0.5,0.5], "ppu": 32 },
  "hash": "sha256:9f2c...", "importedAt": 874312400 }
```

- **重命名/移动文件不破坏引用**：场景/Prefab 只存 GUID；`.meta` 随文件移动。
  **M6a 批⓪ 落地（sprite 引用 GUID 化，最后一处数字资产引用收口）**：
  `SpriteRenderer` 尾加 `uint64_t spriteGuid`（12→24B，C# 镜像同步），`.scene`/
  `.prefab` 双写 `{spriteGuid（真源）, spriteId（进程内派生号）}`。装载/恢复/
  Undo/编辑态 Prefab 落地时 `EditorContext::ResolveSpriteRefs` 按 guid 归一：
  命中 → 覆写 spriteId（改名/移位/manifest 重建 id 漂移后引用不断链）；查无 →
  保留旧号渲染 + 悬空告警；**存量回填**——guid=0 且 spriteId 恰为登记资产本体号 →
  补写 guid 标 dirty（旧档开一次存一次即升级 GUID 口径；切片 cell 号/程序化页号
  无 guid 语义不回填）。切片表解析口径：id 在 cell 区间内 = 保号（cell 是层内
  偏移）；区间外漂移回 cell 0（本体号与 cell 号相邻无法甄别，逐 cell guid 化
  留 M6c (guid, cell) 二元组）。作者面三口（combo/拖入/清空）双写；回归防线 =
  `--smoke-guid`（插入+改名+删 manifest 三难并发 → 重开逐实体归一断言）。
- `.lemon/manifest.json`（项目级索引，**导入器生成物**，随状态目录 gitignore——Godot `.godot/uid_cache.bin` 同位思路；2026-09-19 定落位）：`guid → {path, type, dependencies[], hash}`；编辑器启动按 manifest 做一致性体检（孤儿 meta / 缺失依赖红字报告）。

### 2.2 导入器（编辑器侧，后台线程池）

| 类型 | 源格式 | 导入产物 | 备注 |
|---|---|---|---|
| sprite | png/jpg | 纹理 + 切片（自动/网格/手动，Prowl `TextureImporter` 思路） | 切片 GUID 稳定匹配（重导入不漂移） |
| atlas | 目录/手选集 | 图集页 + sprite 重定向表（MaxRects，Prowl2D M0 实现移植思想） | §5 |
| audio | wav/ogg | PCM16 预解码缓存（`.meta` importer 记循环点/预载；烘焙入 **.baked** 容器 v1，[ADR-015](../ADR/ADR-015-Audio-System-And-Baked-Format.md)；运行时零解码器） | miniaudio（**M6c**，2026-09-30 设计定形；原表 2026-09-24 行改号 M6b 已过时，双行收敛） |
| font | ttf/otf | 位图字体页(v1)/SDF 图集(v2) | 02 §7 |
| clip2d | json | 帧动画资产（**.anim**，T3d 批④前 .clip） | AnimationEditor 产出 |
| animset | json | 动画集/每角色绑定（**.override**，批④前 .ani；段名→clip 引用清单） | AnimationEditor 产出 |
| controller | json | 动画状态机（**.controller**：状态词表/参数/过渡；ADR-013） | 手写 JSON（图编辑器挂起） |
| rml/rcss | rml/rcss | UI 文档/样式表原档（**.rml**/**.rcss**，ADR-014 一屏一文档） | M6b 批③b（原 M6a）：DB/GUID/.meta/浏览器识别 + 双击装载 + 热重载（rcss = ReloadStyleSheet 保 DOM）；`<img>` 经贴图桥吃项目精灵（`AtlasTexture` 页借用，`rect` 属性切子图） |
| particles | json | 发射器资产 | ParticleEditor 产出 |
| tileset | png + json | 图集 + 碰撞标志 + 自动瓦片变体表 | 05 §7 |
| curve/data | json | 曲线、Team 表、波次表 | 数据资产 |

- **热重载**：`FileWatcher`（线程轮询，Luma 同款）触发增量导入 → 按依赖图通知（纹理变更 → 重建图集页 → 受影响场景视口刷新标记）。运行中 Play 的资产热替换（贴图/参数即时生效，音效不中断）。

> **M5 批③落地注记（2026-09-23）**：clip2d（`.clip`）与**单页网格切片**先行——
> - `.clip` JSON（帧动画资产）：`{schemaVersion, name, fps, loop, frames:[{sheet:<guidHex>,
>   cell:<行优先序>}]}`；帧引用 = 精灵表 GUID + 切片号（不存 spriteId，manifest 重排
>   不断链）；编辑器 EnterPlay 解析入 `World::Clips()`（进 Play 快照）；消费 =
>   Animator2D 帧映射（03 §8.1）。AssetType 入库（AssetBrowser 可拖入 Inspector clip 槽）。
> - `.meta` 的 `importer` 段（§2.1 格式）生效：`{"slice":"grid","cell":[w,h],
>   "frames":[c,r]}`——**frames 由作者声明**（yami .anim hframes 同款；DB 零解码
>   即记账连号切片块，manifest 持久）；AssetGpuCache 校验像素整除后一页登记
>   全幅 sprite + 连号切片（`AddSpriteAt`）。热重导网格/尺寸变化 = `SetSpriteAt`
>   覆盖重切（号不变）。多表 MaxRects 打包/手动切片 UI 归 M6c；AnimationEditor 归
>   M6a 批②（2026-09-24 重排）。
> - 第一批素材包入库 `Samples/Assets/yami-dungeon/`（见 §7 注记）。

> **M4.4 落地范围注记（与 05/M4.md §1.2 对齐）**：sprite 族先行——每 PNG
> 独立纹理页（bindless 槽 2..，上限 kMaxTextureSlots=64）+ 一页一全幅 sprite；
> 切片/手动划分与 MaxRects 图集打包随消费者落 M5+/M6。删除资产 = **墓碑**（号与
> GPU 纹理保留到重启：登记号仍在 AtlasRegistry，销毁纹理会使引用中的 spriteId 采样
> 悬空描述符）；体检红字覆盖孤儿 meta / GUID 冲突 / 缺失引用。
> manifest 记账 spriteId 只增不减 → 已存场景引用不因增删资产漂移。
> **2026-10-01 修订（墓碑退役 + 孤儿清扫，用户拍板）**：删除文件 = **条目同轮出表**
>（墓碑机制退役——实现本就会话级、注释"重启不回收"言过其实；误删恢复由 .meta
> 随文件走 + 版本管理承担，Unity/Cocos/Godot 三家均无持久墓碑、市场收敛于清扫）。
> 孤儿 .meta：**零引用自动清扫**（扫描期 + Assets 菜单手动入口同判定）；**仍被引用
> 保留 + 红字**（"只恢复源文件"场景的复链钩子——引用面判据 = 项目数据文本中 guid
> 的 hex/十进制形态，引擎本体可保守，Godot 社区插件只能盲清）；源+meta 双删仍被引用
> = 红字一次。详见 [DevLog](../DevLog/2026-10-01-orphan-meta-sweep-and-tombstone-retirement.md)。
> **M5 批④后修②（2026-09-23，demo/svr-test 实测）**：**换项目 = 图集注册表复位到
> 内置页**（`OpenProjectPipeline`：`Registry().Reset()` + `ProceduralAtlas::Build`
> + `AssetGpuCache::ClearPages` + ImGui 纹理重绑，设备重建回调同配方）——此前基号
> 取"注册表现存计数 +1"，而注册表跨项目累计：**同一会话里作第二个项目打开 →
> 全体 spriteId 后移上个项目的精灵数**（实测 +31）→ 模板场景烘焙引用全悬空、
> 玩家/怪物全不渲染（自动重开上次项目 + 新建向导 = 稳定触发路径；`--smoke-template`
> 一直是独立进程首开故从未命中）。复位后基号恒 = 内置页计数 +1（本机构建 104），
> 无 manifest 的 fresh 项目在任意会话位置逐位可复现；**既有项目漂移自愈 = 删
> `.lemon/manifest.json` 重开**（场景引用按确定性扫描重记账）。回归防线：
> smoke-template 末尾同进程再开第二个模板拷贝，spriteId 记账逐项全等断言。
> **M6a 批⓪ 残余风险勾销（2026-09-24）**：上段自愈的隐含前提"资产集未变"已解除
> ——spriteGuid 真源化后（§2.1），删 manifest 且**同时**导入新资产/改名移位（id
> 全体重排）的场景引用仍逐实体归一（`--smoke-guid` 三难并发断言）。残余面收窄到
> "切片表跨漂移回 cell 0"（§2.1 口径，M6c (guid, cell) 收口）。
> **M4.5 补记（2026-09-20）**：资产扫描根由 `Assets/` 扩为**项目根**（本册 §1 布局
> 对齐）——根级 `Prefabs/` 入索引（prefab 导出/实例化读写改走项目根落位，M4.4 的
> `Assets/Prefabs/` 落位偏差消除），`Game/Scenes/Data/Builds/obj/bin` 与点目录排除
> （脚本工程/场景/数据/出包不是资产源；obj/bin 为 dotnet 构建噪声）；entry.relPath
> 统一为项目根相对（`Assets/x.png`、`Prefabs/y.prefab`），M4.4 旧 manifest 键
> （相对 Assets/）在 OpenProject 时同号迁移。新建项目向导（blank 模板）按 §1 布局
> 落位全套目录 + project.lemon（engineVersion 锚点）+ 零配置 Game/ 脚本工程 +
> 种子资产（spawn.png 固定 guid 与模板 SpawnerBehaviour 直连——"新建项目到刷怪
> 场景零代码"的闭环）。

## 3. 场景与数据格式（JSON + 行程编码，可 diff 的紧凑格式）

- 场景 `.scene` = JSON（schema 版本化；**M6a 批⓪ 起实现为 v2**——实体附加复数
  `scripts[]`（旧单数 `script` 读侧兼容/迁移链升级，03 §13）；SpriteRenderer 携
  `spriteGuid` 真源双写，§2.1）：

```json
{ "schemaVersion": 2, "name": "Arena01", "viewport": [1920, 1080], "tileSize": 32,
  "entities": [
    { "guid": "...prefabRef...", "pos": [128, 64], "overrides": { "Chase.speed": 88 } },
    { "components": { "Transform2D": {...}, "SpriteRenderer": { "spriteId": 104, "spriteGuid": 9103745171752747009, ... }, "Spawner": {...} },
      "scripts": [ { "guid": 0, "class": "PlayerMovement" }, { "guid": 0, "class": "PlayerCombat" } ] } ],
  "tilemap": { "terrain": "~MC#Ae...", "collision": "~Qj0..." } }
```

- 大数组（地形/碰撞层）用**行程编码（RLE）字符串**嵌入——移植 yami `codec.ts` 的算法思想到 C++（编码器 ~100 行），文件既保持"文本可 diff"又避免数 MB 的数字数组。
- 格式三处消费（编辑器、运行时、C# SDK）共用同一 C++ codec；C# 侧不需要读写场景文件（经 API）。

## 4. Prefab

- 扩展名 `.prefab`（与 Unity 一致，2026-09-19 定）；JSON 结构同场景实体段；支持嵌套（Prefab 引 Prefab）与实例覆盖（`overrides` 深合并，字段级）。
- 运行时 `Instantiate` 读资产缓存（无 JSON 解析热路径——编辑器导入时烘焙为二进制紧凑格式，`.baked`，发布只带烘焙产物）。
- **双格式策略**：开发态 JSON（diff 友好），发布态烘焙二进制（加载快 + 轻度混淆）；`Tools/packager` 负责转换与校验。

## 5. 图集打包

- 规则：`Assets/**` 中被标记进图集的 sprite → 每"图集组"一页 4096（像素风默认 Point 采样、禁旋转可选）；MaxRects（Prowl2D M0 同算法思想）。
- 编辑器时打包 + 增量维护（增删 sprite 只重排受影响页）；打包命令行版进 `Tools/`（CI 可跑）。
- 运行时零解析：spriteId → (页, uvRect) 静态表（02 §3.2）。
- **M5 批③最小集注记**：MaxRects 打包前，已落"单页网格切片"（一 PNG 一页 +
  连号切片块，见 §2.2 注记）——切片消费面 = clip 帧引用；sprite 槽直接引用切片的
> UI 与跨表打包归本节 M6c 工作（2026-09-24 重排）。
- **M7a 批⑥ 落地注记（2026-10-04）**：图集 `.baked` v1 已定形——容器 `LAT1`
  （字节表与装箱语义见 [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)
  M5 定稿）：packager 侧 writer（`Engine/Assets/AtlasBake`，shelf 确定性装箱 +
  页裁剪 + RAW RGBA 载荷）现烤入包 `data/.lemon/baked/atlas/atlas.baked`，sprite
  源+.meta 不入包；运行时 reader（`Engine/Assets/AtlasStore`）页位图直传 + 按
  manifest 号账登记，**零 PNG 解码**。编辑器维持一文件一页（D4 默认 packager
  专用——回归面/金回放零影响）；MaxRects 全量升级与多图集组归后续（批⑥ 登记
  项）。模板实测：7 精灵 → 1 页 124KiB、纹理槽 9→3、磁盘 48KB PNG → 124KiB RAW
  （膨胀 2.6×，压缩归 M7b）。

## 6. 发布管线与 Steam

### 6.1 打包流程（`Tools/packager`，一键出包）

```
项目校验（孤儿资产/缺失依赖/GUID 冲突）
→ 资产烘焙（JSON→.baked；图集终打包；可选 ASTC/压缩纹理）
→ 脚本发布（用户 DLL + 依赖；可选 NativeAOT，04 §8）
→ 组装：runtime/（GameEntry+引擎库+dotnet）+ data.pak + 图标/元数据
→ 输出：Win x64 zip + 安装器（M7 定 NSIS 或 MSIX）+ Steam depot 布局
```

- 平台矩阵 v1：`windows-x64`；`macos-universal`（MoltenVK 路径，开发可用级）为第二批。
  > **2026-10-01 注记（用户拍板）**：目标平台集合不变（windows-x64 仍为发布 v1）；**M7a 内实现顺序 mac 先行**——运行时资产层/GameEntry/packager 判据先在开发机闭环，Windows 真机收口为 M7a 末批。拆解见 [Plans/M7a/M7a.md](../Plans/M7a/M7a.md) §0。
- **Steam**：steamworks 动态加载薄封装（成就/云档/富存在），构建产物对齐 depot 上传布局（steamcmd）；成就/云档配置在 `project.lemon` 声明。
- 产物清单对标 yami `Deployment`（三平台产物 + 加密资源 + 外壳），但外壳是我们自己的原生 runtime，非 Electron。

## 7. 模板项目（开箱即用的核心交付）

| 模板 | 内容 | 验收 |
|---|---|---|
| **vs-survivor**（首发） | 一张竞技场、玩家 8 向移动、3 种武器（直射/环绕/穿透）、经验宝石与升级三选一、波次导演、Boss 计时、HUD（血条/经验/计时/击杀数）、死亡结算 | 新建 → Play，10 分钟完整一局可玩 |
| **tower-defense**（M6c） | 网格地图、路径点、3 种塔（单体/溅射/减速）、波次表、经济、建造/升级/出售 UI | 10 波完整通关 |
| **incremental**（M6+） | 离线收益、大数值格式化（K/M/B/科学）、自动购买、升档重置 | 30 分钟循环 + 离线结算正确 |
| **blank** | 空场景 + 最小脚本 | — |

**默认素材全家桶**（以 yami `Templates/arpg-ts-chinese/Assets/` 默认素材为底包**直接采用**——MIT 许可，随模板再分发只需在发布物中保留版权声明（`THIRD_PARTY.md` + 致谢页）；风格缺口与原创内容自备/CC0 补齐）：

- 通用英雄（4 向 8 帧）、通用怪物 3 体型 × 各 8 帧（小/中/精英）、Boss 1 只；
- 弹幕 16 款、命中/死亡/拾取特效粒子预设 8 款、飘血数字页（0-9 + 暴击色）；
- 地形 tileset（地面 2 风格 + 自动瓦片全套变体）、装饰物 16 款；
- UI 九宫格框/按钮/血条/图标 32 枚、BGM 2 首 + 音效 24 条（攻击/受击/死亡/拾取/升级/建造）。

素材清单进 `Samples/`（与压测场景共用，bench-mow 直接用默认怪物——性能验收与默认素材永远同步，不会"压测用专用素材、交付却缺货"）。
> **第一批已入库（M5 批③，2026-09-23）**：`Samples/Assets/yami-dungeon/`——hero×2 /
> monster×2 / boss 精灵表（16px/32px 格，帧数与 yami .anim hframes 核对）+ 3 份
> `.clip` 样例 + README；`THIRD_PARTY.md` 已登记（MIT）。机械验证 = `--smoke-anim`
> （程序化表必验 + yami 表在场即验）；bench-survivor 动画化暂用程序化表自播种
> （hermetic），"bench 直接用默认素材"全面接轨推 M6c 模板打包。音效归 M6b；
> UI/tileset 后续批。
> **vs-survivor 模板已交付（M5 批④，2026-09-23）**：`Templates/vs-survivor/`——完整
> 项目（yami 5 表 + 3 clip 拷贝 + 程序化 gem/bullet/pierce/blade + 6 prefab 固定
> guid + Main.scene 玩家/导演 16 波含 Boss + Game/PlayerBehaviour 单脚本全家桶）。
> 向导"选模板 → 复制模板"流程落地（ProjectWizard 模板分支：目录拷贝 + project.lemon
> 重写（新项目 GUID = 存档隔离键）+ csproj HintPath 重锚；资产 GUID 不重生成）。
> 生成器 = `--gen-vs-template`（改玩法后重跑重生成）；机械验收 = `--smoke-template`
> （进回归第 13 步：向导复制 → build → Play → HUD 四要素/存档载入回显/波次/击杀/
> 升级卡片出现-选择-隐藏断言）。多脚本（scripts[] schema）M5 未落地——模板以
> 单 PlayerBehaviour 规避，余项挂 M6a（2026-09-24 重排）。

## 8. 运行时 UI（分阶段，决策 ADR-008）

| 阶段 | 方案 | 覆盖 |
|---|---|---|
| v1（M5） | **ImGui HUD**（游戏视口独立 ImGui 上下文，皮肤主题化）+ 位图数字/血条走 sprite 管线 | VS 三选一卡片、TD 建造栏、菜单、暂停、结算 |
| v1.x 评估 | **RmlUi 6.3 首选（ADR-008 D2，spike-04 三判据已验收）**：自研 RenderInterface over Lemon RHI；备选自研轻量保留模式 UI（回退条件见 ADR-008） | 需要复杂列表/富文本/本地化排版（ARPG 对话）时升级 |
| 恒定原则 | 世界空间 HUD（血条/飘字）永远走 sprite 渲染管线（合批零额外成本），不进 UI 框架 | yami printer/ui 分工教训 |

> **2026-09-28 触发注记（[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)）**：v1.x 升级触发条件成立（用户游戏富排版需求：菜单/图鉴/品级卡片/富文本 + 文本输入），RmlUi 正式接入排期 M6b（原 M6a 批③，五子批 ③a–③e）；脚本 API 由 ADR-008 D3 窄清单重写为 L1 机制契约 M1–M8（设计冻结、三波实现随消费者）；恒定原则不变。

> **v1 落地注（M5 批①/批④）**：HUD 通道 = World 级 `RtUi`（8 槽：key/text/frac/
> **color**——批④ 着色）+ `Cards`（三选一：ShowCards/CardPick 消费式回读），
> GameView Play 时叠加画（着色文本/进度条 + 居中卡片面板 + 数字键 1/2/3）；
> "独立 ImGui 上下文 + 皮肤主题化"推 M8 打包 HUD（编辑器内嵌 ImGui 即 v1 形态）；
> 位图数字/世界空间血条飘字（sprite 管线）**✅ M6a 批① 落地**（2026-09-24）：
> World 级 `FxChannel`（RtUi 同款第三呈现通道，不入 StateHash）——飘字 = 内置
> 位图字体页（5×7 复用，层 252 文本段合批，寿命 0.8s 上浮淡出）+ 血条 = 白精灵
> 双四边形（层 251 精灵段，按实体锚定 sticky 自隐）；池化 256/128 最老者淘汰
> （03 §10）；C# `Lemon.Fx`（vtable 尾加 `fxPopup/fxBar`）。bench-survivor 饱和
> 口径（256+128 全量在场）fps=78（判据 ≥45，09 §6.10 台账）。

## 9. 本地化

- 字符串表：`Data/locales/{zh-CN,en}.json`（键值 + 复数规则）；编辑器 StringTable 面板 + 脚本 `Tr.Key` API。
- 字体按 locale 切换字体资产；C# 脚本字符串编译期扫描提取（Source Generator，v1.1——2026-09-24 重排：v1 中文单语即可）。

## 10. 存档（双通道，接口形状借鉴 yami `data.ts`）

```csharp
public static class Save
{
    public static void Set(string key, in SaveBlob blob);      // 结构化 blob（内部 msgpack 段）
    public static SaveBlob Get(string key);
    public static void Flush();                                 // 帧末合并写
    // 通道自动选择：桌面 = 文件（%USERPROFILE%/AppData/.../MyGame/slot_N.sav）
    //              Steam 启用时自动云同步（Steam AutoCloud 声明式，零代码）
}
```

- 存档 = 场景快照序列化器输出（03 §13）+ 用户数据段；gzip + 版本头；`slot_N + settings + meta` 三类。
- 防损坏：写临时文件 + 原子改名；保留上一代备份 `slot_N.bak`。
- 增量品类长档：增量游戏模板走"数值快照 + 时间戳"，离线结算在加载时一次追算（确定性模拟保证一致，03 §12）。

> **M5 批④ 落地口径（2026-09-23 修订）**：用户数据段已交付——`Lemon.Save`
> （Set/Get/SetString/GetString/HasKey/Flush）→ World 级 SaveChannel（内存 KV）→
> 宿主 IO 钩子落盘（编辑器 = `.lemon/saves/game.sav`）。**M5 版简化三处**：
> ①单档单文件（slot_N/settings/meta 三类分档推 M6a 批②，模板以 key 前缀区分语义如
> `vs.best`）；②定长头二进制无压缩（gzip 归 M7 packager 引 zlib 时一并）；
> ③场景快照入档推 M6c 后（VS 一局 10 分钟无续局刚需；增量品类"数值快照 + 时间戳"
> 口径即本形态）。防损坏三件套齐：版本头 + 原子改名（tmp→rename）+ `.bak`
> 上一代备份（坏档自动回退）。EnterPlay 自动载入 / ExitPlay 兜底落盘 / Flush 显式
> 立即落盘（幂等）。

> **M6a 批② T5 分档落地（2026-09-28 修订，①收口）**：三档三文件
> `.lemon/saves/{slot_0,settings,meta}.sav`——格式复用零版本变（LEMONSAV v1），
> 每档独立 SaveChannel + 独立坏档兜底（主→bak 按档隔离）；World 持三通道，
> C# 五方法可选参 `Save.Chan`（Slot/Settings/Meta，默认 Slot = 既有源码零改；
> Flush 全档）；vtable 尾加 Ex 三项（旧宿主判空回落单档）。旧 `game.sav`
> **惰性迁移**：slot_0 载入时新档不存在 → 读旧名（写恒写新名，免 rename 竞态）。
> 多档切换 API 仍裁（v1 固定 slot_0，v1.1 候补）。**档内结构 = 纯 KV 引擎不
> 解析**，键约定防后续撞僵 schema（2026-09-28 用户口径）：settings = 版本化 KV
> （首键 `version` = 键集结构版本，设置项键自由增长——M6b 音量/手柄键位/画质
> 直接加键）；meta = 收集条目 `col.<条目id>.state` / `col.<条目id>.count`（条目
> id → 状态/计数映射）+ 全局统计平键（模板 `vs.best` 已迁 Chan.Meta）。

## 11. 可视化事件系统（schema 冻结，编辑器后评估）

- 数据格式**预留并冻结**（借鉴 yami `.event` 指令树 schema，非代码移植）：

```json
{ "type": "onEntityDeath", "filter": { "team": "monsters" },
  "commands": [ { "id": "game.spawn", "params": { "prefab": "...", "pos": "$event.pos" } },
                { "id": "var.add", "params": { "name": "kills", "value": 1 } } ] }
```

- 运行时：指令树在加载时**预编译为命令数组**（yami `CommandCompiler` 闭包思想 → 我们编为 C++ 函数表索引 + 参数块，C# 侧可为委托），未触发零成本。
- 触发源复用 03 §11 事件队列；ARPG 品类（对话/剧情/任务）真正需要时再投资编辑器 UI（yami 22.5k 行教训，05 §8）。

## 12. 移植对照速查（本册）

| 项 | 源 | 处置 |
|---|---|---|
| GUID + manifest schema、meta 思想 | yami `Data/manifest.json`、`file.js` | **schema 照抄**（数据格式非代码） |
| 场景 JSON + RLE codec | yami `.scene` + `Script/codec.ts` | 算法思想移植 C++ 重写（~100 行） |
| Deployment 产物清单/Game Builder 流程 | yami `title.js:973`、`Tools/Game Builder` | 流程与产物清单对标，实现全新 |
| 双通道存档接口形状 | yami `data.ts:745-1091` | 接口形状借鉴 |
| FileWatcher / .meta+hash / Importers | Luma `Resources/`、`Utils/FileWatcher` | 结构移植（MIT），裁剪到 2D |
| MaxRects 图集 | Prowl2D M0（`SpriteAtlasRegistry`） | 算法同源思想，C++ 重写 |
| Tilemap 序列化（gzip 版本化） | duality Tilemaps 插件 | MIT 拷贝思想 |
| 模板默认素材 | yami `Templates/arpg-ts-chinese/Assets/` | **素材直接采用**（MIT，发布物保留声明 + THIRD_PARTY 登记），缺口自补 |
