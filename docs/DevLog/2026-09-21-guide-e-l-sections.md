# 2026-09-21 手测指南 E–L 段：打开场景误退编辑器 + 资产右键目标 + Play 切页 + Console 复制

E–L 段手测（结论在指南"人工测试结论"列）后的修复批：

**打开/新建场景、切项目 = 编辑器直接退出（用户实测 L 类流程抓到）**：脏确认模态
自 M4.2 起被三处场景操作复用，但按钮"保存并退出/丢弃并退出"硬编码 `forceExit_`，
`confirmContext_` 记了没人读——注释里"续操作 M4.2 补齐闭环"从未补。修法：模态按
上下文分流（Exit = 原语义；SceneOp = "保存/丢弃/取消" + `PendingSceneOp` 挂起操作，
确认后直通选择器/新场景），退出裁决处显式重置 Exit 上下文防残留。多场景 tab
（Godot 式）不在本批——设计讨论见指南 §3，倾向 v1.1 再评估。

**G4–G6 资产右键"无菜单"**：上批 NoOpenOverItems 修复只做了一半——条目菜单
`asset_ctx` 绑在 12 字符文件名上（`BeginPopupContextItem` 绑 last item），右键
72×72 缩略图既不弹条目菜单（TextWrapped 未悬停）也不弹空白菜单（已被
NoOpenOverItems 抑制）= 什么都不出。修：asset_ctx 上移绑缩略图。Hierarchy 的
node_ctx 绑整行 TreeNode 无此问题。

**F1 Play 不自动切 Game 页**：Scene/Game 同区 tab（默认布局 DockBuilder 同节点），
Play 进出补 `SetWindowFocus`（+1/-1 经 `tabFocusPending_` 在 BuildUI 帧内消费——
EnterPlay 调用点有的在 NewFrame 外，直接 Set 会断言）。Unity 心智。

**Console 右键复制（J 段建议）**：`console_ctx` 空白菜单 = "复制全部（按过滤）" +
"复制最近一条"（无行选择机制，贴报错场景够用）。

**未修待复验/设计项**：G7 双击建实体"不选中"（代码建完即 Select，需用户指明
症状）；H1"没有新建脚本"（入口在 Assets 菜单且有前置条件，疑找错菜单）；E2 组
旋转其实已支持（旋转工具 E 拖本体 = 绕主选中心公转，`dragTfs_` 全选择集），缺的
是 Select 工具下多选组操作（登记讨论）；smoke-drag 存在**已知 flaky**（cwd 共享
`.lemon/editor/imgui.ini` 状态相关误报，本批回归期间复现两次、重跑即过——第九轮
"ini 漂移误报"同源，根治进 M4.8）。

## 回归

`editor-regression.sh full` **11/11 PASS**（smoke-drag 抖动重跑后稳定 3/3）。
