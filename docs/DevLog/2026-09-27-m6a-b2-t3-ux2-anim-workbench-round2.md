# 2026-09-27 M6a 批② T3-UX2：动画工作台 v3.1 修正批（新建入口归浏览器/加帧四通道/从精灵表两段式/框选拖动修复/布局与命名）

## 事件

T3-UX 交付后用户实测第二轮反馈（十项，批文件
[2026-09-27-b2-t3-ux2-anim-workbench-round2.md](../Plans/M6a/2026-09-27-b2-t3-ux2-anim-workbench-round2.md)）：

- **入口归位**：新建动画集/动画剪辑入口 = AssetBrowser 文件列表**空白处右键**
  （与"导入文件…"平级）；面板删目标行与新建按钮 = 纯编辑器（打开走双击资产）。
- **加帧四通道**：「+ 添加帧 ▾」= 空帧 / 从精灵表…（文件选择 → 选帧对话框
  两段式）/ 从图片文件…（FilePicker 多选整图入帧）/ 从动画剪辑 (.clip) 复制…；
  拖 .clip 资产入预览/带尾 = 复制帧表。
- **从精灵表对话框重做**（Godot Select Frames 对齐）：分割参数（按块数/按像素）
  与大图预览**同屏**、网格实时重绘（修复旧版"先配切片无预览"）；**添加时**才
  写 .meta（SetGridSlice + Rescan 连号块分配 + guid 重查）；图上叠 InvisibleButton
  命中层修复"拖框选变成拖走对话框"（ImGui Image 非交互项，裸图拖动 = 移窗）；
  "关闭"→「取消」；全选/清空/点选序/缩放保留。
- **布局修复**："加帧后图跑到下方、右侧空白"根因 = 大预览 child 在矮面板下高度
  被压到近零。改为：预览紧凑一行（传输+帧图+信息+状态）→ 工具条 → **帧网格
  主体**（flex 吃满剩余，wrap 纵向滚动）→ 属性行。
- **左列图标化**：IconKind 尾加 Add/Duplicate/Delete/Rename/Search 五枚（形状页
  程序化绘制），工具条 IconButton 化。
- **命名统一**（Unity 术语面）：段→动画（动画集/动画/帧三级）、新建动画→新建
  动画剪辑、从精灵表加帧→从精灵表添加帧；数据 schema 字段不动。
- **FilePicker 扩展**：OpenMulti 多选模式 + 扩展名白名单（Open 重置多选态，
  场景 IO 既有调用零影响）。

## 实测

| 项 | 结果 |
|---|---|
| cmake --build --preset mac | 通过 |
| ctest | 3/3 |
| --smoke-anim（无头 120 帧面板 OnGui） | errors=0，rt/cache/whole/set `=> OK` |
| tools/editor-regression.sh full | 14/14 PASS |

## 教训

- ImGui 裸 Image 不拦截鼠标拖动——需要框选/点击交互的图像必须叠 InvisibleButton
  命中层，否则拖动落到窗口移动（模态框被拖走）。
- "预览吃满剩余空间"的布局对矮面板（底部停靠）脆弱：flex 方向给内容区（帧
  网格）而不是给装饰区（预览），辅助信息做固定矮条——Godot 同款取舍。

## 余项

真人验收清单五条（批文件 §5）；auto-slice（透明边界自动分割）与 AssetBrowser
多选拖批量加帧遗留候补。
