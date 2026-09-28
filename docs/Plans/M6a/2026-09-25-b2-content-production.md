# M6a 批②：内容生产 —— 配置表资产 + AnimationEditor 最小版 + 技能数据化 + 存档分档

Status: done（2026-09-28 T0→T6 全勾；T5/T6 收口记录见 §5 尾）

> 拆分自 [M6a 总览](./M6a.md)（08 §2 M6a WBS 第 3 条）。四条腿：**A 线 = 数值配置外置**
> （ADR-012 双轨——[ADR-012](../../ADR/ADR-012-Config-Table-Dual-Track.md)）；
> **B 线 = AnimationEditor 最小版**（05 §7 既列，面板集解冻）；**C 线 = 技能作者路径
> 数据化**（模板消费 A 线产出，兑现验收②）；**D 线 = 存档分档**（06 §10 推迟项收口）。
> 动工前必读：[ADR-012](../../ADR/ADR-012-Config-Table-Dual-Track.md)、
> [06 §10](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（存档双通道）、
> [05 §3/§7](../../EngineDesign/05-Editor.md)（面板集冻结决议 + 内容编辑器三件套）、
> [09 §6.8](../../EngineDesign/09-Testing.md)（零重录三级口径——先例二/三是本批的
> 回放安全依据）。

## 1. 现状盘点（2026-09-25 逐行核对，两路深读）

### 1.1 A/C 线：数据资产通道不存在，数值全在代码里

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| A | AssetType 五值枚举，TypeOf 按扩展名匹配，`.meta` 由 SyncMeta 补齐（guid/type/hash/importer 段） | `AssetDatabase.h:26`、`AssetDatabase.cpp:158-167, 228-257` | 加 `.table` = 枚举 + TypeOf + AssetTypeName 三点；**首条核对项 = .meta type 是字符串名还是数值序**（字符串则插位自由，数值必须尾加保序） |
| B | JSON 解析用 nlohmann（clip/场景/prefab 全走它），原子写统一 `WriteFileAtomic` | `EditorContext.cpp:11`、`AssetDatabase.h:21-24` | 表资产零新依赖，红线（新增库登记）不触碰；CSV 解析自写 mini（~100 行：引号/逗号/换行/BOM） |
| C | C# 零文件 IO；资产 API 仅 SpriteOf/Instantiate.Prefab/Anim.ClipId；vtable 五轮表尾追加先例（M4.4→M6a①） | `Lemon.SDK/Assets.cs:10-42`、`ScriptHost.h:36-64` | 读表 = vtable 尾加 3 项 + SDK 包装；空宿主降级先例（SpriteOfGuid 返 0） |
| D | World 级"持有 + 非 ECS"通道先例 ×4（Clips/Saves/RtUi/Fx）——全部零重录（09 §6.8 先例二） | `World.h:131-150`、`ClipTable.h:16-34` | TableStore 第五通道同款；EnterPlay 快照语义（BuildPlayClipCache `EditorContext.cpp:530-576` 同位置加 BuildPlayTableCache） |
| E | 升级池 = 硬编码数组 + switch；环绕刃 = 纯 C# 轨道 + prefab 常量；穿透 = 切 projectileId | `PlayerCombat.cs:20-22, 94-112, 136-172` | C 线模板侧落点：池/武器参数进表，ApplyOption → kind 派发（2026-09-28 落点二稿：模板面保留 + svr-test 同步面，见 §5 T4） |
| F | 用户游戏已是"复制 behaviour + 手填常量"作者路径（≈19 常量 + 复制池 + 第二份常量表） | `svr-test/PlayerBehaviour.cs:17-41`、`AllyBehaviour.cs:20-46, 63-140` | ADR-012 需求证据；原"不做代码侵入、注记自迁"口径 **2026-09-28 修订（二稿）**：T4 同步落地 svr-test 并承验收② 演示（用户拍板，见 §5 T4 / ADR-012 修订注记） |
| G | xpCurveK 引擎硬编码常量，消费于 xpToNext 组件字段演化 | `Systems.cpp:791-795`、`GameplayComponents.h:56` | 曲线可配 = World 级单参数（TimeScale 同款），默认值不变即零重录（ADR-012 D3） |
| H | 波次已数据化（WaveDirector waves[16] 组件数组段，Inspector 可编辑，入哈希） | `BehaviorComponents.h:126-152`、`Systems.cpp:85-161` | ADR-012 D2：不外置，波次留组件面 |

### 1.2 B 线：clip 无编辑器产出通道

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| I | `.clip` schema = {schemaVersion, name, fps, loop, frames[{sheet, cell}]}；解析进 World::Clips() 只在 EnterPlay 一次性快照 | `EditorContext.cpp:523-576`、实样 `Samples/Assets/yami-dungeon/hero-walk.clip` | 最小版保持 schema 不动（全片 fps + bool loop）；写回生效点 = 下次 EnterPlay |
| J | 现有 clip 产出 = 手写文件 / 模板生成器直写 JSON 串（固定 GUID） | `EditorApp.cpp:127-160, 317-330` | AnimationEditor 是首个"读-改-写 clip 资产"的编辑器面 |
| K | AssetBrowser 双击仅 sprite 建实体；拖拽载荷已有 kind=4 (clip) | `AssetBrowserPanel.cpp:270-317`、`BuiltInPanels.h:155-159` | "双击资产 → 打开专用编辑面板"机制不存在，B 线新造（首个先例） |
| L | 面板注册 = IEditorPanel + CreateAllPanels 唯一登记点 + DockBuilder 布局；ConsolePanel 是最小面板样板（~110 行） | `Panel.h:15-24`、`Panels.cpp:133-143`、`EditorApp.cpp:1173-1189` | 新面板动 4 处：BuiltInPanels.h 声明 + 新 .cpp + Panels.cpp + CMakeLists + Dock 布局；05 §3 面板集冻结决议需解冻注记 |
| M | 精灵槽/clip 槽控件先例（DrawSpriteSlot/DrawClipSlot）；缩略图 = AssetGpu().Thumbnail(guid) | `InspectorPanel.cpp:172, 242`、`AssetBrowserPanel.cpp:252-266` | 帧编辑行控件与预览直染可复用同款素材 |

### 1.3 D 线：存档单档单文件，读写集中三函数

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| N | game.sav = LEMONSAV 头 + KV 段（版本 1，防损坏三件套齐）；SaveChannel 类是纯内存 KV | `SaveChannel.h:4-34`、`SaveChannel.cpp:83-124`、`EditorContext.cpp:709-757` | 拆档零格式变更：每档一个 SaveChannel 实例 + 独立文件，类零改动 |
| O | 路径/落盘/载入三函数单点 + 三调用点（EnterPlay:784 载入 / ExitPlay:812 落盘 / HookSaveFlush `EditorApp.cpp:87-89`） | `EditorContext.cpp:709-757` | 分档改动面集中；旧 game.sav 迁移走**惰性读**（slot_0.sav 不存在且 game.sav 存在 → 读旧名，写时写新名——免 rename 竞态） |
| P | C# Save 六方法全走 vtable 四指针（SaveSet/GetLen/Get/Flush）；模板用 key 前缀 vs.best 语义分档 | `Save.cs:15-43`、`NativeApi.cs:25-28`、`PlayerCombat.cs:77, 184-185` | 分档 API = vtable 尾加 Ex 三项（尾参 channel）；C# 可选参数默认 Slot = 模板零改源码 |
| Q | World 只挂单 saves_ 通道 | `World.h:147-150, 204` | 改 `saves_[3]`（slot/settings/meta）+ Saves() 默认返 slot 兼容 + Saves(ch) 重载 |

## 2. 设计决策（本批定案；D1–D3 违者走 ADR——已落 [ADR-012](../../ADR/ADR-012-Config-Table-Dual-Track.md)）

1. **A 线全按 ADR-012**：`.tab` JSON 资产唯一权威（全字符串格，64 列 × 1024 行 ×
   128 字符/格）；CSV 拖入一次性转换（不留 csv 源）；AssetBrowser 内嵌表格区查看 +
   单格微调写回（不加新面板）；World::Tables() + EnterPlay 快照 + vtable 尾加 3 项
   （tableRows/tableCols/tableCell）；波次留组件面（D2）；xpCurveK 提 World 级单参数
   默认值不变（D3）。
2. **B 线 AnimationEditor 最小版裁剪清单**：做 = clip 选择/新建、fps/loop、帧列表
   （sheet 槽 + cell 序号 + 缩略图预览 + 播放预览）、增删帧/排序、保存（原子写 +
   主动 Rescan）、"EnterPlay 生效"提示行。**裁** = per-frame 时长（schema 不动）、
   LoopMode 五模式（bool loop 保留）、帧事件打点、Play 中热改——登记 v1.1 候补，
   游戏侧卡点再启。面板集解冻 = AnimationEditor 一件（05 §3 修订注记：TilePalette
   仍归 M6c、ParticleEditor 后评估）。
3. **D 线分档口径**：三档三文件 `.lemon/saves/{slot_0,settings,meta}.sav`（格式复用
   零版本变）；C# `Save` 六方法加可选 `Chan` 参数（enum Slot/Settings/Meta，默认
   Slot——源码兼容，模板/用户项目零改即编译）；v1 固定 slot_0，**多档切换 API 裁剪**
   （游戏无多档刚需，登记 v1.1；06 §10 桌面路径 %USERPROFILE% 归 M7/M8）；
   EnterPlay 载全档、ExitPlay/Flush 落全档；坏档兜底每档独立（主→bak 逻辑参数化）。
4. **验收② 演示口径**（2026-09-28 二稿：演示落点 svr-test——资源现成、贴真实
   开发；模板生成器照常扩展作开箱示范）：交付物 = svr-test weapons.tab/
   upgrades.tab 新行 + 新 prefab（散射武器）+ 新敌人变体 prefab（fast-mob）+
   waves.tab 波次条目替换——运行时（Engine/ + Editor/ 非生成器部分）零 diff；
   模板生成器 vs_template 同步扩展属模板工程面（`--gen-vs-template` 重生成口径，
   不算"引擎改动"）；先例 = ADR-012 D2 用户项目例外延伸。

## 3. 验收判据（全部满足才勾销）

1. **表通道全链**：拖 CSV（含中文表头 + BOM）→ `.tab` 入库（GUID/.meta）→
   AssetBrowser 内嵌表格查看 → 双击改格写回 → EnterPlay 后 C# 读格一致
   （script-tests + smoke 断言）；
2. **AnimationEditor**：打开 yami `hero-walk.clip` → 改 fps/增删帧 → 保存 → 重进
   Play 帧率/帧数生效（smoke-anim 或回归新步断言 clip 文件 roundtrip）；
3. **验收② 演示**（M6a 出口判据②）：svr-test 新增"散射武器 + fast-mob 变体" =
   表行 + prefab + C#，运行时零改动（对照 git diff 验证口径见 §8）；
4. **存档三档**：EnterPlay/ExitPlay/Flush 三档独立落盘、坏档兜底按档隔离、旧
   game.sav 惰性迁移可读、模板 vs.best 归 meta（engine-tests + script-tests +
   smoke-template 断言集只增不减）——**✅ T5**（TestSaveChannelSplits +22 /
   TestSaveChannels +13 / smoke `saves(slot_0/meta/legacy/skipEmpty)` 四断言；
   键约定：settings 版本化 KV + meta 收集条目，见 06 §10 修订注）；
5. **零重录**：m5b2 三档金回放 mismatches=0（依据 = 先例二/三：World 级通道零哈希 +
   xpCurveK 默认值不变；基准场禁播新组件实例）——**✅ T5 后复证**（sim-st/
   sim-mt/script 现录现放 mismatches=0；09 §6.8 通道族扩员落账）；
6. **回归与性能**：editor-regression full 全绿（步数按实际登记 09：svr-test
   验证若入回归则增步、否则维持现 14 步——2026-09-28 随 T4 落点修订，原
   "增步后 ≥15 步"预估作废）+ bench-survivor ≥ 45fps（表查找 O(1) 哈希读，
   不预期变化，跑一遍记账 09 §6.10）；
7. **文档回写**：06（§2.2 表资产行 + §10 分档注记）/ 05（§3 解冻注记 + §7）/ 03
   （World::Tables 一句）/ 04（vtable 尾加 + Lemon.Table/Lemon.Save 扩参）/ 09
   （§6.8 先例、回归步数、§6.10 台账）/ 08（批②勾销）/ DevLog 新条目 / 本页勾销。

## 4. 确定性与回放影响

- **TableStore 不入 StateHash**（World 持有 + 非 ECS——先例二第五例：Clips/Saves/
  RtUi/Fx/TableStore 全家同款）；表值影响 gameplay 经组件字段写（入哈希）——
  改表内容改变模拟属预期（与作者改 Shooter.interval 同类），金档基准 = 模板表冻结。
- **xpCurveK World 级参数**：默认值 = 现硬编码值 → Systems.cpp:795 换读点后三金档
  哈希流逐位不变；红线 = 基准场（bench-sim/bench-script/m5b2 三金档）构造零改动、
  不得在基准场写曲线参数。
- **SaveChannel[3]**：World 成员数组化，通道语义零哈希面（先例二原例）；Ex vtable
  尾加 = 旧宿主判空降级（rtUiSetEx 同款）。
- **AnimationEditor 只写资产文件**（clip JSON），不动 ECS/哈希；写回 → Rescan →
  下次 EnterPlay 快照——Play 中打开面板只读（编辑禁用或编辑即弃，取实现简单者，
  面板提示行交代）。
- **模板 PlayerCombat 与 svr-test PlayerBehaviour 双面读表**：各自行为等价变换
  （池内容/效果数值与现硬编码一致），取卡轮换序（`_pickRotation`）与 StateBag
  恢复口径不动；smoke-template 升级链断言只增不减。

## 5. 任务分解（T0→T6 依序；D 线（T5）独立可提前/并行，A/C 线（T1→T2→T4）是主线）

### T0 ADR-012 配置表定形 —— ✅ 已落（2026-09-25，随本分解文件）

- [ADR-012-Config-Table-Dual-Track.md](../../ADR/ADR-012-Config-Table-Dual-Track.md)
  D1 双轨 / D2 波次留组件 / D3 xpCurveK World 级；后续任务违者回 ADR 修订。

### T1 编辑器：.table 资产类型 + CSV 导入 + AssetBrowser 内嵌表格区 —— ✅ 2026-09-26 完工（后缀定名 `.tab`，见 ADR-012 修订注记）

- 首条核对落账：`.meta`/manifest 的 type 均**字符串名**（`AssetTypeName`）→ 枚举
  插位自由，`Table` 插 Clip 后（`AssetDatabase.h:26` 一带）；
- `AssetType::Table` + TypeOf `.tab` + AssetTypeName `"table"`；
- 新 `Editor/Assets/Csv.h/.cpp`（lemon-editor-core，ImGui-free 可单测）：`ParseCsv`
  （引号/逗号/换行转义 + BOM 剥除 + GBK 拒入（UTF-8 校验）+ 空行跳过 + 参差行矩形化；
  上限 64 列 × 1024 行 × 128 **码点**/格超限拒入）+ `TableToJson`/`ParseTableJson`
  （roundtrip；JSON 侧裸数值/布尔格宽松归一为字符串——ADR 示例形态）+
  `NormalizeTable`（三口共用的校验核心）；
- `ImportFile` 分支：`.csv` 源 → 解析 → 同名 `.tab` 落盘入库（csv 不拷入；重拖
  同名 = 覆盖再导入，guid 走路径继承稳定）；`.tab` 手写直接导入照常；
- `AssetBrowserPanel`：单击选中（缩略图高亮）+ 选中 Table 条目时面板底部
  CollapsingHeader 表格区（BeginTable + ScrollY 行裁剪 + 列头钉住；双击单格
  InputText（IME 焦点抢占）→ NormalizeTable 校验 → 原子写 + 主动 Rescan；
  提示行交代 EnterPlay 快照语义）；
- engine-tests +35 checks（`TestCsvTable`×21 + `TestTableAssetImport`×14：引号/转义/BOM/
  CJK 码点上限/roundtrip/坏档拒入/导入生命周期含覆盖重导与跨会话 guid 稳定）。
- 验证记录（2026-09-26）：mac/mac-debug 双 preset ctest 3/3；engine-tests
  **33410 checks**（双档同数）；`editor-regression.sh full` **14/14 PASS**；金回放
  三档现录现放 **mismatches=0**（sim-st/sim-mt/script；T1 零引擎改动，bench 二进制
  不链 editor-core，回放不受影响属结构性结论，跑齐三档为纪律自证）；编辑器真进程
  冒烟（含 .tab 项目）`--smoke --frames 200` PASS errors=0，meta/manifest 落
  `type: "table"`。
- **T1 反馈批（2026-09-26 用户实测：内嵌区可操作面太小 + 无横向滚动）**：表格
  渲染重构为内嵌区/浮动窗共用（`RenderTableGrid`）——补 `ScrollX` +
  `SizingFixedFit`（列宽=内容宽，超窗横滚）+ 行号列 + `ScrollFreeze(1,1)` 双向
  钉住；内嵌预留按面板高比例化（≥12 行）；新增**浮动放大编辑器**（双击 .tab /
  「放大编辑」按钮打开；居中 1040×560 起步、可拖拽缩放、Esc 关窗、按需工具窗
  不进面板注册表 = 05 §3 冻结不破，`NoSavedSettings` 不落 imgui.ini）。
  验证：回归 14/14（首跑 2 FAIL 系后台回放负载抖动，复跑 ×3 全绿 + final 单跑
  PASS）；编辑器与浮动窗共用缓存/编辑态（单缓存按 guid+hash 键，双表并存交替
  重读，小文件可接受）。
- **T1 反馈批②（2026-09-26 用户五点：双击不顺畅/点别处应退出/行高/表头固定/
  点别处应保存）**：整格热区（`Selectable SpanAvailWidth`——此前热区=文字宽，
  宽列空白双击无响应，即"不顺畅"根因）；**Excel 语义**（Enter 或点别处失焦 =
  保存退出，Esc = 弃改，提交失败也退出编辑态防僵尸态）；行高 = CellPadding.y
  ×2.5；表头固定根因 = 表格外高下限可高过窗口 → 滚动升格窗口级、冻结失效
  ——改为外高恒占余量（-2px）+ 浮动窗 `NoScrollbar`，滚动恒归表格内部。

### T2 引擎+SDK：TableStore + vtable 尾加 + Lemon.Table —— ✅ 2026-09-26 完工

- 新 `Engine/ECS/TableStore.h/.cpp`（ClipTable 同款形态）：按 `uint32 guidLow`
  登记全字符串格网格；Add（id=0/空网格拒）/Find/Cell（越界 nullptr）/Clear/Count；
  上限归解析器（T1 ParseTableJson），引擎零 JSON 依赖（依赖向下不破）；
- `World.h`：`TableStore& Tables()`（成员 tables_，Clips 旁；非 ECS 通道零重录
  ——09 §6.8 先例二第五例：Clips/Saves/RtUi/Fx/TableStore 全家同款）；
- `EditorContext.cpp`：`BuildPlayTableCache()`（EnterPlay 内 BuildPlayClipCache
  后调用；坏表 warn + 跳过不炸 Play）；
- `ScriptHost.h/.cpp`：NativeApiVtable 尾加 3 项 `tableRows/tableCols/tableCell`
  （-1 无表/越界、-2 cap 不足、≥0 拷贝数；空宿主全 -1；桥内自持 mini hex→低 32
  解析）；
- SDK 新 `Table.cs`（Rows/Cols/Str/Has/Int/Float，Int/Float Invariant 容错 +
  warn 一次/格）+ `NativeApi.cs` 尾加 3 委托；配套（D2 用户项目例外）：
  WaveDirector/WaveDef 镜像加 `GetWave/SetWave/GetEntry/SetEntry` 拷贝语义
  读写口（布局冻结不动，纯加方法）；
- script-tests 新 `TestTableChannel`（+12 checks：容器单元 + 端到端全 API 面 +
  无表降级 + 越界 + 坏格容错 + 新 World 自清零；TableProbeBehaviour typeId 12
  表尾注册）；热重载名单断言 12→13；
- **使用范例落地（用户拍板，ADR-012 D2 用户项目例外）**：svr-test 波次表数据化
  ——`Assets/tables/waves.tab`（16 波全量迁移，prefab 列存完整 GUID）+
  `Game/WaveTableLoader.cs`（Awake 读表 → GetWave/SetWave 灌回组件 → 引擎
  WaveDirectorSystem 零改动）+ Main.scene 波次清零 + Director 挂 scripts[]；
- 验证记录（2026-09-26）：mac/mac-debug ctest 3/3 ×2；engine-tests 33410（不变）+
  script-tests **1566**（+12）；`editor-regression.sh full` **14/14**；金回放三档
  现录现放 mismatches=0（TableStore 不入 StateHash + 基准场零表脚本 = 零重录，
  先例二第五例落账）；真进程 `--bench-scene Main.scene --frames 900`：表快照
  17×19 → 载入 16 波 → alive 2→47（首波 t=5 刷怪 + 玩家击杀动态平衡），整链通。

### T3 编辑器：AnimationPanel 最小版 + 双击打开通道 —— ✅ 2026-09-26 完工

- 数据面先行：新 `Editor/Assets/ClipEdit.h/.cpp`（lemon-editor-core，ImGui-free 可
  单测——Csv.h 同款形态）：`ParseClipJson`（宽容度对齐运行时 BuildPlayClipCache：
  frames[]/fps 必需、loop 缺省 true、坏类型/非 hex sheet/负 cell 拒入不炸）+
  `ClipToJson`（**手写定版序列化**——字段序/缩进与 Samples/yami 既有 .clip 逐字符
  同型的多行格式；fps 整值整数/非整值一位小数）；
- `AnimationPanel`（BuiltInPanels.h 声明 + 新 `Editor/Panels/AnimationPanel.cpp`
  ~470 行）：目标选择（clip 下拉）/新建（模态：名字校验 → `Assets/<名>.clip` →
  Rescan 反查 guid）/name·fps（DragInt 1..60）·loop/帧列表（每行 = 帧缩略图（切片
  UV 偏移直染，悬空红框）+ sheet 下拉（只列已切片精灵——未切片 = SliceSpriteId 恒
  0 必悬空）+ cell DragInt（界内钳制）+ 上移/下移/删除；拖 Assets 精灵进缩略图 =
  换 sheet）/加帧（承接上行，空表起步取首个切片精灵）/播放预览（编辑器时钟推进
  ▶/⏸/步进/回起点——纯预览不进模拟）/保存（校验三关：可解析·非空帧·全帧可解析
  （判据同 BuildPlayClipCache）→ WriteFileAtomic → 主动 Rescan → 提示行交代
  EnterPlay 快照语义）；缓存键 guid+hash（切目标/保存回读/外部改动同路重读）；
  Play 中面板只读（BeginDisabled + 黄字横幅）；
- 双击通道：`AssetBrowserPanel DrawItem` clip 分支 → `EditorApp::OpenAnimationEditor
  (guid)`（FindEntry 置 open + SetTarget）——**首个"资产 → 专用编辑面板"通道**
  （05 §5 先例注记归 T6 文档回写）；tooltip 补"双击：动画编辑"；
- 注册四处：Panels.cpp CreateAllPanels 尾加 + CMakeLists 双处（core：ClipEdit /
  editor：AnimationPanel）+ DockBuilder 中央区预挂（OpenByDefault=false 按需窗口
  ——Unity 同款；停靠请求对未建窗口有效，首开即落 Scene|Game 标签页）；
- **顺手修既有回归（非 T3 引入，HEAD 复现 3/3）**：smoke-anim 的 play-roundtrip
  byte-exact 自批⓪起实际一直 FAIL——根因 = SeedSmokeScene 给 AnimHero 写切片表
  **本体号**，而批⓪ ResolveSpriteRefs 对切片表"一律按 cell 口径归一（本体引用
  降级 cell 0）"→ 快照 104/重建 105 失配；回归脚本 anim 步 grep 只看
  `smoke-anim: .* => OK` 汇总行，FAIL 行被 masked（14/14 因此"绿"着）。修法 =
  播种改 cell 0（Play 中 Animator 本就驱到 cell 区间，cell 0 才是真实用法）——
  批⓪降级语义不动（逐 cell guid 化仍归 v1.x 候补）；
- 验证记录（2026-09-26）：smoke-anim 扩 clip 编辑链断言（专档 anim-edit.clip 固定
  guid `5bd31a7c30000004`，幂等重写：落盘 → ParseClipJson → 改 fps 10→13 + 增帧 →
  ClipToJson → 原子写 → Rescan → 回读 roundtrip；EnterPlay 后 `Clips().Find` 快照
  13fps×2 帧）——裁决行增 `edit(rt=YES cache=YES)`，byte-exact=YES、editor-smoke
  PASS；engine-tests +15（TestClipEdit：规范解析/golden 定版格式/roundtrip/缺省/
  小数 fps/空帧表/七类坏档拒入/门卫空串）= **33425 checks**（双 preset 同数）；
  ctest 3/3 ×2；`editor-regression.sh full` **14/14**（首跑 12/14：smoke-drag/
  smoke-ui 负载抖动——单独复跑各 3/3 全绿 + 整套复跑 14/14，T1 同类先例）；
  hero-walk.clip 实文件验收（一次性程序链 ClipEdit 实测）：**不改动的往返逐字节
  相同**、改 fps+增帧后 diff 仅 fps 行 + 新增帧块（验收②"diff 仅预期字段"实证）；
  05 §3 解冻注记（AnimationEditor 一件）/§5 双击先例注记归 T6 文档回写清单。

#### T3c 动画集工作台 + 按名播放 —— ✅ 2026-09-26 完工（独立批文件）

用户 T3b 实测反馈"动画很孤立/参考 Godot"驱动的集化重构：`.ani` 集容器（Unity
.controller 壳——容器+段文件，段身份 = 文件 GUID 零迁移；"目录=角色"作用域方案
讨论中被否，作用域 = 集成员关系）+ ClipTable 集按名索引（World 持有零哈希 = 金
回放零重录）+ vtable `clipByName` + SDK 字符串重载按名化 + 工作台 v2（左列段清单/
连续建段/重命名/移除/删除）。**决策记录、U/G 对照与验证全录见
[T3c 批文件](./2026-09-26-b2-t3c-animset-workbench.md)**（回归 14/14、script-tests
1574、smoke-anim 增 `set(rt/cache)` 断言；真人验收余"真素材连续建五段 +
`Play(g, "attack1")`"）。

#### T3-UX 动画工作台 v3 交互重设计 —— ✅ 2026-09-27 完工（独立批文件）

用户 T3c 实测反馈"操作太不方便"驱动的交互重设计（Godot SpriteFrames 式主从
布局，参照截图 `docs/animation/`）：左列段清单工具条化（inline 新建/改名、搜索、
复制段——**建段只输入名字**，选帧回右区）+ 右区三层（工具条/大预览 fit≤512/
胶片带+属性行）+ 帧操作键盘化（←/→/Del/Ctrl+D/Space，`IEditorPanel::
CapturesGlobalKeys()` 键仲裁防双触发删实体）+ 拖 Assets 精灵三通道选图 + 从精灵
表对话框 v2（全选/点选序/缩放）+ 统一保存（段+集连存）。数据面零改动。**全录见
[T3-UX 批文件](./2026-09-27-b2-t3-ux-anim-workbench.md)**（回归 14/14、smoke-anim
errors=0；真人验收清单四条余）。

#### T3-UX2 动画工作台 v3.1 修正批 —— ✅ 2026-09-27 完工（独立批文件）

T3-UX 同日第二轮实测反馈十项：新建入口归 AssetBrowser **空白区右键**（面板删
目标行 = 纯编辑器）；加帧四通道（空帧/从精灵表**文件选择→选帧对话框两段式**/
多选图片/从 .clip 复制 + 拖 .clip 复制帧表）；从精灵表对话框重做（分割参数与
预览同屏实时网格、**添加时才写 .meta**、InvisibleButton 命中层修复"拖框选拖走
对话框"、"关闭"→取消）；布局修复"右侧空白"（预览改紧凑条、**帧网格主体化**）；
左列 IconKind 图标化（尾加 Add/Duplicate/Delete/Rename/Search 五枚）；命名统一
段→动画（Unity 术语面，schema 不动）；FilePicker 扩 OpenMulti 多选+扩展名过滤。
**全录见 [T3-UX2 批文件](./2026-09-27-b2-t3-ux2-anim-workbench-round2.md)**
（回归 14/14、smoke-anim errors=0；遗留 auto-slice 与多选拖批量加帧）。

### T4 模板 + svr-test 双面：数值表落地 + 验收② 演示（散射武器 + fast-mob）—— ✅ 2026-09-28 完工

> **落点修订（2026-09-28 用户拍板，二稿）**：先完善模板（生成器扩展 +
> PlayerCombat 读表化——开箱示范与 smoke-template 回归面），再同步到
> demo/svr-test 落地测试，**验收② 演示落 svr-test**（先例 = T2 波次表 ADR-012
> D2 用户项目例外延伸；svr-test 有现成玩家/怪物资源，演示更贴真实开发）。
> 首稿"模板零改动、全落 svr-test"作废；更早的"仅模板 + svr-test 注记自迁"
> 亦作废。

**模板面（vs_template 生成器扩展——模板工程面，`--gen-vs-template` 重生成）**：

- `EditorApp.cpp:282-330` 段增资产：`Assets/tables/weapons.tab`（列：id/label/
  prefabGuid/interval/speed/pierce/count——直射/穿透/环绕参数三行起步）+
  `Assets/tables/upgrades.tab`（列：id/label/kind/value——模板现六选项逐行对应，
  数值与现硬编码一致）；
- 模板 `PlayerCombat.cs`：`Start` 读两表（Table.Rows/Str/Int 载内存 List，缺表/
  坏行 warn + 空池保底——模板永不因表缺炸 Play）；`kOptions` 数组与 `ApplyOption`
  switch → kind 派发（kind: 0 移速/1 磁力/2 射速/3 换弹种/4 生命/5 环绕+1），
  `_pickRotation` 轮换序不动；环绕刃参数（半径/角速/上限）改表读；
- 模板 `GameMain.Start` 写 xpCurveK（从表读值，缺省不写 = 引擎默认）；
- smoke-template 断言增：表载入行数（现有步内扩展，不加步）。

**svr-test 面（同款路径同步 + 演示落点）**：

- 同步：`Assets/tables/` 增两表（guid `7e57…` 段顺延，按 README 口径登记进
  GameMain；upgrades 七行——池多"飞剑 +1"一项）+ `Game/PlayerBehaviour.cs`
  读表化（同款容错：缺表/坏行 warn + 空池保底；kind 0–6 含 6 飞剑+1；
  `_pickRotation` 与 StateBag 恢复口径不动；环绕刃参数改表读）；
- **验收② 演示**：weapons.tab 加"散射"行（prefab = 新 ScatterBullet.prefab，
  `7e57…` 段顺延）+ upgrades.tab 加"散射弹"行（kind 3 换弹种变体）；
  fast-mob = Mob.prefab 拷贝改 speed/hp + **waves.tab** 条目 prefab 列替换两波
  （svr-test 波次已表化——T2；非 Main.scene 组件面）；
  验证 = `--bench-scene` 跑 svr-test Main.scene（T2 同款口径）+ 断言三件：
  表快照行数、散射弹发射证据（弹体计数/teamAlive 口径实现时定）、fast-mob
  出场；入回归自动化与否实现时按脚本结构取简，若入则步数登记 09；
- svr-test `README.md` 玩法锚点补"武器/升级池表数据化"段（T2 波次表锚点同款
  先例：编辑路径二选一 + 列契约指向脚本头注释 + 新表 guid 登记口径）。

**引擎侧（双面共用，ADR-012 D3）**：

- xpCurveK：`World` 加 `xpCurveK_`（默认 = Systems.cpp 现值）+ getter/setter；
  `Systems.cpp:795` 换读点；vtable 尾加 2 项（getXpCurveK/setXpCurveK，TimeScale
  同款 clamp 无需）；模板与 svr-test 两侧 Start 均从表写值（缺省不写 = 引擎默认）；
- 演示内容零运行时 diff：散射武器/fast-mob 的全部改动落 demo/svr-test（表行 +
  prefab + C# + waves.tab 条目），Engine/ + Editor/ 不涉；模板生成器扩展属模板
  工程面（不算"引擎改动"），xpCurveK 参数化属引擎本体（ADR-012 D3），均不属
  演示内容。

- **完工记录（2026-09-28）**：
  - 引擎侧：`World::XpCurveK/SetXpCurveK`（默认 1.25 = 原 StatSystem 硬编码，
    不 clamp——消费侧 `max(1,ceil)` 兜底）；`Systems.h:204` 成员删除、
    `Systems.cpp` 换读 `world.XpCurveK()`；vtable 尾加 `getXpCurveK/setXpCurveK`
    （空宿主读 1.25 写丢弃）；SDK 尾加 2 委托 + 新 `Lemon.SDK/Balance.cs`
    （`Balance.XpCurveK`）。
  - 模板面：生成器 `WriteTableAssets`（三表落 `Assets/tables/`，独立
    `7e572000…` guid 段）+ PlayerCombat 内嵌串读表化（表载缓存/kind 派发/
    环绕三参表读/balance 写值；`%6` → `%_upgrades.Count`，空池不弹不冻结）+
    README 数值表锚点；`--gen-vs-template` 重生成入库。weapons 列定
    id/label/prefabGuid/interval/speed/pierce/count/radius/angle（**弹体参数归
    prefab、发射参数归表**——pierce/angle 列供散射行，基础行留空）。
  - svr-test 面：三表（guid `7e57100000100002/3/4` 段顺延）+
    PlayerBehaviour 同款读表化（kind 0–6 含飞剑）；**散射 = 表 scatter 行 +
    ScatterBullet.prefab（Bullet 拷贝洋红染色 dmg5/speed300——同图染色免新图
    免 spriteId 漂移）+ `Subscribe(GameEvent.Spawn)` 补发**（主弹事件方向
    ±angle/2 均布 count-1 枚，C# Instantiate 无 Spawn 事件 = 无递归；补发计数
    落存档键 `svr.scatter` 供无 GUI 断言）；fast-mob = FastMob.prefab（Mob
    拷贝天蓝染色 speed150/hp10）+ waves.tab 第 3/7 波 e0 槽替换；README 锚点
    （表契约 + 演示内容 + 新表 guid 段口径）。
  - 实现注记：空串格免 warn（`F()` 先 `Table.Str` 判空再 Float——宽表留空是
    常态，不触 Table.Float 的坏值 warn-once）；`Convert`/`Console` 需
    `using System`（PlayerCombat 内嵌串补）。
  - 验证记录：mac preset ctest 3/3；engine-tests **33519 checks**、
    script-tests **1666 checks** 全绿；`editor-regression.sh full` **14/14
    PASS**（smoke-template 裁决行增 `tables=YES`——三表快照 4/7/2 行断言）；
    金回放三档现录现放 **mismatches=0**（sim-st/sim-mt/script——xpCurveK
    默认值不变的结构性自证）；bench-survivor **fps=70** ≥45 PASS（fx 饱和
    口径）；svr-test `--bench-scene`：四表快照（weapons 5×9/upgrades 9×4/
    waves 17×19/balance 2×3）+ 16 波载入 + fps=447 + 零 warn；fast-mob 长跑
    验证（临时 balance xpCurveK=99999 免首升冻结 → 8400 帧跑过 wave3 t=110s，
    零 prefab 解析 warn + alive 69 正常增长，跑后还原）。
  - **真人验收余项**：散射端到端（升级选"散射弹"卡 → 主弹+2 补发扇形 +
    `svr.scatter` 计数）与 fast-mob 手感/辨识（天蓝快怪）——无输入跑法升级卡
    冻结挡波次，机器断言不可达（批口径"实现时取简"落此）；验收② git diff
    核对已过：Engine/+Editor/ diff 仅 xpCurveK 本体 + vs_template 生成器段
    （273 行），演示内容全落 demo/svr-test（gitignore 区）。

### T5 引擎+SDK：存档分档 slot_0/settings/meta —— ✅ 2026-09-28 完工

- `World.h/.cpp`：`saves_` 单成员 → `SaveChannel saves_[3]`（Q 项）；`Saves()`
  默认返 [Slot]（全调用点零改兼容）+ `Saves(uint8 ch)` 重载；枚举常量
  `kSaveSlot=0/kSaveSettings=1/kSaveMeta=2` 落 SaveChannel.h（含键约定头注）；
- `EditorContext.h/.cpp`：`SaveFilePath(ch)` → slot_0.sav/settings.sav/meta.sav；
  `WriteSaveFile(ch, chn)` / `LoadSaveFile(ch, dst)` 参数化（.bak 兜底逻辑随档
  走）；**旧档惰性迁移**：slot_0 载入链 = 新档 → 新档.bak → game.sav →
  game.sav.bak（新档不存在才读旧名，写恒写新名，旧文件保留）；EnterPlay 三档
  循环载入、ExitPlay 三档循环落盘（空档跳过 + 逐档独立不因一档失败断链）、
  `HookSaveFlush` flush 三档（空档跳过语义保留）；
- `ScriptHost.h/.cpp`：vtable 尾加 3 项 `saveSetEx/saveGetLenEx/saveGetEx`
  （尾参 `uint8 ch`，越界 ch 红 warn + 落 slot）；`saveFlush` 复用全档语义；
- SDK `Save.cs`：`Save.Chan` 枚举 + 五方法可选参数 `Chan chan = Chan.Slot`
  （Ex 路由；旧宿主无 Ex 表项 = chan 忽略走唯一档）——源码兼容，模板零改；
- **档内键约定（2026-09-28 用户口径，防后续里程碑撞僵 schema）**：settings =
  版本化 KV（首键 `version` = 键集结构版本，设置项键自由增长——M6b 音量/
  手柄键位/画质档位直接加键，不定死字段）；meta = 收集条目
  `col.<条目id>.state` / `col.<条目id>.count`（条目 id → 状态/计数映射）+
  全局统计平键。落 SaveChannel.h / Save.cs 头注 + 06 §10 修订注；
- 模板 `PlayerCombat.cs`：vs.best 读写改 `Chan.Meta`（Start 读 + 死亡新纪录
  写 + Flush 全档）；`--gen-vs-template` 重生成入库；
- 测试：engine-tests `TestSaveChannelSplits` +22（三档路径/独立落盘/空通道
  跳过/回读独立/惰性迁移全链（写恒写新名 + 旧文件保留）/坏档兜底按档隔离
  ×3 形态/16 MiB 上限按档 + 迁移接力）；script-tests `TestSaveChannels` +13
  （SaveChanProbeBehaviour typeId 16 表尾：三档写入含约定键 + 越界回落 +
  九项隔离回读 + C++ 原生通道对拍 + RtUi 双证；热重载名单 16→17）；
  smoke-template 断言升级：种子双载体（meta.sav = Chan.Meta 载入回显 +
  game.sav = 旧名迁移链）→ 裁决行 `saves(slot_0/meta/legacy/skipEmpty)`
  四断言（迁移内容解码回读 + 空档 settings 不落文件）。
- 验证记录（2026-09-28）：mac/mac-debug 双 preset ctest 3/3；engine-tests
  **33541 checks**（双档同数）、script-tests **1679 checks**（双档同数）；
  `editor-regression.sh full` **14/14 首跑全绿**；smoke-template
  `saves(slot_0=YES meta=YES legacy=YES skipEmpty=YES) => OK`；金回放三档
  现录现放 **mismatches=0**（sim-st/sim-mt/script——通道族零哈希面的结构性
  自证）。用户项目零强制：svr-test key 前缀在 slot 档照旧工作（vs.best 自迁
  Chan.Meta 属可选，DevLog 注记）。

### T6 全量验证 + 文档回写 + 勾销 —— ✅ 2026-09-28 完工

- 全量：mac + mac-debug 双 preset ctest 3/3；engine-tests/script-tests 计数
  入账（33541/1679，双档同数）；`editor-regression.sh full` **14/14**（首跑
  全绿，无复跑）；m5b2 三档金回放 mismatches=0；bench-survivor **fps=82**
  PASS（playerHp=275793 逐位一致第六次，09 §6.10 台账行）；
- 文档回写（§3 判据 7 清单全落）：06 §10 分档修订注（含键约定）/ 03 落地注
  （Tables + saves_[3] 通道族）/ 04 vtable 批② 全量流水（T2 表 3 项 + T3c
  clipByName + T3d animParamSlot + A 档 tween 4 项补记 + T4 xpCurveK + T5 Ex
  3 项；SDK 新面 Lemon.Table/Balance/Anim 按名/Save 扩参）/ 05（§2 面板集
  解冻注记 + §5 资产→专用面板双击先例 + §7 AnimationEditor 落地口径）/
  09（§6.8 先例二通道族扩员 + §6.10 台账行 + 回归 14 步订正 + smoke-template
  断言注记）/ 08（M6a 批② 勾销）/ DevLog
  [2026-09-28-m6a-b2-t5-save-channels.md](../../DevLog/2026-09-28-m6a-b2-t5-save-channels.md)
  / M6a.md 批次表勾销 / 本页 Status: done。

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| `.meta` type 若是数值序，枚举中插 Table 错位旧 meta | T1 首条核对（盘点 A）；数值序则尾加保序，TypeOf 显式匹配 `.table` 在 Generic 兜底前 |
| CSV 中文/Excel 兼容（BOM/GBK） | 解析器剥 UTF-8 BOM；GBK 不支持（v1 中文单语 + 明示"Excel 导出选 UTF-8 CSV"，坏编码拒入红字） |
| 表微调写回与 FileWatcher/Rescan 竞态（双扫描/抖动） | 写回走 WriteFileAtomic + 同帧主动 Rescan；FileWatcher 防抖已修（F-15 DebounceGate），重复脏合并为一次 |
| AnimationEditor 写 clip 破坏 yami 既有资产 | 保存前留 `.bak`？**不留**——资产在 git（模板/样例入库），编辑器不做版本系统；yami 样例区只读红线不涉（Samples 可改，THIRD_PARTY 只约束许可登记） |
| ApplyOption kind 派发 vs 原 switch 行为漂移 | 数值等价变换 + smoke-template 升级链断言只增不减（模板面）+ bench-scene 手测升级链（svr-test 面）；取卡轮换序/池容量不动 |
| 升级池空表/坏行炸游戏 Play | PlayerBehaviour 载表全容错：缺表 = 空池 + warn（三选一不弹 = 与"无升级"语义一致）；坏行跳过 |
| 三档落盘部分失败（盘满） | 每档独立 try/原子写失败红字；ExitPlay 兜底逐档独立，不因一档失败断链 |
| 表查找进 Play 热路径开销 | TableStore 一次性快照（EnterPlay 建，Play 中只读哈希查）——零逐帧解析；bench 记账验证 |

## 7. 与并行游戏线的接口

- T4 双面（2026-09-28 用户拍板二稿）：模板先完善表化示范（生成器 + PlayerCombat
  读表化），svr-test 同步落地 + 承验收② 演示；后续用户加第 8 项升级/新武器 =
  表行 + prefab；伙伴（AllyBehaviour 常量组）表化不在 T4 必做面（表通道已通，
  游戏侧卡点再启）；引擎侧不接受"帮加个武器"类 C++ 改动请求（验收② 口径）。
- 存档语义变化（vs.best → meta）对用户项目零强制（key 前缀在 slot 档照旧工作）；
  自迁建议随 DevLog 注记。

## 8. 验证命令（批②完工口径；2026-09-28 实测数字）

```bash
cmake --preset mac && cmake --build --preset mac
ctest --test-dir build/mac --output-on-failure      # 3/3（mac-debug 同）
./build/mac/tests/lemon-tests                       # 33541 checks（双 preset 同数）
./build/mac/tests/script/lemon-script-tests         # 1679 checks（Ex/Table/Chan 断言）
./tools/editor-regression.sh full                   # 14/14（首跑全绿）
./build/mac/Editor/lemon-editor --bench-survivor    # fps=82 PASS（09 §6.10 台账）
# 金回放：sim-st/sim-mt/script 三档现录现放 mismatches=0（T5 后复证）
git -C Lemon diff --stat Engine/ Editor/            # 验收② 演示核对：运行时 diff 仅
                                                     # 表通道/AnimationEditor/存档分档/xpCurveK
                                                     # 本体 + vs_template 生成器段（模板工程面），
                                                     # 演示内容零运行时 diff（全落 demo/svr-test）
```
