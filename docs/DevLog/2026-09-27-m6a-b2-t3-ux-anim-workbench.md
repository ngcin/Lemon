# 2026-09-27 M6a 批② T3-UX：动画工作台 v3 交互重设计（Godot SpriteFrames 式主从布局 + 键盘化帧操作 + 拖拽选图）

## 事件

T3c 交付次日用户实测反馈"操作太不方便了"，第四轮收敛（批文件
[2026-09-27-b2-t3-ux-anim-workbench.md](../Plans/M6a/2026-09-27-b2-t3-ux-anim-workbench.md)）：

- **布局重排**：Godot 式 master-detail——左列段清单（190→230px，集名 +
  [新动作/改名/复制/移除] 工具条 + 搜索 + inline 新建输入）+ 右区三层（帧操作/
  播放工具条 · 大预览 fit≤512 居中吃满剩余高 · 胶片带+属性行）。竖向 8 行 → 3 层。
- **建段改流**：`+ 新动作` 大表单（名/fps/循环+内嵌框选、替换右区）→ inline 只
  输入名字（如 idle），Enter 建空段入集并选中，**输入保持开 = 连续建段**；
  fps/循环回右区工具条随手改。段重命名弹窗删除（改 inline，模态 -1）。
- **选图三通道**：拖 Assets 精灵入胶片带格=换图 / 入带尾或预览=加帧 / 入 sheet
  槽=换 sheet；所有 sprite combo 项带 18px 缩略图；从精灵表对话框 v2 =
  全选/清空 + 框选(行优先)/**点选(按点击序 = Godot As Selected)** + Ctrl+滚轮
  缩放 + 动态按钮文案（"追加 N 帧"）。
- **键盘化**：←/→ 移选 · Ctrl+←/→ 换序 · Del/Backspace 删（多选批量）· Ctrl+D
  复制 · Space 播放；Shift+单击范围多选；帧右键菜单插入副本。单击帧即预览跟随
  （取消旧双击跳帧）；播放态胶片带游标高亮。
- **统一保存**：工具条一个「保存」= 段成功后集模式连存集（旧"保存集/保存"分居
  两处易漏存一半）。
- **框架级键仲裁**：`IEditorPanel::CapturesGlobalKeys()` 虚函数（默认 false），
  AnimationPanel 持窗口焦点时声明捕获；EditorApp 实体级 Delete/Ctrl+D 加守卫——
  防面板删帧双触发删掉场景选中实体（全局快捷键原只挡 WantTextInput 无焦点守卫；
  无头 smoke 面板从不聚焦，注入式 smoke-ui 零影响）。

数据面零改动：.clip/.ani schema、ClipEdit、ClipTable、Animator2D 全部不动。

## 实测

| 项 | 结果 |
|---|---|
| cmake --build --preset mac | 通过 |
| ctest | 3/3 |
| --smoke-anim（无头 120 帧，面板每帧 OnGui） | errors=0，rt/cache/whole/set 四链 `=> OK` |
| tools/editor-regression.sh full | 14/14 PASS |

## 教训

- ImGui 1.92.9b 已删 `ImGuiSelectableFlags_SpanAvailWidth`（行为并入默认）——
  升级后按旧记忆写旗标会编译失败，combo 全宽行直接裸 Selectable。
- 用户给的 Godot 截图（docs/animation/）是最高效的对齐介质：两张图直接定版了
  主从布局与 Select Frames 对话框形态，省掉多轮文字描述。

## 余项

真人验收清单四条（连续建段三通道/键盘一轮/窄窗布局/Play 只读，见批文件 §6）；
帧事件 + 段末过渡 = T3d 既定候选；AssetBrowser 多选拖批量加帧候补。
