# M7a 批①：缺陷第二批修复（D6/D7/D8/M21/M22–M25）

2026-10-03 · M7a 批①（[批文件](../Plans/M7a/2026-10-03-b1-defect-batch2.md)——修法/锁设计/完成明细以其 §1/§3 为准；M13/M14 已证伪不修）

## 摘要

七条全修 + 顺手两簇（右键路径 `sg` erase 后失效引用 UB ×2；QuickCreate/Duplicate 失败行残留 ×2——报告未点、M24 同簇）：

- **D7 残余**：`ValidateAssetName` 校验单源（拒 `"`/控制字符/＞64B + 旧四则）接四处创建/改名口。写侧转义确认为 review 2026-10-02 #5 已修（JsonEscape + roundtrip 预验），不重复。
- **D6 残余**：集名输入/双击入口 ro 守卫 + 三个 commit 口 `Playing()` 双判；右键/工具条守卫为既有。
- **M21**：manifest `.bak` 上一代 + durable 原子写（fsync/_commit）+ 坏主档 .bak 恢复；实现期二次坑 = .bak 只收可解析档（坏主档转存毒化唯一好备份）。
- **M22**：`SanitizeClipEvents` 删帧清理 + TrySave 防御位——事件无 UI 编辑面，硬拒即自锁。
- **M23**：ResetEditingState 清 inline 改名态/搜索串。
- **M24**：集面结构性操作收口两助手（失败回插行 + 红字）；「删除动画文件」重排为先存集后删文件（不可逆步最后）。
- **M25**：独立 `setDirty_` + 集名 Enter/失焦即存。
- **D8**：UiSubsystem 四处装载/重载失败路径强制清 shown/modal + 红字（原静默）；条目保留可重试。

## 锁与验证

- 单测 +25 → engine-tests **34098**：`TestValidateAssetName`（正 2 负 8）/ `TestClipEventBounds`（阴性内置：未清理越界事件 Parse 必拒 = 自锁本体反例）/ `TestManifestBakRecovery`（三精灵删二判别设计：恢复失败 hero 重排拿首号必不等 gen1——阴性可分）。
- D8 = smoke-uirml `d8(rollback)` 新位（frame 195，双模式）；**阴性验证过**（回退修复 → rollback=0 FAIL → 复原复绿）。
- 门格：ctest 3/3 / script-tests 1776 / smoke-anim·uirml×2 单跑绿 / 回归 **full 17/17**。

## 实现期坑（登记）

1. **mv 复原 + ninja mtime**：阴性验证回退用 `mv` 保留旧 mtime → ninja 跳过重编 → 回归跑旧二进制假红一轮（15/17，uirml 双挂 d8=0）。touch 重编后 17/17。今后复原一律 touch。
2. **RmlUi 坏 XML 宽容**：未闭合标签实测解析"成功"——D8 探针毒化改删档（读盘失败，同走 `return false` 位）。
3. **探针层序污染**：D8 探针复原 Show 发新 showSeq 会盖住 dyn（无脚本层序断言饿死）——挪 195 帧（层序两拍后/Stop 前）+ origin 取 Scene（翻 Edit 会豁免 Stop 清场污染 p2 面）。
4. M24 失败注入未自动化（ImGui 注入面不扩）；M25 深层接线 + clip 面换目标无确认 = 同族结构债登记。

## 现场

改动未提交（待用户指令）。下一步批② `Engine/Assets` 资产读取核心（搬家批，金回放零重录强验证窗口期——本批已把 editor-core 数据面修干净，搬运基线干净）。
