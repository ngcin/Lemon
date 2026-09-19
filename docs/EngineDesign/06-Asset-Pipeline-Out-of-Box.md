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
- `.lemon/manifest.json`（项目级索引，**导入器生成物**，随状态目录 gitignore——Godot `.godot/uid_cache.bin` 同位思路；2026-09-19 定落位）：`guid → {path, type, dependencies[], hash}`；编辑器启动按 manifest 做一致性体检（孤儿 meta / 缺失依赖红字报告）。

### 2.2 导入器（编辑器侧，后台线程池）

| 类型 | 源格式 | 导入产物 | 备注 |
|---|---|---|---|
| sprite | png/jpg | 纹理 + 切片（自动/网格/手动，Prowl `TextureImporter` 思路） | 切片 GUID 稳定匹配（重导入不漂移） |
| atlas | 目录/手选集 | 图集页 + sprite 重定向表（MaxRects，Prowl2D M0 实现移植思想） | §5 |
| audio | ogg/wav | 解码缓存 + 元数据（循环点/预载标记） | miniaudio |
| font | ttf/otf | 位图字体页(v1)/SDF 图集(v2) | 02 §7 |
| clip2d | json | 帧动画资产 | AnimationEditor 产出 |
| particles | json | 发射器资产 | ParticleEditor 产出 |
| tileset | png + json | 图集 + 碰撞标志 + 自动瓦片变体表 | 05 §7 |
| curve/data | json | 曲线、Team 表、波次表 | 数据资产 |

- **热重载**：`FileWatcher`（线程轮询，Luma 同款）触发增量导入 → 按依赖图通知（纹理变更 → 重建图集页 → 受影响场景视口刷新标记）。运行中 Play 的资产热替换（贴图/参数即时生效，音效不中断）。

## 3. 场景与数据格式（JSON + 行程编码，可 diff 的紧凑格式）

- 场景 `.scene` = JSON（schema 版本化）：

```json
{ "schemaVersion": 3, "name": "Arena01", "viewport": [1920, 1080], "tileSize": 32,
  "entities": [
    { "guid": "...prefabRef...", "pos": [128, 64], "overrides": { "Chase.speed": 88 } },
    { "components": { "Transform2D": {...}, "SpriteRenderer": {...}, "Spawner": {...} } } ],
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
- **Steam**：steamworks 动态加载薄封装（成就/云档/富存在），构建产物对齐 depot 上传布局（steamcmd）；成就/云档配置在 `project.lemon` 声明。
- 产物清单对标 yami `Deployment`（三平台产物 + 加密资源 + 外壳），但外壳是我们自己的原生 runtime，非 Electron。

## 7. 模板项目（开箱即用的核心交付）

| 模板 | 内容 | 验收 |
|---|---|---|
| **vs-survivor**（首发） | 一张竞技场、玩家 8 向移动、3 种武器（直射/环绕/穿透）、经验宝石与升级三选一、波次导演、Boss 计时、HUD（血条/经验/计时/击杀数）、死亡结算 | 新建 → Play，10 分钟完整一局可玩 |
| **tower-defense**（M6） | 网格地图、路径点、3 种塔（单体/溅射/减速）、波次表、经济、建造/升级/出售 UI | 10 波完整通关 |
| **incremental**（M6+） | 离线收益、大数值格式化（K/M/B/科学）、自动购买、升档重置 | 30 分钟循环 + 离线结算正确 |
| **blank** | 空场景 + 最小脚本 | — |

**默认素材全家桶**（以 yami `Templates/arpg-ts-chinese/Assets/` 默认素材为底包**直接采用**——MIT 许可，随模板再分发只需在发布物中保留版权声明（`THIRD_PARTY.md` + 致谢页）；风格缺口与原创内容自备/CC0 补齐）：

- 通用英雄（4 向 8 帧）、通用怪物 3 体型 × 各 8 帧（小/中/精英）、Boss 1 只；
- 弹幕 16 款、命中/死亡/拾取特效粒子预设 8 款、飘血数字页（0-9 + 暴击色）；
- 地形 tileset（地面 2 风格 + 自动瓦片全套变体）、装饰物 16 款；
- UI 九宫格框/按钮/血条/图标 32 枚、BGM 2 首 + 音效 24 条（攻击/受击/死亡/拾取/升级/建造）。

素材清单进 `Samples/`（与压测场景共用，bench-mow 直接用默认怪物——性能验收与默认素材永远同步，不会"压测用专用素材、交付却缺货"）。

## 8. 运行时 UI（分阶段，开放决策 ADR-008）

| 阶段 | 方案 | 覆盖 |
|---|---|---|
| v1（M5） | **ImGui HUD**（游戏视口独立 ImGui 上下文，皮肤主题化）+ 位图数字/血条走 sprite 管线 | VS 三选一卡片、TD 建造栏、菜单、暂停、结算 |
| v1.x 评估 | RmlUi（RCSS/HTML 风格、C++、有 Vulkan 社区后端）或自研轻量保留模式 UI | 需要复杂列表/富文本/本地化排版（ARPG 对话）时升级 |
| 恒定原则 | 世界空间 HUD（血条/飘字）永远走 sprite 渲染管线（合批零额外成本），不进 UI 框架 | yami printer/ui 分工教训 |

## 9. 本地化

- 字符串表：`Data/locales/{zh-CN,en}.json`（键值 + 复数规则）；编辑器 StringTable 面板 + 脚本 `Tr.Key` API。
- 字体按 locale 切换字体资产；C# 脚本字符串编译期扫描提取（Source Generator，M6）。

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
