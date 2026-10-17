# M8 批⑭ —— 样本 + 手册 + 模板（动画系列收尾批，带 ⑮d）

Status: planned 2026-10-16（b9 系列立项"样本+手册端到端+去 GUID 化"，⑬/⑮ 插队后
顺延为批⑭）→ **机器面收口 2026-10-17**（[DevLog](../../DevLog/2026-10-17-m8-b14-samples-manual-template.md)；
真人组合走查 = 用户窗口，基于手册 + 模板测试）

## 1. 输入与定形

- **输入**：动画创作面系列（⑨–⑬/⑮a/c）机器面全清后的收尾批——创作者路径
  （图编辑器/打点/注册表/终态）已落地，但**手册动画章还是裸 id 口径**、**模板
  脚本仍持 clip GUID 魔法常量**、**Play 态热改静默无感**（b5 走查摩擦实报族，
  ⑮d 随本批走查）。
- **去 GUID 化定形**（本批定义，b9 起只有一句话登记）：动画面——模板脚本不再
  手写 clip GUID；经 `.ani` 段名 + `AnimGraph` 显式集绑定（ADR-013 按名解析
  作用域）走既有 `Anim.Play` 字符串重载。**非动画资产（音频/表/prefab）的
  GUID 消费维持既有 {GUID:…} 生成期占位符机制**（源在 VsTemplateGen.cpp，
  早已占位符化）——name→资产桥是 v1.1 触发式（消费者出现即议）。

## 2. 范围（In）

| # | 面 | 内容 |
|---|---|---|
| 1 | ⑮d Play 态热改横幅 | `assets::IsAnimFamily`（AssetTypes.h 单源谓词）+ `EditorContext::AnimHotChanged` 标志（EnterPlay/ExitPlay 清位）+ `RescanAssets` Play 态过滤变更集（modified ∩ 动画族）→ 置位 + Console info；AnimControllerPanel 只读横幅下追加红字行；i18n 双语 +1 key（anim.json 230=230 对齐）；单测 `TestAnimFamilyPredicate`（+8 checks）。**快照语义保留**——只提示不热更；"mtime 新于 EnterPlay" 由 watcher 500ms 轮询基线天然满足 |
| 2 | vs-survivor 去 GUID 化 | 生成器新增 `Assets/mob.ani`（walk/hit 两段 → monster 两 clip；guid `5bd31a7c30000007`）+ Mob.prefab 增 `AnimGraph{setGuid=kMobAni}`（controller 0 = 纯按名解析模式）+ 内嵌 PlayerCombat 脚本 `Anim.ClipId(GUID)×2` → `Anim.Play(victim,"hit",false)`/`Queue(victim,"walk")` 段名形态；产物再生成入库（实体 guid 漂移 = 既有先例，Flow 钉死项不动） |
| 3 | 帧事件样本命名形态 + val 端到端 | svr-test：Monster01/02 `Attack.clip` 打点补 `"val": 7`（⑬c 载荷通道真实消费样本）+ `anim-events.tab` payload 列 `-`→`int`（批⑬a 预留列本批消费）+ `DuelBehaviour` 切 `OnFrame("attack-hit", m => Strike())` msg 重载（P1 载荷在手、不读 → 行为零漂移） |
| 4 | Manual 动画章端到端 | `14-animation.md`：帧事件节重写（命名事件主口径：注册表/下拉/内联新建/Define/OnFrame 双重载/val 载荷/反向 lint/旧档裸 id 渐进采用）+ 状态节终态（⑮a：徽记/any 不命中/复活流黄字）+ 脚本 API 表补 IsFinished/IsPlaying 三态口径 + Queued（⑮c）+ 调试节 Play 热改横幅（⑮d）与红字速查两行 + 验收参考三条（anytest/命名事件全链/模板去 GUID 样本）；文档站重建（tools/build-manual.mjs） |

### Out（明确不做）

- **音频/表/prefab 的 name→资产解析桥**：非动画面，{GUID:…} 占位符机制已够
  （v1.1 触发式登记）；
- **Play 态热重载 controller/clip/ani**：推翻快照语义 = 动确定性根基（⑮d 只补
  提示，b15 批文件既定裁决）；
- **模板侧帧事件打点**：模板无攻击段 clip，硬造打点 = 为样本而样本；命名事件
  真实消费者 = svr-test DuelBehaviour（⑬a 起）；
- **⑮b facing**：已挪 M9 首批（2026-10-17 拍板）。

## 3. 设计要点

- **⑮d 挂点选 RescanAssets 而非 watcher 回调**：FileWatcher 只置脏无逐文件信息，
  变更集在 `AssetDatabase::LastChange`（guid 列表）——Play 态过滤动画族是
  ChangeSet 消费面的一行扩展（sprite 导入/音频烤制同款钩位）；不新增线程不动
  watcher 协议（b15 §3 既定）。
- **去 GUID 行为等价性**：mob.ani 段名解析 → 同两 clipId（FindByName 集内查），
  Play/Queue 写的组件字段与原 uint 重载逐位同——smoke-template `hitClip=YES` +
  kills=177 实跑坐实。
- **val 零漂移前提**：payload[1] 仅经 msg 重载可见，DuelBehaviour 不读 P1 →
  伤害时间线/RNG 消费序不动；金回放三档现录现放机械证明。

## 4. 出口判据

- ⑮d：进 Play 外部改 .controller → Console info + 工作台红字横幅；退 Play 横幅
  消；非 Play 态改动零提示零渲染成本（谓词单测钉过滤面）；✅ 机器面（手测归
  真人走查）。
- 模板：smoke-template 全绿（含 hitClip/kills/flow/replay 位）；生成产物 diff
  仅预期面（mob.ani 新增 + Mob.prefab AnimGraph + 脚本段名化 + guid 漂移先例）。
- svr-test：无头 ani.scene 700 帧 rc=0 零脚本红字；金回放三档 mismatches=0。
- 手册：动画章覆盖 ⑬a/b/c+⑮a/c/d 全部作者面口径；文档站重建零错误。
- 全批：单测/script-tests/ctest 4/4/回归 full 26 步全有绿记录；vtable 60·组件
  33·系统 21 不变（零引擎运行时改动）；smoke-anim 帧预算 141 不动（⑮d 不加
  冒烟位——谓词单测 + 真人手测口径）。

## 5. 遗留与登记

- 真人组合走查（⑨–⑭ 全系列 + ⑫ 帧事件 UI + ⑮d 横幅手测）= 用户窗口，基于
  Manual 动画章 + vs-survivor 模板测试；
- 音频/表/prefab name 解析桥 → v1.1 触发式（08 登记项随 M8 收口盘点）。
