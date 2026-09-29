# 批③d 前置：UIDocument 场景挂载——.rml 文档组件化 + 进 Play 自动装载 + Show 落空兜底

- 日期：2026-09-29
- Status: **done 代码面（2026-09-29 T1–T6 全落；真人验收判据 6 三件余用户，不阻塞 ③d 开工）**
- **2026-09-29 审核修订（开工前解冻再冻结）**：M6b 开工前第三方审核发现三处硬伤 + 两项关联决策拍板，本文件同步修订——①T1 布局字数错误（8B → 16B）+ 补注册 id（=30）；②T3 的 modal 归位缺引擎侧写入口，补 `ShowDocument` 尾加形参任务项；③§3 归位规则与 ③b 双击预览语义打架（C5），落 stale 位口径消歧；④D1 层序路线拍板「甲-轻量：Show 即提层」，落进 T2/T3；⑤关联决策 B1（dp 坐标系）与 D2（③d 两拆）归 [M6b.md 决策注记](./M6b.md)，本批不含 dp。
- 归属：M6b 游戏UI产品壳（原 M6a 批③，2026-09-29 迁；总览页 [M6b.md](./M6b.md)）
- 关联：[ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md) M1（装载语义补章，T6 落注记）· [批③c](./2026-09-28-b3c-csharp-ui-api.md)（C# API 已交付，本批零改动）· [③b 批文件](./2026-09-28-b3b-ui-font-asset-channel.md)（双击手动通道 = 本批取代对象）
- 性质：③d 模板迁移（六屏）的公共地基——每屏都要"装载 + 显隐管理"，本批把这两个动作从口头约定升为场景数据。

## 决策记录（2026-09-29 与用户对齐）

1. **挂载粒度 = Unity UIDocument 同构**（一组件挂一个文档资产），否决 uGUI 式逐元素实体化（同步地狱/实体爆炸/StateHash 纪律破坏/UI Builder 级投资，且 ADR-014 D2 的墙明确挡这个）。
2. **不给引擎引入实体 active**——UIDocument 组件自带设计期显隐位（`showOnStart`），效果等价、成本一天 vs 一月。
3. **EnterPlay 归位（大扫除）**——每次进 Play 全部界面重置为场景声明态；未声明的动态屏 Hide 保留装载。
4. **C# 动态寻址维持 relPath + "UI 资产不挪"约定**；GUID 重载/组件查询留 v2（对齐 Unity `GetComponent<UIDocument>()` 的后手）。
5. **命名对齐 Unity**：组件名 `UIDocument`、字段 `sourceAssetGuid`（Unity `sourceAsset` 的 GUID 形态）、菜单 Create → UI Document。与 Unity 的两点有意差异（共享文档模型 vs 私有克隆；组件显隐位 vs GameObject active）见设计定案 §1。
6. **Edit 模式预览不做**（二期——双态管理复杂度翻倍，接口留位）。

## 背景与动机

③b 实现了双击 .rml 手动装载（`EditorApp::LoadUiDocument`），代码注释留话"③c C# 装载通道落地前的手动通道"——但 ③c 批文件设计定案时**静默遗漏**了装载通道（T1–T6 无对应任务项，smoke 靠 `SeedSmokeUiDocument` 预载夹具绕开）。现状 `UI.Show(doc)` 对未装载文档 = 响亮契约失败，真实项目（svr-test）每会话需手动双击装载，不可靠。本批补上这块地基：**场景声明式装载（治本）+ Show 落空兜底（治时序与动态屏）双通道**。

## 现状盘点（2026-09-29 摸底，均已对码核实）

| 事实 | 位置 | 含义 |
|---|---|---|
| 引擎无实体级 active/enabled（仅 World 级） | `Engine/ECS/World.h:60`、Hierarchy/Entity 无 enabled 位 | "开关实体=显隐"无底座 → 组件承载显隐位（决策 2） |
| `EnterPlay` = 快照固化 → 建 playWorld；`ExitPlay` = 弃 playWorld、editScene 从快照重建 | `Editor/EditorContext.h:134-135` | Play 期组件写入天然不回写编辑场景 ✓ |
| EnterPlay/ExitPlay 完全不碰 gameUi_（全仓仅资产删除路径调 `UnloadDocument`） | `EditorApp.cpp:2343` 唯一调用 | 跨 Play 会话残留态无人清理（坑 R2 实锤） |
| `UnloadDocument(name)` 已存在 | `Engine/Ui/UiSubsystem.h:72` | LoadScene 档2 前瞻不堵死 |
| 文档主键三态：场景组件（将用 GUID）/ C# `UI.Show`（relPath）/ UiSubsystem+watcher 对账（relPath） | UiSubsystem docs map、③b watcher | 双主键漂移风险（R3），寻址约定见设计定案 §4 |
| 组件定义分布 + 注册目录 + C# 镜像 | `Engine/Components/{Core,Render,Gameplay,Behavior}Components.h` + `ComponentCatalog.cpp` + `Lemon.SDK/Interop/Components.cs` | T1 落点；POD + 布局冻结 + 两侧同步纪律（SpriteRenderer 先例） |
| 进 Play 咽喉点单一 | `EditorApp::TryEnterPlay() :2822`（三处 UI 入口均经此） | T3 装载钩点位（EnterPlay true 后、首帧 TickPlay 前） |
| 资产选择器 / GUID 字段渲染 / AssetType::Rml 过滤 | Inspector + AssetBrowser 既有先例 | T4 编辑器三件低风险 |
| smoke-uirml 现走 `SeedSmokeUiRmlProject` + `SeedSmokeUiDocument` 手动播种 | `EditorApp.cpp:3053/3075` | T5 迁移对象（双通道断言） |

## 设计定案（实现前冻结）

### 1. UIDocument 组件（Unity 同构；Engine/Components/UiComponents.h 新组首件）

```cpp
// Lemon 引擎 — 组件目录 · UI 组首件（M6b 批③d 前置，原 M6a 批③；Unity UIDocument 同构挂载粒度）
// 一组件 = 一 .rml 文档资产 = 一屏（ADR-014 M1 一屏一文档）。运行时状态（装载句柄/
// shown 态）全在 UiSubsystem（name→doc map），**严禁回写本组件**（快照确定性铁律）。
// 布局：u64+u8+u8+u16 裸 12B、自然对齐 sizeof=16/alignof=8（2026-09-29 审核修正：
// 原误写 8B；C# 镜像 Sequential 无 Pack 逐字节同构，勿手写 elemSize——注册一律 sizeof）。
struct UIDocument {
    uint64_t sourceAssetGuid = 0; // .rml 资产 GUID（Unity sourceAsset 的 GUID 形态）；0 = 未挂（合法，Inspector 提示）
    uint8_t  showOnStart = 1;     // 进 Play 即显；0 = 装载但隐藏（死亡/结算类动态屏由 C# UI.Show 点亮）
    uint8_t  modal = 0;           // M1 模态标记初值（运行时 UI.Show(doc, modal) 可覆写）
    uint16_t reserved = 0;        // 布局余量（尾加纪律）
};
```

- **与 Unity 的两点有意差异**（讨论定案，ADR-014 M1 注记收录）：
  - Unity 每组件私有克隆一整棵树、退 Play 随场景销毁（结构免疫残留）；Lemon/RmlUi 全局共享文档（docs map 按 relPath 单键）——这是 ③c C# 按名寻址 API 的地基，改私有克隆 = 推翻 ③c。等价物 = EnterPlay 归位（§3）。
  - Unity 用 GameObject active 当显隐；Lemon 无实体 active（决策 2），组件 `showOnStart` 承载。**"编辑期临时隐藏"与 Edit 预览一起留二期**（v1 显隐语义 = showOnStart）。
- Play 期翻 `showOnStart`/`modal` 字段 = 只影响下次进 Play（快照不回写）；运行时显隐请用 C# `UI.Show/Hide`（设计期/运行时两通道不打架的单一约定）。

### 2. 装载双通道（声明式为主，兜底为辅，谁先谁后都收敛）

- **通道 A（声明式，治本）**：`TryEnterPlay()` 内 `ctx_.EnterPlay()` 成功后、首帧 `TickPlay` 前——扫描 **playWorld** 的 UIDocument 组件：`sourceAssetGuid → AssetDatabase 解析（FindByGuid，type==Rml，missing = LEMON_ERROR 响亮 + smoke 断言）→ LoadDocumentFromFile(relPath, absPath)（复用 ③b 装载机 + watcher 对账 + DocumentReloaded 事件，零新机制）`。同 GUID 多实体 = 去重装载 + LEMON_WARN（一屏两实体无意义）。装载后按 `showOnStart` Show/HIDE、`modal` 置位。
- **通道 B（兜底，治时序与动态屏）**：`UiSubsystem` 增 `SetDocumentResolver(std::function<bool(const char* relPath, std::string& absPath)>)`（头文件零 RmlUi 类型，EditorAssetHooks 先例形态）；`ApplyOps` 中 `FindDoc` 未命中且 op 为 `Show` 时经 resolver 现载。EditorApp 在 gameUi_ Init 处装配（`FindByPath(relPath) → type==Rml → AbsolutePath`）。未装 resolver 的宿主（M8 前裸运行时）维持现行响亮失败。
- 时序保证：C# 即便在 Awake/Configure 期就 `UI.Show + Apply`（ops 于 TickBatch 尾到达），通道 A 已先行；若 C# 早于一切（理论窗口）或动态屏未声明，通道 B 兜住。
- **同一批 ops 内顺序契约**（沿用 ③c）：`Show` 先行，后续 SetText/SetItems 同批可达（装载在 Show op 处完成）。

### 3. EnterPlay 归位（大扫除）与 ExitPlay

- **归位判据 = stale 位（2026-09-29 审核消歧 C5）**：`Doc` 增 `shownDuringPlay` 标记——**只在 ApplyOps 的 Show op 路径置位**（C# 动态 Show）；`ShowDocument`（③b 双击预览、通道 A 声明装载）**不置位**。这样"上局动态 Show 过"（要归位 Hide）与"Edit 期双击装载"（③b 承诺「进 Play 后 GameView 显示」，保持 shown）两个来源无需 R10 的完整装载来源标记即可区分。
- **EnterPlay（通道 A 末段，一次性归位全部已装载文档）**：场景声明的屏 → `showOnStart`/`modal` 声明态 + 清 stale；**场景未声明且 stale 的文档 → Hide（装载保留，下次 Show 免 IO）+ 清 stale**；未声明且非 stale（Edit 期双击装载）→ 保持现状。消灭 R2 残留（第二局 HUD 带上局数据 / modal 残留 / 死亡框没收）。
- ExitPlay：不主动卸载（Edit 期不渲染，无害）；归位统一在下次 EnterPlay 做（单一时点，简单）。
- **层序（2026-09-29 拍板 D1 甲-轻量）**：`ShowDocument` 与 ApplyOps 的 Show 分支在 `doc->Show()` 后**显式 `PullToFront()`**——层级序 = 最近 Show 序，装载序为初值；模态屏总在后 Show 自然压 HUD。纯文本文档（无焦点元素、原本不会被焦点副作用提层）从此有确定语义。ADR-014 M1 注记随 T6 落。

### 4. 寻址约定（R3/R4 处置）

- 场景侧主键 = `sourceAssetGuid`（Unity 同级，资产挪位不断）；C# 侧主键 = relPath 字符串（保可读；断 = 响亮失败）。
- **约定：UI 资产（.rml/.rcss）建后不挪不改名；挪动 = 全局搜索替换 relPath**（= Unity Resources.Load 使用者的日常纪律）。v2 后手：C# 组件查询（`GetComponent<UIDocument>()` 读 sourceAssetGuid 对应 relPath）或 `UI.Show(Guid)` 重载——登记不实现。
- missing 资产（guid-chain 同款模式）：EnterPlay 响亮失败，**绝不静默空屏**；按 guid-chain 先例 per-entity resolve。

### 5. 铁律与护栏

- **禁写回组件**：装载钩/引擎绝不往 UIDocument 写运行时字段——快照确定性、序列化纯净、ExitPlay 重建零漂移。运行时状态只进 UiSubsystem。
- **基准护栏**：无 UIDocument 的场景（bench/replay/基准场）装载调用数 = 0——smoke 断言钉死，防装载点被挪进通用路径弄脏基准。
- **运行时动态加 UIDocument 组件不生效**（装载钩只认 EnterPlay 扫描；每帧扫描是浪费）——文档登记，v1 静默忽略 + 批文件此处即登记。
- **StateHash/回放**：组件数据入 Play 快照（确定性）；UI ops/事件照旧不入哈希/输入快照（③c 纪律不变）。
- **M8 打包 v1 = 全部 Rml/Rcss 资产入包**（文本资产小），不搞"场景=清单"硬约束；瘦身需求出现再上场景扫描。

## 任务分解

### T1 组件（Engine/Components + SDK 镜像）

- 新 `Engine/Components/UiComponents.h`：`UIDocument`（上述 sketch）+ 注册进 `ComponentCatalog.cpp`（序列化/Inspector 泛型渲染即刻可用）。**注册 id = 30**（登记顺序即 id、只增不改序——2026-09-29 审核时点现有 30 项 id 0..29；实现期以目录实际尾部为准，勿凭本文件数字硬编码）。
- `Lemon.SDK/Interop/Components.cs` 同步镜像（IComponent，**16B 布局冻结**——2026-09-29 审核修正，原误写 8B；SpriteRenderer 同款注释纪律）。
- Inspector 特化：`sourceAssetGuid` 字段 = .rml 资产选择器（`AssetType::Rml` 过滤；0 = "未挂"提示）。

### T2 通道 B：Show 落空兜底（Engine/Ui + Editor 装配）

- `UiSubsystem` 增 `SetDocumentResolver` + `ApplyOps` Show 分支（FindDoc miss → resolver → LoadDocumentFromFile → 重试；resolver 未装 = 现行 ContractFail）。
- ApplyOps Show 分支顺带落两件（§3）：置 `shownDuringPlay`（stale，归位判据）；`doc->Show()` 后 `PullToFront()`（层序 = 最近 Show 序）。
- EditorApp gameUi_ Init 段装配 resolver。

### T3 通道 A：EnterPlay 装载 + 归位（EditorApp）

- **前置（2026-09-29 审核补 A4）：`UiSubsystem::ShowDocument` 尾加 `bool modal = false` 形参**——modal 的场景声明态写入口（原本置 true 唯一路径是 ApplyOps 的 C# Show op，T3 的「modal 归位」无 API 可调）；尾加默认参零破坏既有调用（EditorApp 双击路径、smoke 调用点不动）。顺带本 API 内落 §3 的 Show 后 `PullToFront()`。
- `TryEnterPlay()`（:2822）`ctx_.EnterPlay()` true 后：扫 playWorld UIDocument → GUID 解析/去重/装载 → showOnStart/modal 归位（经上述形参）→ 未声明且 stale 的已装载文档 Hide + 清 stale（§3 口径：Edit 期双击装载的非 stale 文档保持 shown）。
- 断言/日志：装载成功数、missing 数、去重警告——进 Console。

### T4 编辑器三件（Editor/Panels）

- Hierarchy Create → **UI Document**（弹资产选择器；取消 = 不建实体）→ 建实体挂 UIDocument（命名 "UIDocument"/"UI" 递增，照抄现有 Create 模式）。
- Inspector：资产选择器 + showOnStart/modal 勾（T1 特化内含，此处验收交互）。
- 层级面板图标/文案（组件图标先例照抄）。

### T5 验收资产（smoke 双通道 + 回归）

- smoke-uirml 迁移：夹具场景挂 UIDocument 实体（主文档走通道 A）+ C# 动态 `UI.Show` 第二文档（走通道 B）——**双通道各至少一条断言**（装载计数/可见像素/契约错误恰为已知反例数）。
- 基准护栏断言：无 UIDocument 场景装载调用 = 0。
- Play→Stop→Play 循环断言：第二局无残留（uiev 类回执或像素断言）；stale 口径断言——动态 Show 过的第二文档第二局被 Hide，Edit 期双击装载的文档跨 Play 保持可见（③b 预期延续，§3）。
- 层序断言（§3 D1）：两文档先后 Show，后者在上的像素/回执验证。
- 回归 15 步结构不动，uirml-chain 断言串升级（`uidoc=…` 位）。

### T6 文档落账

- ADR-014 M1 行补装载语义注记（双通道 + 归位 + 寻址约定 + 与 Unity 两差异）；M6b.md 批次表加本批行；DevLog 收口条目；AGENTS.md 批③段落同步。

## 验收判据（全过才勾销）

1. ✅ smoke-uirml 双通道全绿（通道 A 声明装载 + 通道 B 落空装载各有断言），exit 0，验证层零错——
   双模式（--script / 无脚本）各一次：`uidoc(a=1/b=1/c=1 loads=4/1) layer(bTop=18409/18180 aTop=0)
   p2(stale=1 keepC=1+8176px dyn=OK)`（脚本模式另有 items=2/1 ev=c1r4 contract=1/textOK）。
2. ✅ script-tests 通过（组件计数 30→31 四处同步；C# 镜像布局双向对拍含 UIDocument）。
3. ✅ 回归 full 15/15（uirml-chain 断言串升级 `uidoc=…` 位后；2026-09-29 首跑 13/15——
   script-tests 的 TestUiSdk op 计数 4→6 未同步 + smoke-drag 负载抖动（T1 先例），修计数后
   复跑全绿；smoke-template `uidoc=0` 位随 template-chain 在列）。
4. ✅ bench/replay 口径：装载点只在 `MountSceneUiDocuments`（EnterPlay 扫描），bench-survivor/
   bench-scene 进 Play 必经零装载；smoke-template 断言 `uidoc=0`（无 UIDocument 场景装载恒 0）。
   replay：组件数据入哈希但基准场/模板场零 UIDocument 实例 → 哈希流不变（零重录结构性成立）。
5. ✅ Play→Stop→Play 循环（smoke 帧 200 Stop / 203 重进）：第二局 UI = 场景声明态机器断言——
   动态 Show 过的 dyn 被 Hide（装载保留，第二局装载增量恰 1 = 只重装声明文档）、Edit 双击装载的
   editprev 保持可见（8176px）、A 回 showOnStart 声明态。
6. ⏳ **真人验收**（余用户，不阻塞勾销但阻塞 ③d 开工）：①Hierarchy 建 UI Document → 挂 svr-test
   的 .rml → 进 Play 见屏、退 Play 干净、再进 Play 初始态；②删 .rml 后进 Play 得红字（不空屏）；
   ③svr-test 里纯 C# `UI.Show` 动态屏不经场景声明可用。

## 实现期发现（偏离批文件预设计的落账）

- **文档热重载 = 从 context 根重挂 → 隐式提层**（D1 缺口，smoke 未脚本模式实抓：dyn Show 后
  frame100 .rml 重载把 A 提到 dyn 之上，"最近 Show 序"被装载序覆写）。修 = `Doc::showSeq`
  发号器（Show/ShowDocument(show) 发号，Hide/归位清零，新装载归零）+ `ReloadDocument`/
  `ReloadAllDocuments` 尾部按序 `RestoreDocumentOrder()` 复排——不变量跨热重载成立。脚本模式
  原本靠 DocumentReloaded → C# 重灌的 Show 自愈，纯引擎文档（无脚本管理）从此不再漂层。
- **通道 B 兜底成功不应计契约错误**：预设计的"FindDoc 落空即 ContractFail 再兜底"会把每次
  成功兜底也记一笔（smoke 实测 contract=2）——改 LookupDoc 先行，未命中 → 兜底 → 仍失败才
  响亮。FindDoc 遂无调用方，删除。
- **负面契约 op 时点 160 → 210**：EnterPlay 通道 A 重装主文档会 `InvalidateContainers`——160
  时点（第一局）建的负面容器活不到终帧（negN=-1）。移到第二局内（210 > 203 重进），
  contract 恰 1 / negN=1 两断言恢复。
- **uiev 终值捕获 238 → 199**：Play→Stop→Play 往返使第二局 RtUi = 新世界（无点击/无重载 →
  uiev 槽恒空），终帧读数必须落在第一局尾。
- **smoke-uirml 夹具改制**：主文档 uirml.rml 改由场景 UIDocument 声明（通道 A 独立验收面），
  `SeedSmokeUiDocument` 只装载 editprev.rml（Edit 双击装载代表）；新增 dyn.rml（通道 B 面板，
  场景不声明）。层序断言 = gameRT 中点两捕获（170/190 录、171/191 数色 #802040）：
  B 后 Show 在上 >500 → 175 重 Show A → <5。gameRT 实测 824×464（默认布局确定性），
  dyn 覆盖块 (400,300,160,120) 落 A 面板内、editprev (590,80,140,60) 在 A 右侧空带。

## 风险与既知边界（R1–R12 讨论落账；R1–R3 = 决策记录 2/3/4）

| # | 坑 | 处置 |
|---|---|---|
| R1 | 实体无 active 概念 | 组件 showOnStart 位（决策 2） |
| R2 | 跨 Play 残留态 | EnterPlay 大扫除（§3） |
| R3 | GUID/relPath 双主键漂移 | relPath + 不挪约定（决策 4）；v2 组件查询后手 |
| R4 | missing 资产 | EnterPlay 响亮 + smoke 断言 |
| R5 | 装载时序竞态 | 通道 A 先行 + 通道 B 兜底（§2） |
| R6 | 运行时写回组件 | 铁律禁写（§5）；运行时状态驻 UiSubsystem |
| R7 | 基准漂移 | 零装载断言护栏（§5） |
| R8 | 运行时动态加组件 | 不生效，登记（§5） |
| R9 | Edit 预览双态复杂度 | 二期不做，接口留位（决策 6） |
| R10 | LoadScene 档2 换场景卸载 | UnloadDocument 已有；装载记录带来源标记，登记不实现 |
| R11 | smoke/回归迁移 | T5 双通道断言；③d 六屏即规模化验证 |
| R12 | 编辑器三件 | 照抄既有模式，低风险（T4） |

- **已知欠账（本批不解决）**：C# 无法感知 Show 是否成功（引擎侧响亮、C# 无回执，smoke 兜底——③c 既定口径维持）；`UI.IsShown` 只读查询未提供（需求出现再议）。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D2 M1 装载语义补章 → T6）
- [批③c C# API](./2026-09-28-b3c-csharp-ui-api.md)（本批零改动其线格式与 API；通道 B 是其 Show 的兜底扩展）
- [③b 双击装载](./2026-09-28-b3b-ui-font-asset-channel.md)（保留为手动预览通道，不再是必需步骤）
- 下一批：③d 模板迁移（六屏全走本批通道 = 规模化验收场）
