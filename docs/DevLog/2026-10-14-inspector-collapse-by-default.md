# Inspector 组件默认折叠三件套（用户实报优化）

- 日期：2026-10-14（用户实报："所有组件默认展开占太多空间，建议默认不展开"；方案讨论后拍板"方案 1 三件套"）
- 结论先行：**默认只展开 Transform2D（高频首行），其余组件与脚本段默认折叠；组件头右键菜单新增"全部折叠/全部展开"（Unity 同款）；新增组件/挂脚本后自动展开可直接配置。折叠态沿 ImGui 既有语义按组件类型粘滞（storage 按 header ID 记忆，跨实体选择保持）。**

## 1. 设计要点（讨论定案）

| 项 | 决策 | 依据 |
|---|---|---|
| 默认态 | 仅 Transform2D 展开 | 全折叠（用户原案）每次选中都需点开高频的 Transform；Unity/Godot 全展开默认正是空间问题的来源 |
| 折叠记忆 | 按组件类型（ImGui storage，现状零改动） | 每实体记忆需自定义存储（guid+comp id 键），复杂度不值；Unity 亦类型级 |
| 批量操作 | 组件头右键"全部折叠/全部展开" | Unity parity；作用于当前实体全部头（含脚本段） |
| 新增组件 | 自动展开（一次性，压过既有折叠记忆） | 挂完直接配参（Unity 行为）；DefaultOpen 压不过已存折叠态，故走存储直写 |
| 顺手修复 | 右键菜单改绑到 `if(expanded)` 之外 | 原实现在展开体内 = 折叠头右键无菜单的潜伏不可达；默认折叠后成为主路径，必须修 |

## 2. 实现（引擎内核零改动）

- `InspectorPanel.cpp` DrawComponent：`headerFlags` 仅 Transform2D 带 `DefaultOpen`；`setAllOpen_`（-1/0/1）与 `autoExpandCompId_` 在 `CollapsingHeader` 前直写 `GetStateStorage()->SetInt(GetID(header), …)`；右键菜单（重置组件 + 分隔线 + 全折/全展）移到展开体外绑定。
- OnGui：脚本段头同样默认折叠 + 同款存储直写（`autoExpandScript_`）；`setAllOpen_` 在组件循环 + 脚本段后消费；AttachScript / 添加组件回调置自动展开标志（弹窗在遍历后渲染，标志次帧生效，一帧延迟不可见）。
- `BuiltInPanels.h`：三个私有成员（setAllOpen_ / autoExpandCompId_ / autoExpandScript_）。
- i18n：`insp.collapse_all` / `insp.expand_all` 中英各 +2 key（621→623 条）。

## 3. 验证

- 构建零警告。
- smoke-drag OK（视口路径与 Inspector 无交叠）。
- **smoke-ui 全 15 项 OK**——关键 = D 段 label-scrub 拖 `Transform2D.rot`（14.0deg 精确命中 + Ctrl+Z 往返 + 置脏存盘链），Transform2D 保留默认展开故字段可见性不变。
- anim-chain zh 全段 OK。
- 真人走查待用户（默认态观感 + 右键全折展 + 新增自动展开）。
