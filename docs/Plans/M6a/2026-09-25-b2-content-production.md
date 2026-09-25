# M6a 批②：内容生产 —— 配置表资产 + AnimationEditor 最小版 + 技能数据化 + 存档分档

Status: active（2026-09-25 开工分解；T0 ADR-012 已落）

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
| E | 升级池 = 硬编码数组 + switch；环绕刃 = 纯 C# 轨道 + prefab 常量；穿透 = 切 projectileId | `PlayerCombat.cs:20-22, 94-112, 136-172` | C 线模板侧落点：池/武器参数进表，ApplyOption → kind 派发 |
| F | 用户游戏已是"复制 behaviour + 手填常量"作者路径（≈19 常量 + 复制池 + 第二份常量表） | `svr-test/PlayerBehaviour.cs:17-41`、`AllyBehaviour.cs:20-46, 63-140` | ADR-012 需求证据；svr-test 不做代码侵入，交付通道 + 模板示范 + README 注记由用户自迁 |
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

1. **A 线全按 ADR-012**：`.table` JSON 资产唯一权威（全字符串格，64 列 × 1024 行 ×
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
4. **验收② 演示口径**：交付物 = weapons.table/upgrades.table 新行 + 新 prefab
   （散射武器）+ 新敌人变体 prefab（fast-mob）+ Main.scene 波次条目替换——运行时
   （Engine/ + Editor/ 非生成器部分）零 diff；模板生成器 vs_template 同步扩展属
   模板工程面（`--gen-vs-template` 重生成口径，不算"引擎改动"）。

## 3. 验收判据（全部满足才勾销）

1. **表通道全链**：拖 CSV（含中文表头 + BOM）→ `.table` 入库（GUID/.meta）→
   AssetBrowser 内嵌表格查看 → 双击改格写回 → EnterPlay 后 C# 读格一致
   （script-tests + smoke 断言）；
2. **AnimationEditor**：打开 yami `hero-walk.clip` → 改 fps/增删帧 → 保存 → 重进
   Play 帧率/帧数生效（smoke-anim 或回归新步断言 clip 文件 roundtrip）；
3. **验收② 演示**（M6a 出口判据②）：模板新增"散射武器 + fast-mob 变体" = 表行 +
   prefab + C#，运行时零改动（对照 git diff 验证口径见 §8）；
4. **存档三档**：EnterPlay/ExitPlay/Flush 三档独立落盘、坏档兜底按档隔离、旧
   game.sav 惰性迁移可读、模板 vs.best 归 meta（engine-tests + script-tests +
   smoke-template 断言集只增不减）；
5. **零重录**：m5b2 三档金回放 mismatches=0（依据 = 先例二/三：World 级通道零哈希 +
   xpCurveK 默认值不变；基准场禁播新组件实例）；
6. **回归与性能**：editor-regression full 全绿（增步后 ≥15 步口径登记 09）+
   bench-survivor ≥ 45fps（表查找 O(1) 哈希读，不预期变化，跑一遍记账 09 §6.10）；
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
- **模板 PlayerCombat 读表**：行为等价变换（池内容/效果数值与现硬编码一致），
  smoke-template 升级链断言不回归；取卡轮换序（_pickRotation）不动。

## 5. 任务分解（T0→T6 依序；D 线（T5）独立可提前/并行，A/C 线（T1→T2→T4）是主线）

### T0 ADR-012 配置表定形 —— ✅ 已落（2026-09-25，随本分解文件）

- [ADR-012-Config-Table-Dual-Track.md](../../ADR/ADR-012-Config-Table-Dual-Track.md)
  D1 双轨 / D2 波次留组件 / D3 xpCurveK World 级；后续任务违者回 ADR 修订。

### T1 编辑器：.table 资产类型 + CSV 导入 + AssetBrowser 内嵌表格区 —— 约 1 天

- `AssetDatabase.h/.cpp`：**首条核对 `.meta` type 序列化形态**（字符串名 → Table
  插 Clip 后自由位；数值序 → 尾加 Generic 后，TypeOf 的 `.table` 匹配前插即可）；
  AssetType 加 `Table` + TypeOf 加 `.table` + AssetTypeName 加行；
- 新 `Editor/Assets/Csv.h/.cpp`：mini CSV 解析（引号/逗号/换行/UTF-8 BOM 剥除；
  上限 64 列 × 1024 行 × 128 字符超限红字拒入）+ `TableToJon`（行网格 → .table
  JSON 文本，schemaVersion/name/rows，第 0 行 = 列头）；
- `AssetDatabase.cpp ImportFile`（:582-602）分支：`.csv` 源 → 解析 → 同名 `.table`
  落盘入库 + meta 生成，**csv 不拷入**；`.table` 直接导入照常（手写 JSON 流）；
- `AssetBrowserPanel.cpp`：选中 `.table` 时 OnGui 底部 CollapsingHeader 表格区
  （ImGui BeginTable，先例 InspectorPanel.cpp:851-877）：列头行 + 数据行只读渲染 +
  双击单格 InputText 编辑 → 写回 `.table`（WriteFileAtomic + 主动 RescanAssets）；
  行数超 32 行虚拟化裁剪（ImGui 表格裁剪器或分页，取简）；
- engine-tests：CSV 解析单测（引号/转义/BOM/超限拒绝）+ .table JSON roundtrip。

### T2 引擎+SDK：TableStore + vtable 尾加 + Lemon.Table —— 约 1 天

- 新 `Engine/ECS/TableStore.h/.cpp`：`TableGrid{vector<vector<string>> rows}` 按
  `uint32 guidLow` 登记（ClipTable 同款形态）；Add/Clear/Find/RowsOf；构造上限
  防呆（解析期拒绝超限表，warn-once）；
- `World.h/.cpp`：`TableStore& Tables()`（成员 tables_，:131 Clips 旁；头注释
  "不入 StateHash，零重录——09 §6.8 先例二"）；
- `EditorContext.cpp`：`BuildPlayTableCache()`（EnterPlay 内 BuildPlayClipCache
  :782 旁调用；AssetType::Table → nlohmann 解析 → guidLow 入 store；坏表 warn +
  跳过不炸 Play）；
- `ScriptHost.h/.cpp`：NativeApiVtable 尾加 3 项——`tableRows(guidHex)`（-1 无表）、
  `tableCols(guidHex)`、`tableCell(guidHex,row,col,char* out,uint cap)`（-1 越界 /
  -2 cap 不足 / ≥0 返回拷贝数）；实现读 `g_world->Tables()`，空 store 全 -1 降级；
- SDK `Table.cs`：`static int Rows(string guidHex)` / `Cols` /
  `string? Str(guid,row,col)` / `int Int(...)` / `float Float(...)`（Parse 容错：
  空串/格式错 → 0 + Console.Warn 一次）/ `bool Has(guid)`；
- script-tests：注册表 → Rows/Cols/Str/Int/Float 往返、越界格、无表降级、
  cap 不足二段调用。

### T3 编辑器：AnimationPanel 最小版 + 双击打开通道 —— 约 1.25 天

- `Editor/Panels/BuiltInPanels.h`：`AnimationPanel` 声明（成员：targetGuid_、
  编辑态 fps/loop/frames 副本、dirty 标记、`SetTarget(guid)`）；
- 新 `Editor/Panels/AnimationPanel.cpp`（~300 行，ConsolePanel 骨架起手）：
  - 目标选择：AssetType::Clip 下拉（AssetDatabase 遍历）+ "从 AssetBrowser 双击
    进入"（见下）；
  - 编辑面：name/fps（DragInt 1..60）/loop（Checkbox）；帧列表（每行 = sheet guid
    槽（缩略图 + hex 文本，DrawSpriteSlot 式样）+ cell DragInt + 删行；底部
    加帧（默认承接上行 sheet）+ 上移/下移）；
  - 预览：当前帧 thumbnail 直染（AssetGpu().Thumbnail(sheet guid) + cell 偏移
    UV——与切片号换算同 BuildPlayClipCache :547-560 的算法提公用或复刻）+
    播放预览（▶ = 编辑器渲染帧推进帧步进，非确定无妨——纯预览）；
  - 保存：组 clip JSON（schema 不动）→ WriteFileAtomic → 主动 RescanAssets →
    提示行"已保存，Enter Play 后生效"（快照语义显式化）；Play 中面板只读；
- `AssetBrowserPanel.cpp DrawItem`（:308-317）双击分支：clip → `EditorApp::
  OpenAnimationEditor(guid)`（实现：FindEntry(AnimationPanel) open=true +
  SetTarget）——**首个"资产→专用编辑面板"通道**，05 §5 注记先例；
- `Panels.cpp CreateAllPanels`（:133-143）push_back + `Editor/CMakeLists.txt` 源
  文件 + `EditorApp.cpp` DockBuilder（:1173-1189）追加中央区默认位；
- 验证：yami hero-walk.clip 打开→改 fps→保存→文件 diff 仅预期字段；回归新步
  （smoke-anim 扩 clip roundtrip 断言或独立 smoke-animedit——实现时按回归脚本
  结构取简，步数变化登记 09）。

### T4 模板：数值表落地 + 验收② 演示（散射武器 + fast-mob）—— 约 1 天

- 模板生成器 `vs_template`（`EditorApp.cpp:282-330` 段）增资产：`Assets/tables/
  weapons.table`（列：id/label/prefabGuid/interval/speed/pierce/count——直射/穿透/
  环绕参数三行起步）+ `Assets/tables/upgrades.table`（列：id/label/kind/value——
  现六选项逐行对应，数值与现硬编码一致）；`--gen-vs-template` 重生成；
- `PlayerCombat.cs`：`Start` 读两表（Table.Rows/Str/Int 载内存 List，缺表/坏行
  warn + 空池保底——模板永不因表缺炸 Play）；`kOptions` 数组与 `ApplyOption`
  switch → kind 派发（kind: 0 移速/1 磁力/2 射速/3 换弹种/4 生命/5 环绕+1），
  `_pickRotation` 轮换序不动；环绕刃参数（半径/角速/上限）改表读；
- xpCurveK：`World` 加 `xpCurveK_`（默认 = Systems.cpp 现值）+ getter/setter；
  `Systems.cpp:795` 换读点；vtable 尾加 2 项（getXpCurveK/setXpCurveK，TimeScale
  同款 clamp 无需）；模板 GameMain.Start 从 weapons/balance 列写值（缺省不写 =
  引擎默认）；
- **验收② 演示**：weapons.table 加"散射"行（prefab = 新 ScatterBullet.prefab，
  生成器固定 guid 段顺延）+ upgrades.table 加"散射弹"行（kind 3 换弹种变体）；
  fast-mob = Mob.prefab 拷贝改 speed/hp + Main.scene 波次条目 e2 槽替换两波；
  smoke-template 断言增：表载入行数、散射弹发射证据（teamAlive/弹体计数口径
  实现时定）、fast-mob 出场；
- `svr-test` 不侵入：引擎批 README/DevLog 注记"用户项目按模板升级路径自迁
  （升级池/武器参数表化）"，迁移动作归用户。

### T5 引擎+SDK：存档分档 slot_0/settings/meta —— 约 0.75 天（独立，可提前）

- `World.h/.cpp`：`saves_` 单成员 → `SaveChannel saves_[3]`（Q 项）；`Saves()`
  默认返 [Slot]（全调用点零改兼容）+ `Saves(uint8 ch)` 重载；枚举常量
  `kSaveSlot=0/kSaveSettings=1/kSaveMeta=2`；
- `EditorContext.h/.cpp`：`SaveFilePath()` → `SaveFilePath(uint8 ch)`（slot_0.sav/
  settings.sav/meta.sav）；`WriteSaveFile(ch, channel)` / `LoadSaveFile(ch, channel)`
  参数化（.bak 兜底逻辑随档走）；**旧档惰性迁移**：slot_0 载入时若新档不存在且
  game.sav 存在 → 读旧路径（写恒写新名）；EnterPlay（:784）三档循环载入、
  ExitPlay（:812）三档循环落盘、`HookSaveFlush`（`EditorApp.cpp:87-89`）flush
  三档（空档跳过落盘语义保留）；
- `ScriptHost.h/.cpp`：vtable 尾加 3 项 `saveSetEx/saveGetLenEx/saveGetEx`（尾参
  `uint8 ch`，越界 ch 红 warn + 落 slot）；`saveFlush` 复用全档语义；
- SDK `Save.cs`：六方法加可选参数 `Save.Chan chan = Save.Chan.Slot`（enum
  Slot/Settings/Meta；Ex 路由）——源码兼容，模板零改；
- 模板 `PlayerCombat.cs`：vs.best 读写改 `Chan.Meta`（:77, :184-185 两点）；
- 测试：engine-tests（三档编解码独立 + 旧档惰性迁移 + 每档坏档兜底隔离 + 16MiB
  上限按档）；script-tests（Ex 三通道往返 + 越界 ch 降级）；smoke-template 增
  meta.sav 落盘断言（现断言 game.sav 存在 → 改 slot_0.sav + meta.sav 双断言）。

### T6 全量验证 + 文档回写 + 勾销 —— 约 0.5 天

- 全量：mac + mac-debug 双 preset ctest 3/3；engine-tests/script-tests 新增计数
  入账；`editor-regression.sh full` 全绿（新步登记）；m5b2 三档金回放
  mismatches=0；bench-survivor 跑一遍 ≥45fps 记账 09 §6.10；
- 文档回写（§3 判据 7 清单）+ DevLog 新条目（`2026-09-XX-m6a-b2-content-production.md`）
  + M6a.md 批次表勾销 + 本页 Status: done。

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| `.meta` type 若是数值序，枚举中插 Table 错位旧 meta | T1 首条核对（盘点 A）；数值序则尾加保序，TypeOf 显式匹配 `.table` 在 Generic 兜底前 |
| CSV 中文/Excel 兼容（BOM/GBK） | 解析器剥 UTF-8 BOM；GBK 不支持（v1 中文单语 + 明示"Excel 导出选 UTF-8 CSV"，坏编码拒入红字） |
| 表微调写回与 FileWatcher/Rescan 竞态（双扫描/抖动） | 写回走 WriteFileAtomic + 同帧主动 Rescan；FileWatcher 防抖已修（F-15 DebounceGate），重复脏合并为一次 |
| AnimationEditor 写 clip 破坏 yami 既有资产 | 保存前留 `.bak`？**不留**——资产在 git（模板/样例入库），编辑器不做版本系统；yami 样例区只读红线不涉（Samples 可改，THIRD_PARTY 只约束许可登记） |
| ApplyOption kind 派发 vs 原 switch 行为漂移 | 数值等价变换 + smoke-template 升级链断言只增不减；取卡轮换序/池容量不动 |
| 升级池空表/坏行炸模板 Play | PlayerCombat 载表全容错：缺表 = 空池 + warn（三选一不弹 = 与"无升级"语义一致）；坏行跳过 |
| 三档落盘部分失败（盘满） | 每档独立 try/原子写失败红字；ExitPlay 兜底逐档独立，不因一档失败断链 |
| 表查找进 Play 热路径开销 | TableStore 一次性快照（EnterPlay 建，Play 中只读哈希查）——零逐帧解析；bench 记账验证 |

## 7. 与并行游戏线的接口

- svr-test 卡点回灌预期：升级池/武器参数/伙伴（AllyBehaviour 常量组）表化后，
  用户加第 8 项升级/第 3 伙伴 = 表行 + prefab；引擎侧不再接受"帮加个武器"类
  C++ 改动请求（验收② 口径）。
- 存档语义变化（vs.best → meta）对用户项目零强制（key 前缀在 slot 档照旧工作）；
  自迁建议随 DevLog 注记。

## 8. 验证命令（批②完工口径）

```bash
cmake --preset mac && cmake --build --preset mac
ctest --test-dir build/mac --preset mac          # 3/3
./build/mac/tests/lemon-engine-tests             # 新增计数入账
./build/mac/tests/lemon-script-tests             # Ex/Table 断言
./Tools/regression/editor-regression.sh full     # ≥15 步全绿（新步登记 09）
./build/mac/editor/lemon-editor --bench-survivor # ≥45fps 记账
# 金回放：m5b2 三档 mismatches=0（回归脚本内含/单独跑，口径同批①）
git -C Lemon diff --stat Engine/ Editor/         # 验收② 演示核对：运行时 diff 仅
                                                 # 表通道/AnimationEditor/存档分档本体，
                                                 # 演示内容零运行时 diff
```
