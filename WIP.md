# WIP（冷启动文件——永远只记当前态）

- **停在哪**：三笔提交完成（2026-10-17）——`7d21d1c` docs(m8-b11) 动画状态机
  操作指南（用户点名的手册 14 页）、`5de13e0` feat(m8-b12) 帧事件 UI（review
  两处小修后绿点提交）、批⑬ 帧事件注册表立项 docs。批⑫ 真人走查**未做**
  （开编辑器双击 svr-test Monster01/Attack.clip 看角标/chips/打点/Space 提亮；
  组合验收归批⑭）。
- **下一步第一个动作**：开工**批⑬ 帧事件注册表**（批文件已备
  `docs/Plans/M8/2026-10-17-b13-anim-event-registry.md`：注册表 tab + 编辑器
  下拉 + Anim.OnFrame 启动期校验 + lint 三件，三段分期 ⑬a/b/c；原⑬ 样本+
  手册+模板已顺延为批⑭）；连招窗口（段中百分比 exitTime）已登记 = 真实内容
  撞上连招需求时立项。次选：D 组 #M4 Windows 真机轮 / M9。
- **踩到一半的坑**：**demo 行为类必须 GameMain.Configure() 显式
  Behaviours.Register<T>()**（无自动扫描——场景 scripts[] 挂了类但没注册 =
  "Play 装配：脚本类型未注册（跳过）"红字静默无脚本，anytest 首跑踩中）；
  `--scene` 路径按 **cwd** 解析（非 project 相对）；`--play` 须与 `--smoke` 同挂
  才进 Play（EditorApp.cpp:379 `playTest && smoke`）。
  **共享容器进保留键时，所有"状态节点语义"遍历点必须清单化
  排除**——五轮把 `"*"` 塞进 `edit_.layout` 后 `hitNodeLi` 没跳过，`StateIndex
  ("*") = -1` 让 Any 首拖落键后点击/再拖/右键全静默失效（六轮实测报障；命中/
  把手扫描/FitCanvas 三处补跳过，绘制环 `si < 0 continue` 天然免疫）。
  **smoke-anim 必须按回归脚本口径在临时自播种目录跑**
  （`--project "$(mktemp -d)/anim" --frames 134 --no-reopen`）——指向
  `demo/svr-test` 会因根图 24 张 vs 断言 11 张误红，且自清段会删改工程
  `player.ani`（五轮实测踩坑，已 git 还原；demo 里 pick0-8/anim-*.clip 等
  smoke 残留 = 历次误跑产物，惰性无害）。i18n json 手编勿用 json.dump 整写——
  原文件空行分组会被抹掉（diff 脏），改用精准 Edit 插行（批⑪ 实测回滚重来）。
  `edgeDragFrom_`/`dragNode_` 各有 -2 哨兵（any 源/any 体拖动）——判"无手势/
  无拖动"必须 `== -1`，勿用 `>= 0`/`< 0`。BuiltInPanels.h 头文件**不引 imgui.h**
  （ImVec* 只在 .cpp 用——五轮加 AnyRectGraph 时踩中，出参 float 版收编）。
  smoke-anim 帧预算 134 维持（**f38/f39 均已占用——f37..f40 区间现无空位**，
  批⑬ 新链需抬预算，b10 review-fixes 先例同步回归脚本两处）。批⑩ 遗留登记
  不丢：面板本地撤销栈与场景结构轨 Undo 不互通（后续统一，含批⑫ 事件操作）；
  Play 参数实时值浮层 = v1.1 触发式。
  批⑫ 实录：Edit 大段替换测试文件时 **#ifdef/#endif 配对要逐个数**（内层多余
  包裹吃掉外层闭合 = "unterminated conditional"绕弯排查）；RemapClipEvents
  落尾必须 stable_sort——换位/旋转类映射会失序（升序不变式进函数本体，
  别留给调用点）；**分批提交时混合文件按批时点临时还原/回填**（本次三笔：
  指南 → b12 → b13 立项，逐字串往复，提交后跑 build-manual/对齐检查复核）。
