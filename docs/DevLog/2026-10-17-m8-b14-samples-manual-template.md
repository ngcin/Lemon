# 2026-10-17 M8 批⑭ —— 样本 + 手册 + 模板（机器面收口）

## 交付（[批文件](../Plans/M8/2026-10-17-b14-samples-manual-template.md) §2 四面）

- **⑮d Play 态热改横幅**：`assets::IsAnimFamily`（AssetTypes.h，.clip/.ani/
  .controller 单源谓词）+ `EditorContext::AnimHotChanged()`（EnterPlay/ExitPlay
  清位——退 Play 快照重建自然消）+ `RescanAssets` Play 态变更集过滤（modified ∩
  动画族 → 置位 + Console info 逐文件留痕）；AnimControllerPanel 只读横幅下
  追加红字行（`animctrl.hot_banner`，anim.json zh/en 各 230=230 对齐）；
  单测 `TestAnimFamilyPredicate`（+8 checks：三类型命中 / table/rml/sprite/
  audio/generic 不命中）。零引擎运行时改动、零新线程、零 watcher 协议变更。
- **vs-survivor 去 GUID 化（动画面）**：生成器产 `Assets/mob.ani`（walk/hit →
  monster 两 clip，guid 5bd31a7c30000007）+ Mob.prefab 增
  `AnimGraph{setGuid=kMobAni, controller=0}`（纯按名解析模式）+ PlayerCombat
  `Anim.ClipId("5bd31a…")×2` 常量退役 → `Anim.Play(victim,"hit",false)` +
  `Queue(victim,"walk")` 段名形态；产物再生成入库（非 Flow 实体 guid 漂移 =
  再生成既有先例）。
- **帧事件命名形态 + val 端到端**：svr-test Monster01/02 Attack.clip 打点
  `{frame:6,id:1}` → 补 `"val": 7`；anim-events.tab payload 列消费（`int`）；
  DuelBehaviour `OnFrame("attack-hit", Strike)` → msg 重载
  `OnFrame("attack-hit", m => Strike())`（P0=帧号、P1=val 在手不读 = 行为
  零漂移）；Game.dll 重编译落 `.lemon/bin/`。
- **Manual 动画章端到端**：帧事件节重写为命名事件主口径（⑬a/b/c 全作者面：
  下拉/内联新建/Define 对账/双重载/val 载荷/反向 lint 三件/旧档裸 id 渐进
  采用）+ 终态（⑮a）+ IsFinished/IsPlaying 三态 + Queued（⑮c）+ Play 热改
  横幅与红字速查（⑮d）+ 验收参考三条（anytest / DuelBehaviour 命名事件全链 /
  模板去 GUID 样本）；文档站重建零错误。

## 验证（机器面）

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记）。
- **smoke-template 全绿**：`hitClip=YES`（段名播放链路实证）kills=177
  cards/death/revive/results/restart/pause/settings/tomenu/replay/second-project
  ids identical 全 OK——去 GUID 行为等价实跑坐实（mob.ani FindByName → 同
  clipId，组件写字段逐位同）。
- **svr-test 无头 ani.scene 700 帧 rc=0** 零脚本红字（「缺绑 Hit」= ani.scene
  既有拓扑告警，本批未触 controller/ani 档）。
- 单测 **35,045**（+8）/script-tests 1,925（零新用例——msg 重载走 ⑬a 既有
  TestAnimEventBridge 覆盖面）/ctest 4/4/回归 full 26 步/金回放三档
  mismatches=0：见 ci-daily 报告（批文件 §4）。
- vtable 60·组件 33·系统 21 不变；smoke-anim 帧预算 141 不动。

## 裁决与登记

- "去 GUID 化"收窄为动画面定形（批文件 §1）：非动画资产维持 {GUID:…} 生成期
  占位符；name→资产桥 v1.1 触发式。
- ⑮d "mtime 新于 EnterPlay" 三条件的实现口径 = watcher 500ms 轮询基线（脏必
  发生在本 Play 会话内）+ Play 态过滤 + IsAnimFamily——不逐文件存 EnterPlay
  时间戳（ChangeSet 无 mtime 面，加存 = 为不变式而不变式）。
- 真人组合走查（含 ⑮d 横幅手测 + ⑫ 帧事件 UI）= 用户窗口；M8 动画系列机器面
  至此**全清**。
