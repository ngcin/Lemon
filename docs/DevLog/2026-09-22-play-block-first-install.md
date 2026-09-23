# 2026-09-22 Play 阻断 + 无宿主自动首装（用户实测 ats 坏档 Probe.cs）

**触发**：用户在 ats/Game/Probe.cs 造了个语法错误，启动期编译失败后 Play 照跑——
"游戏在跑但脚本全没生效"的隐性状态极难排查。决议（用户定）：对齐 Unity/Godot，
启动期编译失败直接阻止进 Play；实体多脚本登记 M5（04 分册 §3 引注 + 08 路线图行）。

**Play 阻断**：`TryEnterPlay()` 统一收口工具栏按钮与 Ctrl+P 两处入口——`!host_ &&
FindGameProject()`（项目带 Game/ 而宿主未装配）→ 弹"脚本未就绪"模态：说明 + 
"重新编译并进入 Play"一键重试 + 取消。无 Game/ 的纯场景会话不受影响（直通）。
程序化冒烟入口（--play/--final）不走守卫——那些项目编译失败自有脚本断言兜底。

**无宿主自动首装（修好不用重启编辑器）**：`TryHotReloadScripts` 原先 `!host_`
直接跳过 = 启动失败后修错保存毫无反应，必须重启。改为：无宿主 + 有 Game/ 工程 =
首装恢复路径（build → `InitScriptHostFrom`，无宿主即全新 Initialize 分支）——
修错保存 → watcher → 编译队列 → 首装成功 → "Play 可用"，阻断自动解除。

**端到端验证**（临时拷贝项目，非 ats 本体）：启动坏档 → "Game/ 编译失败（项目
仍可编辑，无脚本）"；运行中修好 Probe.cs → watcher 0.4s 防抖 → 自动编译装配 →
"脚本宿主就绪（类型 3 个）"+"脚本宿主已装配（源码变更）——Play 可用"。闭环成立。
回归 `editor-regression.sh full` **11/11**（脚本链/终验照常——可编译项目不受影响）。

**多脚本 M5 登记**：当前每实体单 ScriptBox（entt 单实例 + `.scene` 单数 `script` +
Inspector 单段）；M5 落存储多实例 + `scripts[]` schema（读旧兼容）+ Inspector 列表 +
SDK `AddComponent<LemonBehaviour>` 路由；调度 dense 分桶天然支持多实例。
