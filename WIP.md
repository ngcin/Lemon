# WIP（冷启动文件——永远只记当前态）

- **停在哪**：动画创作面打磨系列批⑪（any-state 开禁 + 七轮实测修）**机器面全绿，
  待提交**：`from:"*"` 全局中断边（求值 any 先评、编译期稳定前置）+ Any 节点 +
  B16 自环禁 + B17-1 边界锚点 + 箭头中线化（二轮）+ 四向把手/双向对分离（三轮）+
  直线边定形（四轮：贝塞尔退役，双向对 = 平行双线）+ Any 节点可拖/持久化/
  删除（五轮：图空间位，`editor.nodes` 保留键 `"*"` 惰性落位，右键「删除」=
  清空全局边）+ Any 落键后交互死区急修（六轮：`hitNodeLi` 等遍历面跳过 `"*"`）
  + **Inspector 动画图/动画参数可读性**（七轮：AnimGraph 槽位接通 ED_CTRLREF/
  ED_SETREF 资产槽控件；AnimParams 按控制器词面具名化——编辑态只读默认、
  Play 态实时调 + trigger 激发按钮 = v1.1"参数实时调"最小落地）；证据 = 构建
  零警告/单测 34,971（+12）/ctest 4/4/smoke-anim zh+en `=> OK`/
  回归 full 26/26（bench fps=80，二轮时点）/金回放三档 mismatches=0。
  **运行时验收场 = anytest.scene**
  （单实体 Monster02 全绑定 + AnyStateProbeBehaviour 时间线探针，6/6 PASS 含
  "Attack 段中 hit→Hit any 边中断"核心断言；无头跑法见批文件，交互看 = 开
  Scenes/anytest.scene 进 Play 自动演一遍 ~8s）。ADR-013
  D4 修订 + 06/05/M8.md/DevLog/批文件全落账。上一提交 = `7ccbd12`（b10 review
  修复）。Duelist.controller 现档（8 边 any 拓扑 + 布局）= **用户实测亲手连成
  并保存**（勿动）；anytest 三件（scene/探针 .cs/GameMain.cs 注册行）随批提交。
- **下一步第一个动作**：用户点名后提交 `feat(m8-b11): any-state 开禁——全局中断边 +
  七轮实测修（自环/锚点/箭头/四向把手/直线边/Any 可拖可删/Inspector 具名化）`；
  用户窗口实测 Inspector 新观感（动画图下拉 + 动画参数具名/Play 实时调）。
  之后开工批⑫ 帧事件 UI；连招窗口（段中百分比 exitTime）已登记 = 真实内容
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
  smoke-anim 帧预算 134 维持（f38 用了 f37/f40 间空位）。批⑩ 遗留登记不丢：
  面板本地撤销栈与场景结构轨 Undo 不互通（后续统一）；Play 参数实时值浮层 =
  v1.1 触发式。
