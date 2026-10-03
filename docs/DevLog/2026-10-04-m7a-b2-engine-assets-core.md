# M7a 批②：Engine/Assets 资产读取核心（搬家批）

**日期**：2026-10-04　**状态**：done（回归 full 17/17 首跑全绿 + 金回放零重录）　**批文件**：[Plans/M7a/2026-10-04-b2-engine-assets-core.md](../Plans/M7a/2026-10-04-b2-engine-assets-core.md)

## 动因

ADR-016 R1 主体工作项：`STB_IMAGE_IMPLEMENTATION` 与资产库全家（解析器/索引/解码）住在 `lemon-editor-core`——脱离编辑器的运行时连贴图都解不出来（M7.md 批① 判语）。批② 把**资产读取核心**下沉 `Engine/Assets`（namespace `lemon::assets`），编辑器改调不改语义；批③ 续搬 Play 装配、批④ 落 GameEntry 消费。

**纪律**：搬家批金回放零重录窗口期——期间不合入无关改动（M6c 批② 先例）；vtable 47 / 组件 id 31 / 系统序 20 零变动。

## 落地件（Engine/Assets/ 新增 9 TU）

| 件 | 来源 | 说明 |
|---|---|---|
| `AssetTypes.{h,cpp}` | 自 AssetDatabase 抽 | AssetType 枚举/AssetTypeName/TypeOf/GuidToHex/HexToGuid 单源（值序冻结；#88 恰 16 位校验随迁）。症状实证：ScriptHost 桥此前被迫自持 mini hex 解析（引擎不能依赖编辑器头） |
| `StbImage.cpp` | 搬自 `Editor/Tooling/StbImpl.cpp`（删原件） | stb 三实现单 TU（image+write+resize2）。消费面实查全在链接树内（AssetGpuCache/ThumbCache/smoke 播种/截图/模板生成）→ 全迁不分拆；`-w` 静音随行 |
| `AnimAsset.{h,cpp}` | 搬自 `Editor/Assets/ClipEdit.{h,cpp}` | ParseClipJson/ClipToJson/ParseAnimSetJson/AnimSetToJson/JsonEscape/ValidateAssetName/SanitizeClipEvents——逐行搬移只换 namespace |
| `ControllerAsset.{h,cpp}` | 搬自 `Editor/Assets/ControllerEdit.{h,cpp}` | ParseControllerJson/ControllerToJson |
| `TableAsset.{h,cpp}` | 搬自 `Editor/Assets/Csv.{h,cpp}` | ParseCsv/ParseTableJson/TableToJson/NormalizeTable/IsValidUtf8 |
| `AssetIndex.{h,cpp}` | 新建（AssetDatabase 只读子集语义） | manifest 快路径直读 {guid,type,spriteId,slice}（主档坏先试 .bak）+ 回退扫描（.meta 真源 + 路径排序单调发号 + 切片连号块）=「git clean -xfd 后可启动」机器保证；Sprite 补读 .meta 网格段；号域校验（越界/重复记账丢号重派，块账异常转全幅） |
| `TextureStore.{h,cpp}` | 新建（AssetGpuCache 运行时子集） | 解码→AtlasRegistry（首次导入路径逐行同源；无 watcher/缩略图/热重导/Evict）。消费者 = 批④ GameEntry |
| `ProjectFile.{h,cpp}` | 新建 | project.lemon 只读解析（name/guid/engineVersion/entryScene；版本不匹配警告不阻断）+ ResolveEntryScene 回退链（entryScene → 唯一 .scene（扫 Scenes/+Assets/）→ 多场景空串归调用方红字） |
| `SpriteRefs.{h,cpp}` | 下沉自 `EditorContext::ResolveSpriteRefs` | 纯函数化 + `SpriteRefSource` 轻虚接口（SpriteByGuid/SpriteByWholeId/SpriteIdBase → SpriteEntryView 视图）——AssetDatabase 与 AssetIndex 各实现；EditorContext 留薄壳（dirty/日志位） |

**编辑器侧**：AssetDatabase 改 using 引擎类型 + GuidHex 内联转发（既有 `AssetDatabase::GuidToHex` 调用面 14 文件零扰动）+ 实现 SpriteRefSource（查询现算单槽 scratch——Remove 同帧语义与原 FindByGuid 直查逐行同源）；解析器调用点 namespace 改（EditorContext/AnimationPanel/AssetBrowserPanel/BuiltInPanels/EditorAppActions/EditorAppSmoke）；Editor/CMakeLists.txt 删四源。

**不动的**（范围纪律，登记）：WriteFileAtomic 留 AssetDatabase（批③ SaveStore 随批再挪）；ScriptHost mini hex 解析不统（金回放敏感面，批④ 装配期处置）；AssetGpuCache 本体不动（双扫描器中间态，ADR-016 登记合并归 M8）。

## 实现期发现

- **SpriteRefSource 接口命名撞车**：`FindByGuid/FindBySpriteId` 与 AssetDatabase 既有同名 API 签名不同不可共载 → 接口方法定名 `SpriteByGuid/SpriteByWholeId`；`FindBySpriteId`（含切片区间）与回填面（仅本体号）语义不同——AssetIndex 补 `FindByWholeSpriteId` 专用口并注明「回填面不可用含区间的口」（cell 号命中会把切片引用升级成整图 guid）。
- **视图缓存的生命期坑（自抓）**：首版 spriteViews_ 随 Rescan 重建——但 Remove() 置 missing 不触发 Rescan，同帧窗口里视图 alive 仍真（原语义该 dangling）。改查询现算单槽 scratch（entries_ 即事实），缓存与失效问题一并消除。
- **AssetIndex 快路径不读 .meta 的 guid**：manifest 即编辑器/packager 写下的账（"在场且合法 = 快路径"），.meta/manifest 漂移检测是编辑器写侧体检职责——运行时只读不发明事实。Sprite 条目仍补读 .meta 网格段（切片像素几何不入 manifest；一次小 IO 与像素装载同量级）。
- **missing 守卫审计结论**（计划预估"清 30+ 处"实测不成立）：绝大多数字段消费处处于「Remove() 后到下轮 Rescan 前」的**真实同帧窗口**（Remove 不触发 Rescan、watcher 500ms 轮询、删资产后立即 EnterPlay 是真实序列）——FindBySpriteId 族/BuildPlay 四缓存/MountPlayAudio/Inspector/Apply-Revert/SaveManifest（Remove 内即调）均**非恒真，全部保留**。真恒真仅 1 处：Rescan 低 32 位体检（读的是上方刚重建的 entries_）——已清并注释说明；SaveManifest 过时的"墓碑"措辞顺带修正为现行语义。

## 验证（门格）

- 构建 0 error（mac preset 全目标；imgui-isolation 断言随 ctest 过——ImGui 头仍不出 Editor/）。
- **单测 34145（34098 → +47）**；ctest 3/3。新增：`TestProjectFile`（全字段/最小/坏档/版本不阻断 + entryScene 四态）、`TestSpriteRefsEngine`（mock 源四态 + 幂等——引擎侧锁进程独立性，编辑器端到端仍归 TestSpriteGuidResolve）、`TestAssetIndexConsistency`（快路径全等：guid→path/type/spriteId/slice 对 AssetDatabase 记账逐条比对；回退：删 manifest+.bak 后 guid/type 全等 + 路径序派生号确定（hero=100/切片块 102..105 跨 Open 同号）+ 本体号/cell 号查询面 + 无 .meta 散文件不认）。既有解析器测试（TestClipEdit/TestCsvTable/TestControllerAndGraph 等 1574+ checks）改调 `lemon::assets::` 后**全绿 = 搬家对拍全等**（不搞双实现并存：既有测试即"旧实现"预期）。
- **回归 full 17/17 首跑全绿**——金回放判据（script-chain "play byte-exact" + guid-chain 逐实体 resolve）零重录零漂移 = **纯搬家强验证过**。
- bench-survivor（1000 帧 ×3 采样）：**fps=75/79/77**，均 PASS；帧八段 sim 主导（11.3–11.9ms）与资产层无关（解析装载一次性、每帧路径零变化）——历史波动带 73–84 内，零降级判定成立。

## 关联

- [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md) M2（Engine/Assets 清单四件 + stb TU）批② 项全落
- 下游：批③ Play 装配下沉（PrefabCache/SaveStore/UiMount/AudioMount/相机 follow/ExtractScene）；批④ GameEntry 消费 AssetIndex/TextureStore/SpriteRefs/ProjectFile
