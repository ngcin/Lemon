# T3-UX7 创建入口精简：向导退役 → 极简文件夹创建框 + 裸 clip 双击断路修复

2026-09-27 · 批② 动画生产线 · 用户讨论定案（"向导配置杂乱"+“双击动画剪辑没法
编辑"两反馈收敛，方案经用户逐点确认后实施）。

## 背景

文件夹右键「从此文件夹创建动画…」打开的是三 tab 大向导（从文件夹/从精灵表/
空白 + 源目录 combo + 落点目录手输）——从文件夹进来时 2/3 的内容是噪音。用户
实际工作流已收敛为：**文件夹快速建独立 clip → 集工作台"从动画剪辑复制"入集**
（两步流，用户明确表示好用，保留不动）。同时用户报告：双击动画剪辑没法编辑。

## 双击断路诊断（讨论轮发现）

双击路由本身三分支齐全不冲突（.override 直开集 / .anim 集成员→开集选段 /
裸 clip→SetTarget），**但裸 clip 分支只设 targetGuid_ 不清 setGuid_**——OnGui
集模式优先，面板开着集时双击独立 clip 被集吃掉渲染 = "双击没反应"。用户工作流
几乎总开着集，必现。修复：裸分支先 `OpenSet(0)` 再 `SetTarget`。

## 决策（用户逐点拍板）

1. **三 tab 向导退役**，文件夹右键 → 极简单页创建框：源文件夹只读行 +
   缩略图预览 + 名称（预填文件夹名）/fps/循环 + 保存位置只读行（Unity 同款
   无位置字段——"落点"一词退役，固定 = 浏览器当前目录）+ 创建(Enter)/取消。
2. **空白区右键「新建动画剪辑…」删除**（用户定论"没啥用"）。创建动画只剩：
   集内段 = 工作台左列 inline；独立 clip = 文件夹右键（唯一入口）；从精灵表
   建"新"动画 → 集内建段 + 加帧从精灵表（先起名后选帧）。向导的 createMode
   流（流 3）与 wizFrames_/wizPicked_/pickCreate_ 死管道全删。
3. **文件夹右键「新建动画集…」删除**（用户定论：集都从 Animations 空白区
   右键建；"并建首段"是 T3b 时代语义，v3.3 集子文件夹约定后已别扭）。
4. 独立 clip 通道**保留**（用户两步流依赖）；"右键直建入集"方案否决——
   浏览器操作耦合面板开集状态易出意外。

## 改动

- `Editor/Panels/AnimationPanel.cpp`：`DrawWizard` → `DrawFolderCreate`
  （460px 单页 AlwaysAutoResize；clipcreate.ok 探针）；`StartCreateFromFolder`
  瘦身；删 `StartCreateBlank`；`StartSheetPick(app)` 去 createMode；
  `HandlePickerResult` 流 3 删；选帧对话框底行 pickCreate_ 分支删（只剩
  添加/替换）。
- `Editor/Panels/BuiltInPanels.h`：签名/成员同步（wizTab_/wizFrames_/
  wizPicked_/pickCreate_ 删；wizPath_ → wizSaveDir_）；+ `TargetGuidForTest`。
- `Editor/App/EditorApp.cpp`：`OpenAnimationEditor` 裸分支清集态（断路修复）；
  删 `OpenAnimationCreateClip`；smoke 链 f107–f117（f107 现场播种 walksrc
  三图——不随项目播种，选择器目录瓦片会挤占 tile 序破坏 f50 链前提 → f108
  开框 → f110/111 真实点创建 → f114 断言落盘/切换/3 帧文件名序 → f116 开集
  → f117 "双击"裸 clip 断言集态清零+目标切换）；RESULT 增
  `create(folder/dblclip)` 位。
- `Editor/App/EditorApp.h`：删 `OpenAnimationCreateClip` 声明。
- `Editor/Panels/AssetBrowserPanel.cpp`：文件夹右键删「新建动画集…」；
  空白区右键删「新建动画剪辑…」。

## 验证

- smoke-anim `=> OK`：`create(folder=YES dblclip=YES)`，既有位全保持。
- **阴性验证**：临时移除断路修复（sed 反向，非 checkout——帧序事故批教训）
  → `dblclip=NO` + FAIL + "集态残留"错误行；恢复后全绿。
- 回归 full 14/14。

## 遗留

- 真人验收：文件夹右键 → 极简框观感（保存位置只读行/Enter 创建）；开集状态
  下双击独立 clip 应切到该 clip 编辑。
- 三图报告余三点不变（胶片带序号+拖拽换序 / 列表模式缩略图 / 大预览 scrub）。
