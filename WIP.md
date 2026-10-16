# WIP（冷启动文件——永远只记当前态）

- **停在哪**：批⑪ 已提交（`78f8d2c`，七轮实测修 + anytest 验收场全入库）。**新
  待提交 = 动画状态机操作指南**（用户点名"给游戏开发者写详细操作指南"）：
  `docs/Manual/14-animation.md` 新页（三件套/画布速查/过渡与求值序/Any 节点/
  运行时十条/Inspector 绑定/C# 脚本示例含死亡门/调试/FAQ/红线——示例 API 已对
  SDK 实面核过 Input.Axis/Attack 无虚构）+ GROUPS 工作流组登记 + README 页清单
  + 07-assets 交叉链；`./tools/build-manual.sh` 14 页全绿（无死链/无越子集）。
  下一提交建议 `docs(m8-b11): 动画状态机操作指南——手册 14 页（批⑪ 后补）`。
- **下一步第一个动作**：用户过目指南后点名提交；然后开工批⑫ 帧事件 UI；
  连招窗口（段中百分比 exitTime）已登记 = 真实内容撞上连招需求时立项。
  次选：D 组 #M4 Windows 真机轮 / M9。
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
  smoke-anim 帧预算 134 维持（f38 用了 f37/f40 间空位）。批⑩ 遗留登记不丢：
  面板本地撤销栈与场景结构轨 Undo 不互通（后续统一）；Play 参数实时值浮层 =
  v1.1 触发式。
