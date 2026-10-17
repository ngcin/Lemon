# 2026-10-17 M8 批⑬a —— 帧事件注册表（名字↔id + 编辑器下拉 + Anim.OnFrame 启动校验）

## 输入

批⑬ 分期第一段（[批文件](../Plans/M8/2026-10-17-b13-anim-event-registry.md) §4：
⑬a 注册表 + 编辑器下拉 + C# 门面——治魔法数字 id 与静默失败，性价比最高一段
先行）。动因与定形见批文件 §1（批⑫ 打点链落地后作者面残余负担 = id 魔法数字
+ 发现性；ADR-013 治 GUID 魔法常量的同构药方 = 项目级注册表 + 按名解析）。

## 交付

- **Engine：`Assets/AnimEventRegistry.h/.cpp`（进程级单份，宿主注入）**
  - `Assets/Tables/anim-events.tab`（ADR-012 .tab 轨道；列头即列契约
    `name/id/payload/desc`，payload/desc 列⑬c/⑭ 消费、本批原样保留）；
  - 语义钉死：id 插入时分配（max+1）**永不复用**；行级问题（非法名/非法 id/
    同名/同 id）跳行 + WARN 首行胜；坏 JSON/列头不符 = 整表拒绝（裸 id 保底，
    ADR-012 缺表哲学）；**同名异 id 重注入 = 漂移 WARN**（"应新建行替代"，
    以表为准）；名字校验单源导出（`ValidAnimEventName`：非空/≤64B/无引号控制符
    ——跨界窄串 + 下拉字面约束）；
  - 热路径零参与：装载期解析为 u16，模拟/回放/哈希流不见名字。
- **桥（装载期低频，ADR-013 ClipByName 同构药方）**：vtable 尾追 1 项
  `animEventIdByName`（**59→60**，表尾追加第六次沿用；旧宿主/旧 SDK 判空降级
  -1）。批文件 §6"vtable 零新增"按实施收窄为**热路径零新增通道**——订阅糖
  仍是 AnimFrame 队列上的 u16 逐位比较；名字解析一次发生在装载期走低频桥
  （批文件自引的 ADR-013 先例即 ClipByName 尾追项）。
- **C# 门面（`Lemon.SDK/AnimEvents.cs` 新文件）**：
  - `AnimEvents.Define(name)`——GameMain.Configure 对账：命中预缓存（id 永不
    变更 = 缓存安全）、未注册 = **启动红字**（表缺失/拼错二义同面）+ 返 false；
  - 「以表为准」实现口径：SDK 不另立 id 源，"Define/表 双源冲突"收窄为两类
    红字——Define/OnFrame 名字未注册（SDK 层）与表内同名/同 id 行冲突（引擎
    装载期）；换域清缓存（DomainManager 三处 Reset 点 +AnimEvents.Reset）；
  - `Anim.OnFrame(g, name, cb)` 静态版（域级订阅，文档注明实例场景用助手）+
    **`LemonBehaviour.OnFrame(name/谓词, cb)` 实例版**（Subscribe 助手同款
    OnDestroy 自动退订——M15 病程防复发；未注册 = 红字 + 该订阅挂空不影响
    其他订阅）。
- **宿主注入**：编辑器 = `EditorContext::RefreshAnimEventRegistry`（开项目/
  RescanAssets 尾/EnterPlay 快照点/ExitPlay 补注；变更门 = 条目 guid+hash，
  **Play 中冻结**——BuildPlayTableCache 同语义，域线程只读无并发写）；
  lemon-game = 装配脚本宿主前直读契约路径（tables 目录大小写折叠回退）；
  script-tests = 夹具注入。寻址 `AssetDatabase::FindByPathFold`（ASCII 折叠
  ——svr-test 既有 `Assets/tables/` 小写 vs 契约路径 `Tables`）。
- **编辑器·AnimationPanel（作者面）**：
  - 打点模态：注册表在场 = **名字下拉**（项 = `名字（#id）`；当前 id 未注册 =
    红字置顶项可原样保存——旧档兼容）+ **内联「新建事件…」**（建行即写表：
    追加行 → TableToJson 原子写 → Rescan → 注册表注入自证；缺表项目零门槛
    从此采用）；缺表 = 裸 id DragInt 照旧（降级保底）+ 新建入口同样可用；
  - chips 行：已注册 = `帧·名字`（tooltip 补 `·#id` 副位）；未注册 = 红 tint
    `帧·#N（未命名）`；chips 右键编辑项显名；胶片带 tooltip 逐打点显名——
    **旧档裸 id 不迁移不重写，显示层兜住（渐进采用）**；
  - i18n 双语 +16 keys（anim.json 202→218 对齐）。
- **svr-test 迁移（真实消费者）**：`Assets/tables/anim-events.tab`
  （`attack-hit → 1`，与 Monster01/02 Attack.clip 既有 `id:1` 打点同 id =
  行为零漂移前提；.meta 经 1 帧编辑器会话物化）+ `GameMain.Configure` 增
  `AnimEvents.Define("attack-hit")` + DuelBehaviour `m.User == 1` 魔法数字
  → `OnFrame("attack-hit", Strike)`。
- **测试**：
  - 引擎单测 `TestAnimEventRegistry`（+19 checks：好表跳号/行级四类拒/漂移
    以表为准/坏表整拒/缺表保底/NextId）；
  - script-tests `TestAnimEventBridge`（+7：Define 对账面 d1 m0 + OnFrame
    实例/静态双路径命中 1|1 + 错 id/错实体/未注册挂空三分支零触发；
    AnimEventProbeBehaviour typeId 27 表尾——0 基，Light 探针注释 26 本就
    正确、误判已回改）；
  - `--smoke-anim` f134-136 注册表链 `reg(create/pick/unreg/del)` 四位
    （帧预算 **134→138**，回归脚本两处同步——f37..f133 全占位，b10 先例）；
    临时项目零表起步：新建事件写表+注入 / 按名打点档面落注册表 id / 裸 id
    99 badge 面 / 基线还原（种子档盘面仍零改动）。

## 验证

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记）；单测
  **35,001 checks OK**（+19）；script-tests **1,918 OK**（+7）；ctest 4/4。
- `--smoke-anim` zh+en：`reg(create=YES pick=YES unreg=YES del=YES)` 全位
  YES，既有位（含批⑫ events 五位）零回归。
- **DuelBehaviour 行为逐位不变实跑坐实**：lemon-game 无头 ani.scene 700 帧，
  新（OnFrame 命名订阅）vs 旧（git HEAD `m.User==1`）对照组**伤害时间线
  逐行 IDENTICAL**（11 发 strike 的帧号/伤害值/对手余量全等——含 RNG 消费
  序）。附带发现：ani.scene 怪物 HP 实为 14 万/10 万（历史数值层放大），
  900 帧 `duel.winner` 落档验收口径（m7c b1 先例）早已过期——非本批回归，
  真实消费者验收锚改为时间线对拍（更强：逐位含 RNG）。
- **回放三档 PASS（现录现放，09 §6.8/b6 口径）**：sim-st/sim-mt
  `replay=PASS mismatches=0` + bench-script 双 PASS（TestScript 新探针只注册
  不实例化 = 哈希流零接触）；id 线上格式不变——Monster01/02 Attack.clip
  盘面零改动（git status 空），no-edit 往返由批⑫ golden 单测继续钉。

## 裁决与登记

- "vtable 零新增"（批文件 §6）→ 实施口径 = 热路径零新增通道；装载期解析走
  ClipByName 同款低频桥尾追 1 项（59→60）。已按"推翻既定设计需文档交代"在
  本条目与本段记录；⑬b lint 起订阅者集合（Define/OnFrame 声明面）已就位
  （`AnimEvents.DefinedNames` internal 面）。
- 注册表 Play 冻结 + ExitPlay 补注（watcher 事件在 Play 中到达不推进变更门
  记忆——漏变更由退 Play 拍补上）。
- 帧预算 134→138 后 f137 空位一枚（⑬b lint 链可占）。
- ⑬b（反向 lint 三件）/⑬c（payload 类型化 `m.F`）顺延；原⑬ 样本+手册+模板
  = 批⑭ 不变。
