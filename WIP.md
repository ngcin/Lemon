# WIP（冷启动文件——永远只记当前态）

- **停在哪**：动画创作面打磨系列——批⑨ 已提交（`8ff48d1`）；批⑩ 精简可视化图**机器面全过（⑩a+⑩b 同日，未提交）**：画布（拖节点/拖线建边/边选中/右键菜单/双击改名/Play 高亮/不可达灰化）+ 选择驱动检查器（节点/边/参数表）+ `SnapshotStack` 面板本地撤销重做（Ctrl+Z/Shift+Z/Y + Del 聚焦内优先，`CapturesUndoKeys` 让位全局）+ 布局 `editor.nodes` 持久化（⑩a）；证据 = 构建零警告/单测 34,959（+22）/ctest 4/4/smoke-anim zh+en `ctrl(open load save gate layout edge)` PASS（f37 边链 + f129 canvas/insp.node 双探针）/回归 full 25+1（bench 热态单尖刺 76→隔离复跑 78 PASS；anim zh 回归内一次注入抖动→隔离双绿）/金回放零重录（⑩b 零 Engine 运行时文件，⑩a 跨版本证据沿用）；文档已收（DevLog/05-Editor/06 §2.2/批文件 Status/M8.md 行）；moreal 回退、**Lemon 唯一主线**。
- **下一步第一个动作**：向用户报批⑩ 收口 + 定提交口径（⑩a/⑩b 单提交或分两三提交——工作树 12 文件 +1 新头文件）；真人 15 分钟观感过目（打开 Duelist.controller → 拖节点/连线/改条件 → 保存 → Play 高亮；归批⑫ 组合验收前置）。之后开工批⑪ 帧事件 UI。次选：D 组 #M4 Windows 真机轮 / M9；B 档基建（分级门禁 L0/L1 + bisect 包装）待排期。
- **踩到一半的坑**：无（⑩b 遗留登记：面板本地撤销栈与场景结构轨 Undo 不互通——后续统一；Play 参数实时值浮层 = v1.1 触发式）。
