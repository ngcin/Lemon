# 2026-10-16 M8 批⑨ —— .controller 状态机工作台（动画创作面打磨 A/3）

## 输入

用户："Lemon 里面的动画工作流程梳理一下，目前还不能很顺利的使用起来。" 梳理结论
（同日会话）：运行时链路闭环完整（解析→Play 缓存→求值→写回→C# 门面→事件，单测/
script-test/smoke 三层覆盖），痛点在创作面——**.controller 没有任何编辑器 UI**
（全 Editor 无 `ControllerToJson` 调用，`Duelist.controller` 12 条 transitions 全
手写），错误要到进 Play 才炸（快照语义当局还不生效）。用户拍板按推荐三批推进：
**批⑨ controller 工作台（本条）→ 批⑩ 帧事件 UI → 批⑪ 样本+手册+模板**；
不推翻 ADR-013（其 v1 负面项自登记"无可视化图编辑"——本批补的正是 v1 该有的
编辑面，图编辑器维持"游戏阻塞触发"挂起）。

## 交付

- **Engine（+纯函数，运行时零改动）**：`ControllerStructuralWarnings`
  （`Engine/Assets/ControllerAsset.*`）——entry 出发 BFS 可达性 lint，不可达状态
  逐个点名；解析管"引用悬空"、lint 管"逻辑死状态"，告警不拦解析。
- **Editor**：`AnimControllerPanel`（`Editor/Panels/AnimControllerPanel.cpp` 新建，
  ~700 行）——三张表：参数表（name/kind/def，≤8 拦，改名/删参引用传播）+ 状态
  词表（inline 改名双击进入、entry 徽标、删状态清相触及边）+ 选中状态出边表
  （to 下拉/exitTime 勾选/条件行按参数 kind 收窄算子域：float 五算子、bool ==、
  trigger 无值）+ 入边只读行。
  - **编辑期校验三件（本批灵魂）**：结构 lint 每帧直算（引擎同源）；
    exitTime×Loop 跨资产黄字（扫项目全部 .ani 集同名段绑定 clip，非 Once →
    "段末边沿永不触发"点名——`Systems.cpp` exitTime 只认非 loop 收尾的登记边角
    用编辑面堵，运行时不改；clip 解析 (guid,hash) 缓存 + 结构变更拍才重算）；
    保存门禁 = `ParseControllerJson` 判据（悬空引用/空条件/超 8 参数/重名）+
    roundtrip 预验（序列化器缺陷不覆写原档，AnimationPanel 同款防线）。
  - **接线**：双击 .controller 进工作台（`OpenAnimControllerEditor` + EnsureLoaded
    急切装载——无头路径不开窗也持编辑态）；AssetBrowser 空白区右键"新建动画状态
    机…"（起步档单状态 Idle，落点默认 `Assets/Controllers/` = ADR-013 D3）；
    Play 态只读横幅（快照语义）。
  - **浮动默认（不进 DockBuilder）**：预挂 bottomId 与 Animation 抢同一标签组
    ——真人侧"编辑 clip 双击 .controller 把胶片带压成后台标签"互踩 + smoke 复跑
    ini 恢复"活动 = AnimController" → Animation Begin false → 注入链全断（首轮
    实测定位）。Audio Mixer 工具窗先例；三表表单对窗形不挑。
- **i18n**：双语 ~32 keys（animctrl.*）+ panel.animcontroller + assets.ctx.
  new_controller；顺手修 `panel.clipation` 死 key（AnimationPanel 标题 key 双语
  表均无——替换事故，标题一直显示原文 key）→ `panel.animation`。
- **测试**：EditorTests `TestControllerAndGraph` 扩结构 lint 五断言（全可达零
  告警/孤岛点名/补入边清/换 entry 旧入口不可达/坏档不炸）；--smoke-anim 扩四链
  `ctrl(open/load/save/gate)`（路由+急切装载 → 孤岛 lint → 语义直调出边编辑+
  保存+盘上 roundtrip（4 状态 5 过渡含双 exitTime）→ 跨资产黄字 + 悬空 to 门禁
  拒后恢复）+ 渲染面探针（animctrl.begin/skip 双键——Begin false = 被标签遮住
  也证 OnGui 拉起；**f130 探针毕即关窗**：开窗态被 ini 持久化，复跑该窗浮在
  FilePicker 落位上方吃点击——复跑实测定位）。同项目复跑绿 = 口径外场景
  （HEAD 同场全红取证，非本批回归）。

## 验证

- 构建 Release 零警告；单测 **34,937 checks OK**（+5 lint +2 wizard 目录项）；
  ctest 4/4；金回放零重录（运行时零改动）；vtable 59·组件 id 33·系统 21 不变。
- **--smoke-anim**：回归脚本同口径（fresh 项目 + retry 取优）zh/en 双档 PASS；
  ctrl 四链在全部轮次（含同项目复跑）零抖动；既有 pick/folder 注入偶发红 =
  官方登记抖动族（09 §9，HEAD 同场再现定性正交）。
- **顺手修齐存量红**（3d021e0 布局归一 Game/→Assets/Scripts/ 未随迁测试，
  HEAD 上 engine-tests 直接 abort）：TestProjectWizard 三处断言
  （wizard dir missing: Game / SpawnerBehaviour.cs references spawn guid /
  template declares class）——本批前 HEAD 单测不可绿，批内闭环。

## 裁决与登记

- 编辑期校验替代运行时改动（exitTime×Loop 不动求值语义，金回放零重录）；
- AnimController 浮动默认 + smoke f130 关窗（见上，教训落 `BuiltInPanels.h` 注
  与批文件 §4）；
- 帧事件 UI 归批⑩；样本/手册端到端路径 + vs-survivor 去 GUID 化归批⑪（文档
  随本批已改 05 §3/§7、06 §2.2、Manual 07 产出方口径）；
- 真人走查归批⑪ 组合验收。

[批文件](../Plans/M8/2026-10-16-b9-anim-controller-workbench.md)
