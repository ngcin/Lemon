# EditorApp 批①：vs-survivor 模板生成器外迁（EditorApp.cpp 减脂第一刀）

- 日期：2026-09-29
- 性质：纯机械搬移（零行为变化）；EditorApp.cpp 瘦身序列批①
- 动机：EditorApp.cpp 7212 行 / 398KB（超 docs 单文件 ~30KB 规约 13 倍），
  其中 `Run()` 单函数 ~3470 行。膨胀机制 = 每个里程碑批次的最顺手落点都在
  EditorApp.cpp / Run() 里追加。重构总方案四批（用户已认批）：
  **批① 模板生成器外迁（本批）→ 批② 同类多 TU 机械拆分（Chrome/Actions/Scripts）
  → 批③ SmokeHarness 抽取（g_tpl* 全局 + final* 计量 + 冒烟状态机归 harness，
  Run 只留 播种/每帧/末帧裁决 三挂点）→ 批④ GameUiBridge/ScriptReloadPipeline
  顺路立门户**。原则：只做机械搬移、行为逐位不变、每批全量回归收口。

## 改动

- 新建 `Editor/Templates/VsTemplateGen.h`（57 行）+ `VsTemplateGen.cpp`（1305 行）：
  - 搬移原 EditorApp.cpp 304–1606 整块——`vs_template` 命名空间（GUID 常量 +
    WriteHitClips/WriteTableAssets/WriteUiAssets/WriteProceduralAssets/
    WriteGameSources 含 ~560 行内嵌 C# + RunGuidSmokeChain）+ `GenerateVsTemplate`。
  - **GUID 常量块上移头文件单源**：Run 内 smoke-template 采样（表计数
    `rowsOf(kWeaponsTab)` / 受击段判定 `kMonsterHitClip`）与生成器共用，
    模板 GUID 锚点不再散落实现文件。
  - 匿名命名空间 → 具名 `lemon::editor::vs_template`（外部要引用
    RunGuidSmokeChain/常量，内部链接保不住）；其余文本逐行原样。
- EditorApp.cpp：7212 → 5911 行（-1301）；加 include + 顶注出处一行。
- `Editor/CMakeLists.txt`：lemon-editor 源列表登记（显式列源，无 glob）。
- 编译期补齐：`Components/BehaviorComponents.h`（Health/Shooter/WaveDirector/
  WaveDef 所在——EditorApp.cpp 四件套没猜全，编译器纠出）。

## 范围调整（对既定批①方案的一处偏离）

`SeedBenchSurvivorScene` 顺延批③：它依赖 `WriteAnimSheetAssets`/`kAnimClipGuid`
（smoke-anim 夹具，Run 内 40+ 处引用），现在搬就得为它建批③马上会拆掉的
临时共享头；且它本属 bench 编排（冒烟/bench harness 一族），批③ SmokeHarness
是正确归属。`RunGuidSmokeChain` 按计划随 vs_template 迁出（`--smoke-guid`
回归步骤覆盖）。

## 验证

- **产物对拍（--gen-vs-template）**：改前/改后各生成一份完整模板项目（57 文件），
  归一化后全一致。归一化集 = 生成器固有随机量（实体 `Meta.guid`、扫描器自动
  meta 的 guid（README.md/project.lemon）、`importedAt` 时间戳——同版本二进制
  跑两次 diff 实证确认，与重构无关）；其余（Main.scene 6620B、六 prefab、
  三表、UI 三资产、Game/ 脚本、固定 guid meta）逐字节比。
- **回归 full 16/16**（tools/editor-regression.sh）：ctest 3/3 + 冒烟全绿，
  其中 template-chain / guid-chain 直接覆盖搬移代码。
- 两个既有告警与本批无关（HEAD 即有，git 确认）：`kSmokePngGuid` 死常量
  （定义后无使用点）、smoke-anim 采样 lambda 的 `ent` unused-parameter。

## 遗留与登记

- 批②③④见上方序列；批③时 `kAnim*` 常量块 + `WriteAnimSheetAssets` +
  `SeedSmokeProject`/`SeedBenchSurvivorScene` 一并归 SmokeHarness，届时顺手
  处置 `kSmokePngGuid` 死常量。
- 防复发纪律（总方案随批①生效）：新冒烟/CLI 模式不得进 `EditorApp::Run`，
  登记进（批③后的）SmokeHarness；编辑器源文件软上限 ~1500 行/TU。
