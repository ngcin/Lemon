# 2026-09-26 M6a 批② T1+T2：配置表通道全线贯通（.tab 资产 → 编辑器表格区 → TableStore → Lemon.Table）+ svr-test 波次表数据化范例

## 事件

批② A 线两任务同日完工（用户要求后缀定名 `.tab`，ADR-012 修订注记）：

- **T1 编辑器侧**：`AssetType::Table`（`.meta`/manifest type 字符串名 → 插位自由，
  实测落 `"table"`）；新 `Editor/Assets/Csv.h/.cpp`（mini CSV 解析：引号/转义/
  CRLF/BOM 剥除/GBK 拒入（UTF-8 校验）/参差行矩形化/上限 64 列 × 1024 行 ×
  128 码点；TableToJson↔ParseTableJson roundtrip，JSON 侧裸数值/布尔宽松归一）；
  `ImportFile` csv→tab 转换分支（csv 源不拷入，重拖同名 = 覆盖再导入且 guid 稳定）；
  AssetBrowser 单击选中 + `.tab` 内嵌表格区（列头钉住/ScrollY 裁剪/双击单格编辑
  原子写回 + 主动 Rescan，EnterPlay 快照语义提示行）。
- **T2 引擎+SDK 侧**：新 `Engine/ECS/TableStore`（World 持有 + 非 ECS，不入
  StateHash——09 §6.8 先例二第五例）；`BuildPlayTableCache()`（EnterPlay 快照，
  坏表 warn 不炸 Play）；vtable 尾加 3 项（tableRows/tableCols/tableCell）；
  SDK `Lemon.Table`（Int/Float Invariant 容错 + warn 一次/格）；
  WaveDirector/WaveDef 镜像加 GetWave/SetWave/GetEntry/SetEntry 拷贝语义读写口
  （布局冻结不动，纯加方法）。

## svr-test 波次表数据化（使用范例；ADR-012 D2 用户项目例外）

用户拍板将 svr-test Main.scene 的 WaveDirector 波次外置为 `.tab` 作表通道范例。
D2 约束的引擎模板与金档口径不变；用户项目自选表驱动波次，"表→组件"载入器归
用户脚本层：

- `Assets/tables/waves.tab`：16 波全量迁移（含 boss 波），prefab 列存**完整
  16 位 GUID**（比 Inspector 裸低 32 位可读且改名稳定），载入时取低 32 位；
- `Game/WaveTableLoader.cs`：Awake 读表 → GetWave/SetWave 灌回组件 →
  引擎 WaveDirectorSystem 消费链零改动；Main.scene 波次清零 + Director 挂
  `scripts[]`（并行游戏线首次消费批⓪ 多脚本 schema）。

## 实测

| 项 | 结果 |
|---|---|
| ctest（mac + mac-debug） | 3/3 ×2 全绿 |
| engine-tests | 33410 checks（+35：Csv/TableJson 单测 ×21 + csv→tab 导入生命周期 ×14） |
| script-tests | **1566 checks**（+12：TestTableChannel 端到端；热重载名单 12→13） |
| editor-regression full | **14/14 PASS** |
| 金回放三档 | sim-st/sim-mt/script 现录现放 **mismatches=0**（T2 动了 Engine 但
  TableStore 不入哈希 + 基准场零表脚本 = 零重录结构性证明） |
| 真进程整链（svr-test） | `--bench-scene Main.scene --frames 900`：`Play 表 17×19` →
  `waves.tab 载入：16 波（首波 t=5）` → alive 2→47（首波刷怪 + 玩家击杀平衡） |

## 观察 / 遗留

- smoke 模式的 script-spawn 断言预设 TestScript 的 SpawnerBehaviour——用户程序集
  （--script Game.dll）跑 `--smoke --play` 必 FAIL，属既有断言语境错配（非本次
  引入）；用户项目功能验证用 `--bench-scene`（只报数、判读 alive/日志）。
- `.tab` 微调写回与 FileWatcher 防抖（F-15）实测无双扫描抖动（主动 Rescan 去重）。
- 批② 余项：T3 AnimationPanel、T4 模板表落地 + 验收② 演示、T5 存档分档、
  T6 全量收口。
