# 2026-09-27 M6a 批② T3d：动画控制面——绑定/.controller 状态机/帧事件/资产族定名

## 事件

用户四轮设计讨论（接 T3-UX3 集内落位修复）定方向：多角色共享状态机逻辑、资产族
对齐 Unity、骨骼动画只留边界。[ADR-013](../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md)
（草案随本批转已采纳）+ [批文件](../Plans/M6a/2026-09-27-b2-t3d-anim-controller.md)，
四小批一次落地：

- **批① 绑定**：新组件 `AnimGraph{controllerGuid u64, setGuid u64, inited}`（id 28）
  ——实体↔集/状态机显式绑定；按名解析作用域改绑定优先（`NativeClipByName` 先查
  `AnimGraph.setGuid`，回退 T3c 当前段口径，旧场景零改动）。Inspector 新增两 GUID
  资产槽（`FieldHint::AnimSetRef/ControllerRef` + 下拉/拖入 kind 6/7/右键清空）。
  SDK `Anim.SetSet/SetController`。
- **批② 状态机**：`.controller` 资产（`ControllerEdit` 解析 → `World::Controllers()`
  下标形态）+ 新组件 `AnimParams`（8 槽 f32 参数黑板，全 FIELD_RT，id 29）+ 新系统
  `AnimGraphSystem`（**#16，插 CSharpBatch 后**——读当 tick 脚本参数、写段由下一
  tick #13 消费，与脚本直写 Play 同拍；不消费 RNG 不占子流）。当前状态由 clipId 经
  集绑定反查（`ClipTable::NameOfClip`，同集同 clip 换名去重保确定性）；trigger 评估
  命中即清；exitTime = 非 loop 段收尾边沿；缺绑状态 warn-once 保持（ADR-013 D4）。
  vtable 表尾追加 `animParamSlot`；SDK `Anim.SetParam/GetParam/Trigger`。
- **批③ 帧事件**：`.anim` 扩 `events[]: [{frame,id}]`（`ClipDef.events`）；
  `Animator2D` 借 `_pad` 首字节加 `ended`（FIELD_RT，28B 布局逐位不动）——非 loop
  段收尾 0→1 边沿发 `GameEvent::AnimFinished`（补 IsPlaying 判不了播完的缺口）；
  帧跨越发 `GameEvent::AnimFrame`（user=id, userArg=clipId, payload[0]=帧号）。两
  枚举值表尾追加在 Custom 之后（用户区起点零位移）。
- **批④ 资产族定名**：`.clip→.anim`、`.ani→.override`（ADR-013 D2，兑现
  2026-09-19"扩展名与 Unity 一致"纪律）。保 GUID 迁移（meta 同行 rename，.meta
  type 字符串不动零迁移）；存量：Templates/vs-survivor、demo/svr-test、Samples
  yami 五件 + manifest 路径；全库扩展名字面量清扫（`.clipId/.clipGuid` 标识符
  后顾断言排除）。

## 终验：ani.scene 双怪对决

`demo/svr-test`：`Assets/Animations/{Monster01,Monster02}/`（8 个 .anim + 2 个
.override）+ `Assets/Controllers/Duelist.controller`（Idle/Walk/Attack/Hit/Death
五状态 13 过渡）+ `Scenes/ani.scene` + `Game/DuelBehaviour.cs`。

- **同一份 controller 驱动词表不一致的两角色**（ADR-013 D4 活样）：Monster01 只绑
  Idle/Walk/Attack（受击/死亡缺绑 = warn-once 原地），Monster02 五状态全绑（Hurt
  打断 + Dying 终态）。
- 脚本零 GUID 魔法常量：`SetParam(speed)` 条件边、`Trigger(attack)` 攻击、帧事件
  （Attack 第 7 帧打点 id=1 → 出伤）判定帧即表现帧、`Trigger(hit/death)` 对手反应。
- 无头验收：`--scene ani.scene --play --frames 1500 --no-reopen` exit 0；日志
  3/3 + 5/5 段登记、`viewportVisible=2`、errors=0；胜者落存档键 `duel.winner`。
  （`--smoke` 的 script-spawn/overlay 子断言为刷怪场/选区播种场景口径，对决场 N/A
  ——Main.scene 同命令 overlay 也 FAIL，非本批回归项。）

## 实测

| 项 | 结果 |
|---|---|
| engine-tests（+TestControllerAndGraph：解析/roundtrip/坏档六拒/条件评估/NameOfClip/同集去名/事件表） | ✔（计数 28→30、系统 17→18 同步） |
| script-tests（+TestAnimGraphProbe typeId 14：SetParam 条件边/Trigger 消费即清/exitTime 回归/AnimFrame/AnimFinished/GetParam 回读） | ✔（behaviours 14→15） |
| smoke-anim 扩 graph 链（播种 controller + whole→walk→hit→whole 三段切换） | ✔ `graph(rt/cache/switch) => OK` |
| 金回放 | **三档重录**（加 2 组件 → 组件名无条件入哈希流，StateHash.cpp:75；M5 批② WaveDirector 同款先例）后复验 PASS |
| ctest 3/3 + editor-regression full 14 步 | ✔ **PASS=14 FAIL=0**（2026-09-27 全绿） |
| ani.scene 无头对决 | ✔ exit 0 / duel.winner 落盘 |

## 附带解堵（M6c 前过渡容量）

- `kMaxTextureSlots` 64→**256**（RHI.h；描述符数组/池同源缩放）：双怪 111 张帧图
  超 M4 最小集；M6c 图集打包落地后回落评估。
- ImGui 后端 `DescriptorPoolSize` 64→**256**：缩略图逐资产 AddTexture，同批撞
  OUT_OF_POOL_MEMORY（-1000069000）。

## 热修（同日，用户实测报告）

**症状**：双击 .override 空态（提示"双击 Assets 中的动画集或动画剪辑打开"）；
先点某集内 .anim（归并路径）后，双击任何 .override 显示的都是那个集的历史内容；
新建集后观察到的"旧内容"同源（新建成功后双击新 .override 踩同一坑）。

**根因**：`EditorApp::OpenAnimationEditor` 对 AnimSet 双击走 `SetTarget`（只设
`targetGuid_`），而面板集模式由 `setGuid_` 驱动、剪辑模式要求 target 是 Clip——
指向集资产的 targetGuid_ 两头不满足 → 空态分支；面板保留上次归并装载的集。T3c
注释宣称的"面板侧 OpenSet 语义"从未实现（T3c 验收走的是 clip 归并路径，直开
集路径无程序化覆盖）。

**修复**：路由处加 AnimSet 直接分支 `OpenSet(guid) + SelectSegment(0)`；smoke-anim
补防回归断言（`SetGuidForTest()` 钩子 + RESULT `set(open=)` 位——断言必须先于
clip 归并调用，否则归并已置同值会假通过）。quick 回归 6 步 + smoke-anim 全绿
（smoke-ui 首跑 scrub=8° 判定 FAIL 为已知注入时序边际，复跑 3/3 OK，与本修无关）。

### 热修②（同日，用户复测报告：右键"新建动画集…"弹窗仍显历史集）

**根因**：`OpenAnimationCreateSet` 只置面板 `open` + 开新建弹窗，不清编辑态——
AnimationPanel 常驻 PanelRegistry，`setGuid_` 残留 = 弹窗背后照渲染上一个集。
与热修①同族（历史状态残留），但入口不同：①是"打开"路由错置，②是"新建"入口
不清场。三个创建入口（新建动画集 / 从文件夹建剪辑 / 空白建剪辑）同病。

**修复**：`AnimationPanel::ResetEditingState()`（清 setGuid_/setLoadedGuid_/
segGuid_/targetGuid_，清后走空态提示行；创建成功路径由 TryCreateClip(SetTarget)/
OpenSet 重新置位）在三个 Start* 入口统一调用。smoke-anim 断言位排布沿用①的
教训：清态断言必须放在归并重开**之前**（归并会重置 setGuid_，后置假通过），
归并调用照旧保住面板本体 OnGui 覆盖；RESULT 增 `set(create=)` 位。模态不点
创建 = 零落盘副作用。smoke-anim `create=YES` + quick 6/6 PASS（smoke-ui 首跑即过）。

### 热修③（同日晚，用户复测：右键新建动画集后空面板、无任何入口）

**症状**：热修②后用户实测——创建后面板提示"双击 Assets 中的动画集…打开"，
无新增动画入口。

**定位（smoke-anim TestHooks 注入真实模态点击逐帧探针）**：`queued=ON @f3 →
OFF @f4`——右键"新建动画集"在面板窗口**关闭**状态触发时，开窗（首帧浮动）与
模态排队同瞬发生；窗口被停靠布局吸收的换代瞬间，ImGui 杀掉其所属弹窗；下一帧
`BeginPopupModal` 失败，旧代码"外力关→复位弃单"把误杀当用户关闭 → 模态静默
消失 → 空面板无入口。窗口稳定后（f30 重排队 → f40 真实点击）创建全链
`flow=YES`——**TryCreateSet/Rescan/FindByPath/OpenSet 链路本身健康**。

**修复**（两处 + 一防御）：
1. `DrawSetModals` 外力关分支改为**重排队**（`setCreatePending_ = true`）而非
   复位弃单——用户侧关闭（创建/取消/Escape）都先行清 `setCreateOpen_`，落到
   该分支的失败必是外力，重开无死循环风险；向导 `DrawWizard` 同款（原为静默
   return = 卡死态）。
2. `TryCreateSet` 防御：落盘成功但 `FindByPath` 未命中 → 红字留模态报内部
   不一致（旧版静默跳过 OpenSet 仍返回 true——同症状的另一条潜在静默路）。
3. smoke-anim 增 `set(flow=)` 位：TestHooks 扩到 smoke-anim 会话 + 「创建」
   按钮矩形登记 + hold/release 隔帧注入——播种期排队必须**自愈存活**到 f40
   真实点击并断言 setGuid_ 切到新集（Assets/player.override）。

验证：探针 `queued=ON @f3` 后无 OFF（自愈生效）；smoke-anim
`set(rt/cache/open/create/flow 全 YES) => OK`；quick 6/6 PASS。

**遗留待用户确认**：真人会话重测右键新建动画集（面板关闭态触发）应见弹窗；
若弹窗出现过且点创建后仍空态，需看模态内红字（防御位②会拦内部不一致）。

## 教训

- **"面板侧 XX 语义"式注释是债**：T3c 把 .override 直开集的责任写在注释里而非
  代码里，验收又只覆盖了归并路径 → 路由半成品潜伏一天即被真人踩中。防回归断言
  必须钉在被宣称的行为上（且警惕与前置调用共享状态的假通过——本例断言若放在
  归并调用之后永远绿）。
- **手写资产 GUID 必须保证低 32 位全局唯一**：clipId/setId = GUID 低 32 位截断
  映射——本批自踩 `aa01.../aa02...` 低位同尾（两集都成 0x10、M02 只登记 2/5 段）。
  换 `0x101..0x110 / 0x201..0x210` 唯一低位后 3/3 + 5/5。与 ClipTable.h:26 既有
  碰撞告警同源，手写资产侧同样适用。
- **manifest carry 优先于 meta guid**：改写 .meta guid 后旧 manifest 仍把资产绑回
  旧 GUID（集引用 0 段命中）。改 GUID 的正确姿势 = meta + 引用方同改后**删
  .lemon/manifest.json 强制重建**（meta 随文件走才生效）。
- 加组件必重录金档（组件名入哈希流）——"零重录"论证只适用于零实例组件的**字段**
  （ended/FIELD_RT），不适用于**新组件本身**。口径写进 09 §6.8 语义的自然推论。
