# M7a 批②：Engine/Assets 资产读取核心（搬家批）

Status: **done**（2026-10-04：九 TU 全落 + 单测 +47 → 34145 + ctest 3/3 + 回归 full 17/17 + **金回放三档零重录**（纯搬家强验证过）；missing 守卫审计结论见 §4；[DevLog](../../DevLog/2026-10-04-m7a-b2-engine-assets-core.md)）

> [M7a.md](./M7a.md) §4 批② 落名批文件。**搬家批金回放零重录窗口期纪律：期间不合入无关改动**（M6c 批② 先例）；vtable 47 / 组件 id 31 / 系统序 20 零变动 → 零重录预期成立。出口判据：ctest 3/3 + 单测增长；编辑器改调后回归 full 17/17 + **金回放零重录（纯搬家强验证）**。

## 0. 开工现场核对（2026-10-04）

- **stb 消费面实查**：load = AssetGpuCache（导入）/ThumbCache（缩略图）/smoke 播种；write = 截图（EditorApp/smoke-uirml）+ ProjectWizard/VsTemplateGen 模板播种；resize2 = ThumbCache 专属。全部消费方经 lemon-editor-core/lemon-editor 链接树可达 lemon-engine → **三实现全迁单 TU**（无分拆必要；ADR-016 M2 "write 留编辑器或全迁按消费面定" → 全迁）。
- **解析器三件是纯逻辑**（零 ImGui/GPU；ClipEdit.cpp 头注释本就自证"可单测"）→ 整文件搬，非函数抽取。类型名（ClipData/ControllerData/TableData 等）**保持不变只换 namespace**（`lemon::editor` → `lemon::assets`），调用面最小扰动。
- **AssetType 枚举/AssetTypeName/TypeOf/Hex 转换**被两侧共需（AssetDatabase 写侧 + AssetIndex 只读侧；ScriptHost.h 桥内已自持 mini hex 解析 = "解析器住编辑器"症状实证）→ 单源下沉 `Engine/Assets/AssetTypes.h`。
- **`missing` 守卫审计预告**：字段语义只剩"编辑器 Remove() 后到下轮 Rescan 前的同帧隐藏位"——同帧窗口在 BuildPlay*Cache/面板查询路径**真实可达**（Remove 不触发 Rescan），这些守卫不是恒真。预计实清 = Rescan 内部刚重建表后的数处；实清数与保留理由记 §4，不机械凑 30。

## 1. 件清单（全部目标 = `Engine/Assets/`，namespace `lemon::assets`）

| # | 件 | 来源/形态 | 要点 |
|---|---|---|---|
| 1 | `AssetTypes.{h,cpp}` | 新建（从 AssetDatabase 抽） | AssetType 枚举（值序不变）+ AssetTypeName + TypeOf(relPath) + GuidToHex/HexToGuid（含恰 16 位校验）单源；AssetDatabase 改 using/内联转发（编辑器 API 面 `AssetDatabase::GuidToHex` 调用点 14 文件零扰动） |
| 2 | `StbImage.cpp` | 搬自 `Editor/Tooling/StbImpl.cpp`（删除原件） | 三实现单 TU（image + write + resize2）；THIRD_PARTY 无新登记（stb 在册只动 TU 归属）；`-w` 静音随行 |
| 3 | `AnimAsset.{h,cpp}` | 搬自 `Editor/Assets/ClipEdit.{h,cpp}` | ParseClipJson/ClipToJson/ParseAnimSetJson/AnimSetToJson/JsonEscape/ValidateAssetName/SanitizeClipEvents + 类型族 |
| 4 | `ControllerAsset.{h,cpp}` | 搬自 `Editor/Assets/ControllerEdit.{h,cpp}` | ParseControllerJson/ControllerToJson + 类型族 |
| 5 | `TableAsset.{h,cpp}` | 搬自 `Editor/Assets/Csv.{h,cpp}` | ParseCsv/ParseTableJson/TableToJson/NormalizeTable/IsValidUtf8 + TableData + 上限常量 |
| 6 | `AssetIndex.{h,cpp}` | 新建（AssetDatabase 的只读子集语义） | 扫 Assets/** + 根级 Prefabs/**（06 §1 布局同源排除规则）；`.lemon/manifest.json` 在场合法 = **快路径**直读 {guid,type,spriteId,slice}；缺失/损坏 = 回退扫描（.meta 读 guid + 路径排序单调发号 spriteId + 切片连号块）红字——"git clean -xfd 后可启动"机器保证；`FromManifest()` 探针位 |
| 7 | `TextureStore.{h,cpp}` | 新建（AssetGpuCache 运行时子集骨架） | 解码→CreateTexture→BindTextureToSlot→RegisterAtlas→AddSpriteAt→切片登记；无 watcher/缩略图/热重导/Evict；消费者 = 批④ GameEntry（本批编译面达标即可，无 GPU 单测——引擎 tests 无窗口纪律） |
| 8 | `ProjectFile.{h,cpp}` | 新建 | project.lemon 只读解析（name/guid/engineVersion/entryScene；engineVersion 不匹配 = 警告不阻断）；ResolveEntryScene 回退链：entryScene → 唯一 .scene（扫 Scenes/ 递归）→ 多场景缺字段 = 空串归调用方红字 |
| 9 | `SpriteRefs.{h,cpp}` | 下沉自 `EditorContext::ResolveSpriteRefs` 纯函数化 | `SpriteRefSource` 轻虚接口（FindByGuid/FindBySpriteId/SpriteIdBase → SpriteEntryView）——AssetDatabase（编辑器）与 AssetIndex（运行时）各实现；EditorContext 留薄壳（dirty/日志位） |

**编辑器侧**：Editor/CMakeLists.txt 删四源；调用点 namespace 改（AnimationPanel/AssetBrowserPanel/EditorContext/EditorAppActions/smoke 族/BuiltInPanels.h/engine_tests）；AssetDatabase 实现 SpriteRefSource；EditorContext::ResolveSpriteRefs → 薄壳。

**不动的**（范围纪律）：WriteFileAtomic 留 AssetDatabase（写侧工具，批③ SaveStore 随批再挪）；ScriptHost 自持 mini hex 解析不统（金回放敏感面，登记批④ 装配期处置）；AssetGpuCache 本体不动（双扫描器中间态，ADR-016 已登记合并归 M8）。

## 2. 单测

- `TestAssetIndexConsistency`：临时项目（多类型资产 + 切片 meta）→ AssetDatabase 开项目建账 → AssetIndex 快路径：guid→path/type/spriteId/slice 两路全等 + FromManifest=true；删 manifest（git clean -xfd 模拟）→ 回退扫描：guid 全等（.meta 真源）、spriteId 路径序派生两次 Open 同号（确定性）、FromManifest=false。
- `TestProjectFile`：entryScene 三态（声明在场/唯一 .scene 回退/多场景缺字段空串）+ engineVersion 差异 = ok 不阻断 + 坏档 ok=false。
- `TestSpriteRefsEngine`：引擎函数级四态（guid 命中归一/悬空保号/存量回填/切片区间外回 cell 0）——mock source，进程独立性证明（TestSpriteGuidResolve 编辑器端到端已有，保留）。
- 解析器既有测试（TestClipEdit/TestCsvTable/TestControllerAndGraph/TestValidateAssetName/TestClipEventBounds/TestAnimSetAndClipIndex/TestTableAssetImport/TestManifestBakRecovery 等）改调 `lemon::assets::`——**改 namespace 后全绿 = 搬家对拍全等**（1574+ checks 即"旧实现"预期双跑；不搞双实现并存）。

## 3. 门格

构建 0 error（含 imgui-isolation：ImGui 头仍不出 Editor/）→ ctest 3/3 → engine-tests 单测计数 ≥ 34098 + 增量 → 回归 full 17/17（含金回放三档 mismatch=0 = **零重录强验证**）→ `git clean -xfd` 复验口径由 AssetIndex 回退扫描单测承担（批④ 实机复验）。

## 4. 完成情况（2026-10-04 收口）

| # | 状态 | 实施与验证摘要 |
|---|---|---|
| AssetTypes | ✅ | 枚举值序冻结搬移；AssetDatabase 改 using + `GuidToHex/HexToGuid` 内联转发（14 文件调用面零扰动）；TypeOf 三调用点改 `assets::TypeOf` |
| StbImage | ✅ | 三实现单 TU 全迁（消费面实查全在链接树）；`Editor/Tooling/StbImpl.cpp` 删除；`-w` 静音随行；THIRD_PARTY 无新登记（stb 在册只动 TU 归属） |
| 解析器三件 | ✅ | `ClipEdit/ControllerEdit/Csv` → `AnimAsset/ControllerAsset/TableAsset` 整文件搬（逐行只换 namespace，类型名不变）；调用点 EditorContext/AnimationPanel/AssetBrowserPanel/BuiltInPanels/EditorAppActions/EditorAppSmoke/engine_tests 全改 `assets::`；既有测试改调后全绿 = **搬家对拍全等**（不搞双实现并存） |
| AssetIndex | ✅ | manifest 快路径（主档坏先试 .bak）+ 回退扫描（.meta 真源 + 路径序发号 + 切片连号块）+ 号域校验（越界/重复记账丢号重派、块账异常转全幅）+ `FindByWholeSpriteId` 本体号专用口（回填契约）；`FromManifest()` 探针位 |
| TextureStore | ✅ | AssetGpuCache 首次导入路径逐行同源的运行时子集；无消费者（批④ GameEntry 接）——编译面达标，GPU 面归批④ game-smoke |
| ProjectFile | ✅ | 四字段解析 + engineVersion 不阻断 + ResolveEntryScene 回退链四态单测锁 |
| SpriteRefs | ✅ | 纯函数 + `SpriteRefSource` 接口（SpriteByGuid/SpriteByWholeId——方法名避开 AssetDatabase 既有 API 不可共载）；AssetDatabase 现算单槽 scratch 实现（同帧语义逐行同源）；EditorContext 薄壳（dirty/日志位） |
| missing 审计 | ✅ | **"清 30+ 处"实测不成立**：绝大多数字段消费处处于 Remove→下轮 Rescan 的真实同帧窗口（Remove 不触发 Rescan/watcher 500ms/删后立即 EnterPlay）——查询族/BuildPlay 四缓存/MountPlayAudio/SaveManifest 均非恒真**全部保留**；真恒真仅 Rescan 低 32 位体检 1 处已清（读上方刚重建的 entries_）+ SaveManifest"墓碑"措辞修正 |

**门格（2026-10-04）**：构建 0 error；ctest 3/3；engine-tests **34145**（34098→+47：TestProjectFile 10 / TestSpriteRefsEngine 10 / TestAssetIndexConsistency 27）；回归 **full 17/17** 首跑全绿（script-chain play byte-exact = 金回放 script 档零重录）；bench-survivor 三采样 **fps=75/79/77**（历史波动带 73–84 内，帧八段 sim 主导与资产层无关——搬家批每帧路径零变化，零降级判定成立）。

**实现期发现**（详 [DevLog](../../DevLog/2026-10-04-m7a-b2-engine-assets-core.md)）：① SpriteRefSource 方法名撞车（FindByGuid 签名不可共载）；② 首版视图缓存撞 Remove 同帧生命期坑（自抓自修：现算单槽）；③ 快路径不读 .meta guid（manifest 即账，漂移检测归编辑器写侧体检）；④ ScriptHost mini hex 解析不统（金回放敏感面，登记批④ 处置）。

**登记移交**：WriteFileAtomic 下沉归批③（SaveStore 随批）；AssetGpuCache/AssetIndex 双扫描器合并归 M8（ADR-016 既有登记）。
