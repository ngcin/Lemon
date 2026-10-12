# 修复：资源浏览器文件夹改单击选中 / 双击进入（对齐系统文件管理器语义）

- 日期:2026-10-13（用户走查实报；c303811 后同会话第三项）
- 性质:用户报障修复，非批次项。引擎内核零改动（AssetBrowserPanel.cpp +
  BuiltInPanels.h + EditorAppSmoke.cpp 三文件）。

## 1. 实报（用户原话口径）

assets 下的目录现在是单击进入到文件夹，不符合常规，单击选中，双击才是
进入到子文件夹。

## 2. 修法

- **面板**（AssetBrowserPanel 文件夹单元格）：单击 = `selectedDir_ = d`
  选中高亮（ImageButton 底色 kAccentDim，同条目选中色）；双击 = 进入
  （`currentDir_ = d`）。双击 = 按下帧检测（`IsMouseDoubleClicked` 同帧
  语义，else-if 防第二击重复选中）——与条目/选择器瓦片同拍。进入/选中
  互斥清对侧选集（`selectedDir_` ↔ `selectedGuid_`，条目单击清文件夹选中、
  文件夹单击清条目选中——表格区随 selectedGuid_=0 收起，语义正确）。
  新成员 `selectedDir_`（BuiltInPanels.h）。
- **smoke-ui 驱动**（EditorAppSmoke.cpp §I）：原 f112/113 单击注入 +
  f115 断言进入——语义改后必红。改为 f112-115 两对按下/释放（间隔 2 帧
  ≈33ms < MouseDoubleClickTime 0.30s 判双击），断言移到空闲帧 f116；
  面包屑段（f117+）不动。

## 3. 验证

- Release 零警告。
- 按用户口径未跑回归；smoke-ui 驱动改动的机器验证留待下次回归（注入时序
  与 picker 瓦片已验证的双击模式同构：两次按下间隔 2 帧）。

## 4. 备注

M4.7d 原设计"单击进入"注释与实现同步退役（注释块更新）。`assets.folder.*`
stash 键与 rect 不变（smoke-ui 点击坐标仍取标签矩形中心）。
