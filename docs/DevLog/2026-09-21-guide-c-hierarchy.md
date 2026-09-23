# 2026-09-21 手测指南 C 段（Hierarchy）：右键菜单被空白菜单劫持 + 五件杂项

按 Editor-Manual-Test-Guide §2-C 走查，结论记录在指南"测试结论"列。七项中
六项为真缺陷/缺口，一项（C4）维持现状。

**C5/C6（右键"摘根/删除"看不见）——本批主菜，机制型 bug**：ImGui 1.92.9b 的
`BeginPopupContextWindow` 默认**不排除悬停在条目上**（须显式传
`ImGuiPopupFlags_NoOpenOverItems`）。Hierarchy 右键行时 `node_ctx`（条目菜单）
与 `hierarchy_bg`（空白菜单）同帧双触发，后开者把前者关掉——行右键永远只见
"创建空实体/创建精灵"两项，摘根/删除/重命名/Prefab 化四个条目菜单全部不可达。
AssetBrowser 同款写法同病（右键资产只见"导入"）。**定位方式**：用内置 imgui
源码写了个无渲染最小复现（/tmp 一次性；进程内注入鼠标事件，右键 TreeNode 行，
打印 OpenPopupStack）——不带 flag：`node_ctx=0 hierarchy_bg=1`；带 flag 反转。
**修复**：两处 `BeginPopupContextWindow` 补 `NoOpenOverItems`。

**C1（创建三入口 Undo 不对称）**："+ 创建"弹窗有快照，GameObject 菜单/空区
右键漏推（创建后 Ctrl+Z 报"栈空"）；右键"复制"同病。四处补齐结构轨快照。

**C2（点空白不清选）**：tree child 内补"干净左击清选"（`MouseDragMaxDistance
<3px` 防拖拽挂接的释放误触）。

**C3（搜索不忽略大小写）**：Hierarchy `PassFilter` 与 AssetBrowser 过滤器改
ASCII 不区分大小写子串匹配（`StrIStr`，非 ASCII 字节段原样比较）。

**C8（Ctrl+D 父子链只得根）**：不是设计——`DuplicateEntity` 此前只平移单实体
组件表。改走 `SaveEntityTree/LoadEntityTree`（与复制粘贴同链：子树全量/
EntityRef 子树内重映射/guid 全换新），副本原位对齐 Unity；Ctrl+D 从"只取
Primary"改为"选中子树的根"（同 CopySelection 祖先过滤）。注意迭代
`Selection()` 时先收集根再复制——迭代中 `Select()` 改容器 = 迭代器失效。

**C4（双击父节点=折叠，无法重命名）**：维持现状——F2/右键可达（右键修复后
真正可达），双击折叠是 ImGui 树惯例；Godot 式"双击标签重命名"与 Unity 心智
不符，不跟。

smoke-ui 断言随 C8 调整：dup/dupUndo/dupRedo 计数从 `+1` 改 `+子树大小`
（种子 Player 带 3 Mob，`SubtreeSizeOf` 实测）。

## 回归

`editor-regression.sh full` **11/11 PASS**（含调整后的 smoke-ui 21 断言）。

## 补修：node_ctx 弹窗 ID 全节点共享 → 菜单按实体数重复 + conflicting ID 报错框

C5/C6 修复后用户实测暴露（此前 node_ctx 永远被空白菜单关掉，从未真正显示，
共享 ID 的雷一直埋着）：DrawNode 无 PushID，所有节点的
`BeginPopupContextItem("node_ctx")` 解析到**同一个弹窗 ID**——右键任一行后，
每个节点的调用点都返回 true、向同一弹窗重复提交菜单内容（5 实体 = 5 份菜单
叠显 + 同名条目 ID 冲突报错框）。无渲染复现：3 节点共享 ID = 3 个调用点全
true；按 `PushID(e.id)` 隔离后 = 1。Inspector（字段级 PushID(f.name) + 组件级
PushID(meta.name)）与 AssetBrowser（DrawItem 顶层 PushID(e.guid)）同位点均有
隔离，Hierarchy 此前是唯一裸奔点。回归 11/11。
