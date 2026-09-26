# ADR-012：数值配置表——外置导入 + Inspector 微调双轨（.table 资产为唯一权威）

- 日期：2026-09-25
- 状态：已采纳（M6a 批② T0 定形；08 §2 M6a 批② "开工时 ADR 定形" 条款闭合）
- **修订 2026-09-26**（用户定名）：表资产后缀 `.table` → **`.tab`**（实施 T1 时定）。
  本文及各文档历史行中 `.table` 字样按此映射读取；schema 与其余决策不变。
- 影响：`06-Asset-Pipeline-Out-of-Box.md`（§2.2 表资产行落地、§10 分档另议）、`05-Editor.md`（§3 面板集注记、§7）、`08-Development-Roadmap.md` M6a 批②、`Editor/Assets/AssetDatabase.*`、`Engine/ECS/`（新 TableStore）、`Engine/Scripting/`（vtable 尾加 + `Lemon.Table`）

## 背景

08 §2 M6a 批②登记①：数值配置外置——"波次/技能/掉落走 CSV/JSON 配置表资产（Excel/Numbers
编辑 → 导入，Inspector 表格转查看/微调双轨），替代'在 Inspector 里逐格填 16 波'的
作者路径；与'波次表编辑器'合并评估（表格式编辑器 vs 外置表导入 vs 双轨，开工时
ADR 定形）"。

需求侧活证据（demo/svr-test，2026-09-25 摸底）：用户游戏 `PlayerBehaviour.cs:17-36`
≈19 个手填调参常量、`:38-41` 复制模板升级池再手加第 7 项、`AllyBehaviour.cs:20-46`
第二份同款手填表（飞剑参数）——"每加一种武器/伙伴 = 复制一份 behaviour 手填一组
常量"已是现实作者路径。模板侧同构：升级池硬编码字符串数组
（`PlayerCombat.cs:20-22`）+ 应用逻辑 switch（`:136-172`）。

现状（2026-09-25 逐行核对）：**数据资产通道不存在**——AssetType 仅
Sprite/Prefab/Script/Clip/Generic（`AssetDatabase.h:26`），全库零 `.asset/.table` 文件；
C# 无文件 IO，唯一数据入口 = SaveChannel（KV 字节档）。06 §2.2 规划的 `curve/data`
json 数据资产行未实现。

## 决策

### D1：双轨——`.table` JSON 资产为唯一权威，CSV 一次性导入，Inspector（AssetBrowser 内嵌表格区）查看 + 单格微调

- **`.table` 资产**：JSON 文本文件（nlohmann 解析，零新依赖），走既有资产纪律——
  GUID + `.meta` sidecar + `WriteFileAtomic` 原子写 + FileWatcher/Rescan 感知。schema：

  ```json
  { "schemaVersion": 1, "name": "weapons",
    "rows": [ ["id","label","interval","speed"],      // 第 0 行 = 列头
              ["shoot","直射",0.12,320] ] }
  ```

  **全字符串格**（行×列 string 网格）：引擎侧零类型系统，数值解释归 C#
  （`Lemon.Table.Int/Float` 容错包装）。防呆上限：64 列 × 1024 行 × 单元格 128 字符。
- **CSV 导入**：拖 `.csv` 入 AssetBrowser → 解析（自写 mini 解析器，带引号/逗号/
  换行转义 + **剥 UTF-8 BOM**——Excel 中文表头刚需）→ 生成同名 `.table` 入库，
  **csv 源文件不保留**（避免双源漂移；批量再编辑 = Excel 改完重拖覆盖）。
- **Inspector 微调**：AssetBrowser 选中 `.table` 条目时面板底部长出内嵌表格区
  （ImGui Table 先例 = InspectorPanel 字段表/ProfilerPanel）——双击单格 InputText
  改值写回 `.table`（原子写 + 主动 Rescan）。**不加新面板**（05 §3 面板集冻结核心 7
  纪律不破；AnimationEditor 另案解冻）。
- **运行时读取**：EnterPlay 时 `BuildPlayTableCache()`（BuildPlayClipCache 同款快照
  语义）解析进 World 级 `TableStore`（`World::Tables()`，ClipTable 同款"World 持有 +
  非 ECS"）；C# 经 vtable 表尾追加（3 项：行数/列数/取格）读字符串格——
  **零重录**（09 §6.8 先例二第三例：World 持有 + 非 ECS 机制一律零重录）。

**取舍**：
- 不选 **A（表格式编辑器为权威）**：Excel/Numbers 批量编辑/筛选/公式是登记①的原始
  诉求（16 波×N 条目在 ImGui grid 里逐格点远逊电子表格）；且编辑器内表格权威 = 要自建
  行列增删/撤销/复制粘贴全套，成本不成比例。
- 不选 **B（纯导入只读）**：Play 中调单格数值（ADR-011 丢弃语义下试错→Stop→把定值
  写进表）是调参高频动作，只读会逼作者回 Excel 走全量重导。

### D2：波次不外置——"波次表编辑器"并案结论 = 场景组件与表资产分工

- **WaveDirector 波次数据留在 Main.scene 组件数组段**（`waves[16]` 定长、Inspector
  `DrawArraySeg` 已可编辑、入 StateHash = 回放确定性面）——外置波次到表 = Play 期
  注入组件，平添一条"表→组件"同步链与回放说明义务，无增益。
- **表资产服务项目级数值配置**：武器参数/升级池/掉落表/成长曲线/伙伴参数——
  "场景内结构化玩法数据（组件，回放哈希面）vs 项目级数值表（资产，脚本读取面）"
  分工立界。`DrawArraySeg` 精细化（列宽/行复制等）若游戏侧真实卡点再另批，不并本批。
- > **D2 用户项目例外（2026-09-26 修订）**：D2 约束的**引擎模板与金档基准场**口径
  > 不变（vs-survivor 模板波次留组件）。用户项目可自行选择表驱动波次——首个先例
  > = svr-test（用户拍板，作表通道使用范例）：`Assets/tables/waves.tab` +
  > `WaveTableLoader.cs`（Awake 读表 → `GetWave/SetWave` 写回组件 → 引擎
  > WaveDirectorSystem 消费链零改动）。"表→组件"载入器归用户脚本层，不进引擎；
  > 用户项目非金档基准，无回放说明义务。SDK 侧配套：WaveDirector/WaveDef 镜像加
  > 拷贝语义读写口（布局冻结不动，纯加方法）。

### D3：XP 曲线可配 = World 级单参数（默认值不变），逐级查表曲线 v1.1

`xpCurveK` 现为引擎硬编码常量（`Systems.cpp:795`，消费于 `xpToNext` 组件字段演化）。
提为 World 级可调参数（TimeScale 同款：getter/setter + vtable 尾加 2 项），**默认值
= 现硬编码值**（三金档哈希流逐位不变，零重录）；模板 Start 时从表读值覆盖。
逐级查表曲线（level→xp 表）登记 v1.1（升级连升循环在引擎系统内，逐级注入动系统
消费链，超本批）。

## 后果

- 正面：svr-test"每加内容 = 复制 behaviour 手填常量"路径被表替代；验收②
  （新增 1 武器 + 1 敌人变体仅靠 prefab/C#/配置表）有了承载物；CSV 作者流
  （Excel→拖入）与微调流（编辑器内单格）各取所长。
- 负面/边界：CSV→.table 单向（csv 不留 = 不能从 .table 导回 csv，接受——.table
  本身是可读 JSON）；全字符串格把类型错误推迟到 C# 运行时（Table.Int 容错 +
  列头约定缓解）；EnterPlay 快照语义 = Play 中改表不生效（与 clip 同款，面板/表格
  区提示行交代）。
- 关联纪律兑现：新增第三方库红线未触碰（CSV 自写 + nlohmann 既有）；vtable 表尾
  追加约定第五次沿用；AssetType 扩展需核对 `.meta` type 序列化形态（字符串名则
  自由插位，数值则必须尾加保序——批文件 T1 首条核对项）。
