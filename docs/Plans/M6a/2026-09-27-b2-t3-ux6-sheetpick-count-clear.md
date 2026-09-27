# T3-UX6 选帧对话框已选计数 + 清空（三图优化点③）

2026-09-27 · 批② 动画生产线 · 三图走查报告五优化点之 3/5（优先级建议 3 最高，
用户拍板继续）。

## 背景与现状核对

走查条目原文："选帧对话框有全选+选择序号，但无已选计数/清空——select frames
from sprite sheet.png 右参数区"。**动手前 git 考古核对**：底行"已选 %d 帧"
计数自 T3+T3b 就有、右栏"清空"钮是 T3-UX2 对话框重做批（9137fcb）加的——条目
按 Godot 参考图的位置口径（右参数区）衡量，**真实缺口 = 选择操作发生处（右栏）
没有计数反馈**：调分割参数、点选、全选时都得看底行才知道选了几帧；且全选/清空
是不显眼的 SmallButton（Godot 参照为常规钮）。本轮补齐位置缺口 + 升格按钮 +
全链回归位，而非从零实现。

## 决策

1. **计数画在右栏「选择」区内**（全选/清空正下方）：选择操作处即时反馈；
   底行独立计数文字**删除**（与右栏重复）——底行动态按钮文案本就带计数
   （"添加 N 帧"，Godot Add N Frame(s) 同款），三处同数 = 冗余。
2. **全选/清空 SmallButton → 常规 Button**：选择是本对话框主操作，与 Godot
   参照一致；行为零改动（同款 if 体）。
3. **计数计算上移**：`cnt` 原在底行段算，上移到框选矩形归一化后（图区子窗内，
   右栏/底行渲染之前）——右栏与底行/按钮同源同值；顺带 `pickCountLast_`
   渲染侧缓存 = 冒烟探针（与 T3-UX5 SegRowCache 同思路：断言的就是画出来的）。
4. **smoke 全链真实点击**：全选/清空/取消三钮走 TestHooks 矩形 + hold/release
   隔帧注入（smoke-ui 同款），不走语义直调——按钮升格 + 计数直染都是纯 UI 改动，
   真实管线点一遍才作数；开对话框用语义直调 `OpenSheetPickForTest`（第一段文件
   选择器已有 pick(click/shift/all) 独立回归位，不重跑）。
5. 顺手两件：① FilePicker「取消」登记 `picker.cancel` 探针——既有 picker 链
   （f50–66）测完**不关窗**，本轮 f68 真实点击关掉再开选帧对话框，消掉双层
   模态叠着的隐患，顺带给取消路径补上回归触点；② 取消后断言 `pickOpen_`
   回落（v3.1 已有"被外力关复位"防僵尸逻辑，回归位补上）。

## 改动

- `Editor/Panels/AnimationPanel.cpp`（DrawSheetPicker）：`cnt` 计算上移 +
  `pickCountLast_` 缓存；「选择」区 = Button 全选/清空 + `已选 %d 帧` 文本 +
  `sheetpick.all/none/count` 三探针；底行删独立计数文字，取消钮登记
  `sheetpick.cancel`；新增 `OpenSheetPickForTest`（复刻 HandlePickerResult
  流 1 第二段就位）。文件头注释 v2.1 注记。
- `Editor/Panels/BuiltInPanels.h`：`OpenSheetPickForTest` /
  `SheetPickCountForTest`（读 `pickCountLast_`）/ `SheetPickOpenForTest`
  （读 `pickOpen_`）三钩子 + `pickCountLast_` 成员。
- `Editor/Tooling/FilePicker.cpp`：取消钮登记 `picker.cancel`。
- `Editor/App/EditorApp.cpp`：smoke-anim 注入链 f68–f88（关 picker → 直开
  对话框 → 点全选 → 断言计数=4（anim-sheet 4×1）→ 点清空 → 断言计数=0 且
  `sheetpick.count` 矩形在 → 点取消 → 断言关窗）；RESULT `pick(...)` 扩
  `sheet(all/clear/close)` 位进 animOk。

## 验证

- smoke-anim `=> OK` 一次过：`pick(click=YES shift=YES all=YES sheet(all=YES
  clear=YES close=YES))`，errors=0，既有 probe（左列 meta / 集模态 queued）
  全保持。
- 回归 full 14/14（ctest 3/3、基础/关闭/拖拽/UI 冒烟、资产/动画/模板/GUID/
  脚本链、终验、场景 roundtrip）。
- 截图目检未做（同 T3-UX5 会话限制）——像素观感归真人验收首条。

## 遗留

- 真人验收：开选帧对话框（加帧通道"从精灵表…"）——右栏「选择」区即时计数
  （拖框/点选/全选时跟着变）、全选/清空为常规钮、底行不再有重复计数文字。
- 三图报告余三点：胶片带帧序号+拖拽换序 / 列表模式行首小缩略图+模式记忆 /
  大预览 scrub 条（原优先级建议 3>2>4>5，③已销 → 余序 2>4>5）。
