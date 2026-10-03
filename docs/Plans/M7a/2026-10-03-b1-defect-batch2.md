# M7a 批①：缺陷第二批（评审 §6/§8：D6/D7/D8/M21/M22–M25）

Status: **done**（2026-10-03 一日收口：七条全修 + 单测 +25 + D8 smoke 锁带阴性验证 + 回归 full 17/17；M13/M14 已证伪不修）

> [M7a.md](./M7a.md) §4 批① 分解的落名批文件。排序理由：毁资产类先修（D7 校验/M21 manifest 加固——packager 与运行时都将消费这些文件）；D8 修在原位，批③ 搬运时随迁。流程照 M6c review 热修先例：修 + 回归锁 + 阴性验证。出口判据：每修带阴性验证；回归 full 17/17 + 单测增长。

## 0. 开工现场核对（2026-10-03，报告行号 = 2026-09-30 版，现行号有漂移）

- **D7 写侧半边已被 review 2026-10-02 #5 修过**：`ClipToJson`/`AnimSetToJson` 已走 `JsonEscape` + 直接拼接（128B 缓冲退役），且 TrySave/TrySaveSet 均有落盘前 roundtrip 预验——「写坏 JSON 毁资产」主径已闭合。**本批残余 = 改名/建段校验硬化**（拒 `"`/控制字符/限长，四处共用）。
- D6 右键菜单已有 ro 守卫（报告未列）；**残余 = 集名输入（1271）/双击改名入口（1373）/commit 双判**。
- M24 同簇新发现：两处右键路径 `erase` 后继续读 `sg.clipGuid`（vector 失效引用 UB）；QuickCreate/Duplicate 的 `TrySaveSet` 失败时已 push 的段行未回滚（报告只点了删除路径）。

## 1. 修法与锁

| # | 修法 | 锁 |
|---|---|---|
| D7 | `ClipEdit` 新增纯函数 `ValidateAssetName`（拒空/`/`/`\`/`..`/`"`/控制字符/＞64B）；四处接入：TryCreateClip/TryCreateSet/CommitSegRename/QuickCreateSegment | engine-tests `TestValidateAssetName`（正例 + 每负例一断言；「无校验则写坏档」由 roundtrip 既有测试天然覆盖） |
| D6 | 集名输入包 `BeginDisabled(ro)`；双击入口加 `!ro`；CommitSegRename/QuickCreateSegment/DuplicateSegment 顶部 `Playing()` 双判（入口禁用 + commit 拒绝，Inspector 橙幅同语义） | smoke-anim 链回归（面板只读面已有 smoke 位）+ 代码走查；交互注入面不新增（范围纪律） |
| M22 | `ClipEdit` 新增 `SanitizeClipEvents`（删 `frame ≥ frames.size()` 的事件）；`DeleteSelectedFrames` 删帧后调用（提示 N 个已移除）+ `TrySave` 落盘前防御性调用（roundtrip 前清——不清则 roundtrip 必拒 = 自锁复现） | engine-tests `TestClipEventBounds`（带事件 clip 删帧 → sanitize → ClipToJson/ParseClipJson roundtrip 过；**阴性内置**：不 sanitize 的原数据 Parse 必拒——即缺陷本体作反例） |
| M23 | `ResetEditingState` 补清 `segEditIdx_ = -1` / `segEditBuf_.clear()` / `segFilter_.clear()` | smoke-anim 既有 leftcol/create 位回归（串写路径进不了自动化注入面，代码级修复 + 走查） |
| M24 | 六处 TrySaveSet 返回值收口：工具条/右键「从集移除」失败**回插行**；「删除动画文件」**重排为 先存集后删文件**（TrySaveSet 成功才 `db.Remove`——文件删除是不可逆步必须最后做）；CommitSegRename 失败**回滚 db.Rename + 段名**；QuickCreate/Duplicate 失败**弹出行**。顺手修两处 `sg` 失效引用（先拷值） | smoke-anim 回归 + engine-tests 不适用（ImGui 面）；失败注入（只读目录）留走查 |
| M25 | 集名输入 Enter/失焦即 `TrySaveSet`（`EnterReturnsTrue` + `IsItemDeactivated`）+ 独立 `setDirty_`（集面任何改动置位、TrySaveSet 成功清除、失败红字保留）——结构性操作本就即时存（M24 后失败也回滚），残余未存窗口 ≈ 打字中 | smoke-anim 回归；深层接线（面板 dirty → ctx_.dirty 退出确认、clip 面换目标无确认）登记结构债不扩批 |
| M21 | `SaveManifest`：写前 `.bak` 上一代（WriteSaveFile 同款 copy_file）+ tmp 写后 **fsync** 再 rename（POSIX `open/fsync`，Win 分支 `_commit` 归批⑦ 核）；`LoadManifest` 损坏时**先试 .bak 再重建**（红字注明来源） | engine-tests `TestManifestBakRecovery`（gen1→gen2 断言 .bak 内容=gen1；**阴性**：坏主档 + 好 .bak → 重开恢复记账不重排；主/备全坏 → 重建红字路径既有） |
| D8 | `LoadDocumentFromMemory/FromFile` 失败且条目已存在 → 强制 `shown=false, modal=false`（条目保留可重试，输入让出面即刻释放）；`ReloadDocument`/`ReloadAllDocuments` 失败 → 不回写 shown、强制清 + LEMON_ERROR（现失败路径静默） | smoke-uirml 增 poison-reload 位（已显文档写坏触发 watcher 重载 → 断言不卡 modal/输入释放）——实现期看夹具结构定注入形态 |

## 2. 施工序

ClipEdit（D7+M22 纯函数 + 单测）→ AnimationPanel（D6/M22/M23/M24/M25）→ AssetDatabase（M21 + 单测）→ UiSubsystem（D8 + smoke 位）→ 门格（ctest/单测计数/回归 full/阴性验证记录）→ 文档收口。

## 3. 完成情况（2026-10-03 收口）

| # | 状态 | 实施与验证摘要 |
|---|---|---|
| D7 | ✅ | `ValidateAssetName` 单源（拒空/`/`/`\`/`..`/`"`/控制字符/＞64B）接四处（TryCreateClip/TryCreateSet/CommitSegRename/QuickCreate）；`TestValidateAssetName` 正例 2 + 负例 8。写侧转义半边（JsonEscape + roundtrip 预验）确认为 review #5 已修，本批不重复 |
| D6 | ✅ | 集名输入 `BeginDisabled(ro)` 包输入本体；双击入口 `!ro`；CommitSegRename/QuickCreateSegment/DuplicateSegment 顶部 `Playing()` 双判（红字「沙盒只读」）；右键菜单/工具条 ro 守卫为既有（报告后已修），现场核对收窄 |
| M21 | ✅ | SaveManifest：写前 .bak 上一代（**只收可解析档**——坏主档转存会毒化唯一好备份，实现期发现的二次坑）+ `WriteFileAtomic` 增 `durable` 参（POSIX fsync/Win `_commit` 批⑦ 核）；LoadManifest 坏主档先试 .bak 再重建（主档缺失只有 .bak 也恢复）。`TestManifestBakRecovery`：三精灵删二件判别设计——恢复成功 hero 保 gen1 号，失败走重排 hero 独活拿首号必不等（阴性可分实测） |
| M22 | ✅ | `SanitizeClipEvents`（删 `frame≥frames.size()`）接 DeleteSelectedFrames（提示移除数）+ TrySave 落盘前防御位（成功消息带顺带移除数）。`TestClipEventBounds` 阴性内置：未清理的越界事件序列化→解析必拒（自锁本体作反例）→ 清理后 roundtrip 过 |
| M23 | ✅ | ResetEditingState 补清 `segEditIdx_/segEditBuf_/segFilter_`（+`setDirty_`）；smoke-anim 全链绿（create/flow/leftcol 位） |
| M24 | ✅ | 新助手 `RemoveSegmentAt`/`DeleteSegmentFile`（工具条+右键共用）：内存先改→TrySaveSet→失败**回插行**+红字；删文件重排为**先存集后删文件**（不可逆步最后做）；CommitSegRename 失败**回滚 db.Rename+段名**（回滚再失败 = 分叉红字交底）；QuickCreate/Duplicate 失败**弹出行**（文件保留为合法独立资产）。**顺手修两处 `sg` erase 后失效引用 UB**（拷值）；另修 QuickCreate/Duplicate 失败行残留（报告未点、同簇） |
| M25 | ✅ | 独立 `setDirty_`（集名打字中即置、结构性操作置位、TrySaveSet 成功清）+ 集名输入 **Enter/失焦即存**（`EnterReturnsTrue` + `IsItemDeactivated`，值变才落盘）——集面无提示丢失窗口压到打字中一瞬。深层接线（面板 dirty→ctx_.dirty 退出确认）与 clip 面换目标无确认为同族结构债，登记不扩批 |
| D8 | ✅ | LoadDocumentFromMemory/FromFile 失败且条目已存在 → 强制 `shown=false/modal=false`（条目保留可重试）；ReloadDocument/ReloadAllDocuments 失败不回写 shown、强制清 + LEMON_ERROR（原失败路径静默）。**smoke-uirml 增 `d8(rollback)` 位**（frame 195：武装 modal→删源文件→同名装载应失败且 shown 清→复原重载应成功→现场复原；时点/origin 选 Scene 保层序与 Stop 清场断言面）。**阴性验证过**：回退修复→`d8(rollback=0) FAIL`→复原复绿。实现期两坑：RmlUi 对坏 XML 宽容（未闭合标签解析"成功"→毒化改删档）；复原 Show 的 showSeq 会打乱层序捕获（探针从 99 挪 195） |

**门格（2026-10-03）**：构建 0 error；ctest 3/3；engine-tests **34098**（34073→+25）；script-tests 1776（=）；smoke-anim/uirml×2 单跑绿（d8=1）；回归 **full 17/17**（首跑 15/17 = 阴性验证复原用 `mv` 保留旧 mtime、ninja 跳过重编跑了旧二进制——touch 重编后 17/17，工具坑登记批文件 §4）。

**提交前自查 review 三修（2026-10-03 同日）**：① DeleteSelectedFrames 丢失 `saveMsg_.clear()`（旧「√ 已保存」滞留 dirty 态）补回；② 集名即存成功未推进 `setLoadedName_` 基准（每帧重置脏 + 失焦重存循环）+ 触发判据改「名字差值」防结构性操作的脏标被失焦意外重触发；③ 三处失败路径 `setMsg_` 取因在「集条目丢失」时会嵌陈旧消息——取因只在 TrySaveSet 真跑过时读。修后重验：单测 34098 + smoke-anim/uirml 绿 + 回归 full 17/17 首跑全绿。

## 4. 工具坑与登记

- **mv 复原 + ninja mtime**：阴性验证回退源码用 `mv`（保留备份时 mtime）→ ninja 判目标新于源跳过重编 → 回归跑旧二进制假红。今后阴性验证复原一律 `touch` 或重写文件。
- 探针毒化口径：RmlUi XML 解析对未闭合标签宽容（实测返回有效 doc）——「解析失败」路径难注入，读盘失败（删档）为确定失败路径，两者同走 `return false` 位。
- M24 失败注入（只读目录逼 TrySaveSet 失败）未自动化——ImGui 面注入面不扩（范围纪律），失败路径逻辑经代码走查 + 回滚结构对称性保障；批⑤ packager 校验器落地后可复用其只读场景补注入口。
- M25 深层接线 + clip 面换目标无确认：同族结构债登记（M8 打磨候选）。
