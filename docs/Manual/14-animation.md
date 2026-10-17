# 动画状态机

动画状态机（`.controller`）解决一个问题：**角色"该播哪段动画"的常态逻辑，从脚本
里搬进可视化的图**。走位循环（idle↔walk）、攻击段末回归、受击/死亡全局打断——
这些每个角色都要、抄一遍就乱一坨的规则，画成一张图；脚本只负责说语义（"我被
打了"、"我在移动"），不关心 clip 名。

设计基准与决策记录见 [../EngineDesign/03-ECS-Runtime.md](../EngineDesign/03-ECS-Runtime.md) §8 与
[ADR-013](../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md)；本章是纯操作指南。

## 三件套：clip → 动画集 → 状态机

| 资产 | 后缀 | 职责 | 产出工具 |
|---|---|---|---|
| 帧动画剪辑 | `.clip` | 一段逐帧动画（帧序列 + fps + 循环模式） | Animation 工作台 |
| 动画集 | `.ani` | **每角色**的绑定表：段名 → clip | Animation 工作台 |
| 状态机 | `.controller` | **全项目共享**的状态词表 + 参数 + 过渡 | AnimController 工作台 |

对齐规则只有一条：**`.controller` 里的状态名 = `.ani` 里的段名**。状态机说"进入
Hit 态"，运行时拿"Hit"去该角色绑定的 `.ani` 里查段、播对应 clip。

- 一份状态机可以驱动任意多个角色：角色 A 的 `.ani` 只绑 Idle/Walk，那么 Hit 态
  对它就是缺绑——条件满足也保持当前画面（Console 出一次红字提醒），等于天然
  封门，零成本。
- 换皮/换体型 = 换 `.ani` 绑定表，状态机不动。

## 五分钟跑通

演示工程 `demo/svr-test` 自带一个最小验收场，先看它建立直觉：

1. 打开编辑器加载 demo/svr-test，`Ctrl+O` 打开 `Scenes/anytest.scene`；
2. 双击 `Assets/Controllers/Duelist.controller` 进状态机工作台；
3. 点 Play：场景里的怪物会自动走完 **待机 → 行走 → 攻击 →（攻击中被击打断）受击
   → 回待机 → 死亡** 一整圈，工作台画布上当前状态实时高亮；
4. 播放中选中该实体，Inspector 的「动画参数」区拖动 speed、点「激发」按钮，手动
   遥控状态切换。

这个场景挂了一个探针脚本（`AnyStateProbeBehaviour.cs`）自动驱动参数并逐项断言，
可当参考实现读。

## 建一个状态机

在 AssetBrowser 的 `Assets/Controllers/` 目录（共享库约定落点）右键新建控制器，
双击进入工作台。工作台分两栏：左边画布管结构，右边检查器管细节（参数表/边条件）。

### 参数

点画布空白处（不选中任何东西），检查器显示参数表。参数是脚本与图之间的黑板，
上限 8 个，三种类型：

| 类型 | 用途 | 条件写法 |
|---|---|---|
| float | 连续量（速度、距离、血量百分比） | `>` `>=` `<` `<=` 比较阈值 |
| bool | 开关（持盾、潜行） | 为真/为假 |
| trigger | 一次性事件（受击、死亡、攻击指令） | 已激发 |

trigger 的语义：激发后**保持待发**，直到被某条命中边消费（消费即清零）——本 tick
没切走的 trigger 下个状态照样响应，跨状态有效。

### 状态

- 右键画布「新建状态」，落点就在右键处；
- 双击节点改名（改名会同步所有边引用和 `.ani` 段名预期）；
- 右键节点「设为入口」：入口状态 = 实体进 Play 后的起始态，节点左上有「入口」
  标记；
- 右键「删除状态」：级联删除它的所有出入边；
- **终态**（右键「设为终态」/检查器复选，节点双圈 + 「终」角标）：声明"进了
  就不再被全局中断"——**any 边在终态不命中**（死亡态不再被受击边打断），
  显式 from 边照常（复活流作者自决）。缺省不标记 = 旧行为（any 边可打进）。
  终态挂条件出边会出黄字提示（复活边合法，但请确认）。

### 保存

`Ctrl+S` 走三道门禁：序列化 → 解析回读等值（roundtrip）→ 原子落盘。条件引用了
不存在的参数、空条件且非段末这类作者错误，在解析关就被拦下并给出具体提示——
保存失败时按提示修，不会产出坏档。

## 画布操作速查

| 操作 | 手势 |
|---|---|
| 平移画布 | 中键拖动（或按住空格 + 左键拖动） |
| 缩放 | 滚轮 |
| 适配全图 | F |
| 移动节点 | 左键拖节点本体（8px 网格吸附，位置随档保存） |
| 改名 | 双击节点 |
| 建边（拖） | 从节点**任一边中点的圆点**（hover 显现四个）按住拖到目标节点松开 |
| 建边（点） | 右键节点/Any 节点「新建出边」，再点目标节点；Esc 或点空处取消 |
| 选边 | 左键点边（点到直线 6px 内）；箭头是天然的目标参照 |
| 删边 | 选中边后 Del |
| 撤销/重做 | Ctrl+Z / Ctrl+Shift+Z（或 Ctrl+Y），工作台窗口聚焦时生效 |

防误触：把手上**单击**（位移小于 6px 就松开）不会建边，会话自动取消。

边的走线是直线：双向对（A→B 与 B→A 并存）自动渲染成两条平行线；同向多条叠边
逐条错开；箭头恒在直线中点、指向目标。两端锚定在节点边框上，不会从节点底下
穿过。

## 过渡（Transition）详解

一条边 = `from → to` + 条件。选中边后检查器显示全部细节：

- **条件**：多个条件按 AND 组合；float 用比较符 + 阈值，bool/trigger 直接选参数；
- **段末过渡（exitTime）**：当前段播完（非循环段收尾）才切——攻击播完回待机就
  靠它。可与 when 条件并存（段末且条件成立才切）；
- **目标（to）**：下拉可换目标，列表会过滤源自身；
- **求值序**：检查器显示"同源第 N 条/共 M 条"——运行时按文件顺序评估，**首条
  命中即切**。想让高优先级的边先判，就把它排在前面（画布上无序，按创建/文件序）。

建边时的起步默认：项目里有参数 → 默认带一条首参 trigger 条件；没参数 → 默认
exitTime。都是占位，选中后改。

## Any 节点：全局中断边

画布左上方的「任意」节点是特殊节点：**从它连出的边对所有状态生效**，求值时先于
一切状态边评估。

**为什么需要它**：受击、死亡这类"任何状态都要响应"的中断，如果用普通边写，每个
状态都要手抄一条 ×→Hit、×→Death——五个状态就是八条线，画布直接变蛛网。用 Any
节点收敛成两条，拓扑一眼可读。

**操作**：

- 建边：与普通节点一样，从 Any 节点四边中点的圆点拖出（或右键「新建出边」）；
- 挪位：拖 Any 节点本体，位置随档保存（存在 `.controller` 的 `editor.nodes` 保留
  键 `"*"` 里，不影响运行时）；
- 删除：右键 Any「删除任意节点（清空 N 条全局边）」——删的是它的全部出边，节点
  本体恒在（它是建全局边的入口，同 Unity Any State）；
- 排序：Any 边之间同样按文件序首条命中——把 any→Death 排在 any→Hit 前面，死亡
  优先级就更高。

**语义要点**：

- 同态 no-op：any→X 命中时如果当前已经在 X，保持播放不重启（等于 Unity 的
  Can-Transition-To-Self 默认关）。**"重播当前动画"归脚本 `Anim.Play` 管**，图
  里没有也不需要自环边（from == to 会被三处入口拒绝）；
- **死亡中再受击会切到 Hit**——这是拓扑事实，不是 bug。两种收法：把"死亡中
  不受击"的门写进伤害脚本（见下文脚本示例），或者更省一句——把 Death 设成
  **终态**（上节），any 边在终态天然不命中；
- any 边也可以带 exitTime（"任意当前段播完时切"）。

## 运行时规则十条

1. 进 Play 时，当前状态初始化为入口状态，参数从控制器默认值播种；
2. 每 tick 评估一次：先扫全部 any 边（文件序），再扫当前状态的出边（文件序）；
3. 首条条件满足的边命中，执行切换——**每 tick 至多切一条**，无级联（新状态的边
   下个 tick 才参与评估）；
4. 命中边消费它用到的 trigger（消费即清）；没被任何边用到的 trigger 保持待发；
5. 同态命中（目标段 = 当前段）不重置进度、不重播；
6. exitTime 边只在当前段**收尾边沿**触发（非循环段播完；循环段永不触发）；
7. 图切换的落点是换段队列，与脚本 `Anim.Play/Queue` 同一条指令通道，**脚本直写
   优先级高于图**（脚本当 tick 的 Play 立即生效，图评估不覆盖它）；
8. 缺绑状态（角色 `.ani` 没这个段名）：保持当前画面 + Console 红字一次（warn-once）；
9. 未绑控制器 / 未挂 AnimParams 的实体：图不驱动，Animator2D 按自身 clipId 播；
10. 参数值不落档（运行时态），Stop 再 Play 从默认值重新播种。

## 绑定到角色（Inspector）

场景实体加两个组件即可接入状态机：

- **动画图（AnimGraph）**：两个资产槽——控制器（`.controller`）+ 动画集（`.ani`）。
  下拉全列同类资产、从 AssetBrowser 拖入、右键清空。控制器为空 = 纯按名解析模式
  （脚本仍可按段名 Play，不走图）。
- **动画参数（AnimParams）**：按所绑控制器的参数表**具名显示**（名字 + 类型 +
  值）。编辑态只读显示默认值；**Play 态实时可调**——float 直接拖、bool 勾选、
  trigger 点「激发」按钮（等价脚本 `Anim.Trigger`），是调状态机最直接的遥控器。
- Animator2D 组件照常存在（当前 clipId/播放速度/循环模式），图评估写的正是它的
  换段队列——两者是配合不是竞争。

## 脚本驱动（C#）

脚本对图只做一件事：**写参数**。语义意图归脚本，表现归图。

| API | 作用 |
|---|---|
| `Anim.SetParam(g, "speed", 5f)` | 写 float/bool 参数 |
| `Anim.GetParam(g, "speed")` | 读参数 |
| `Anim.Trigger(g, "hit")` | 激发 trigger（= 写 1，图命中即清） |
| `Anim.IsPlaying(g)` | 在播中？（= playOnStart 开关；Paused 态 false） |
| `Anim.IsFinished(g)` | 非 loop 段播完？（Once 钳末帧后 true、loop 恒 false、Play 切段归零） |
| `Anim.Queued(g)` | 有在途换段队列？（Queue/CrossFade 已排队未切） |

播放状态三态口径：**Playing / Paused / Finished** 互斥面——前两态由
`IsPlaying` 判（`Anim.Pause`/`Resume` 切换），末态由 `IsFinished` 判。注意 Once
段播完钳末帧后组件仍"在播"（IsPlaying = true），"这段演完了没"问 IsFinished。

走位与攻击（挂玩家实体，每帧；输入面 = `Input.Axis` 移动轴 / `Input.Attack`
攻击键，见 [05-scripting.md](05-scripting.md)）：

```csharp
protected override void Update()
{
    // 走位：速度参数驱动 idle↔walk 条件边（Axis 非零 = 移动中）
    Vec2 move = Input.Axis;
    Anim.SetParam(gameObject, "speed",
                  move.X != 0f || move.Y != 0f ? 5f : 0f);
    // 攻击指令：trigger 交给图（Attack 段末自动回 Idle；trigger 待发语义天然去重）
    if (Input.Attack)
        Anim.Trigger(gameObject, "attack");
}
```

受击与死亡（伤害入口处，含"死亡中不受击"门）：

```csharp
public void ApplyDamage(GameObject target, float amount)
{
    if (!target.TryGetComponent<Health>(out var hp)) return;
    if (hp.Cur <= 0f) return;                       // 死亡中不受击（代码侧门）
    hp.Cur -= amount;
    target.SetComponent(hp);
    Anim.Trigger(target, hp.Cur <= 0f ? "death" : "hit");  // any 边全局打断
}
```

图之外的直控 API 仍然有效，各有所长：

- `Anim.Play(g, "hit", loop: false)` —— 立即切段、同段 = 重播（重启语义）。适合
  一次性演出、UI 动画、不走状态机的零碎段；
- `Anim.Queue(g, "walk")` —— 当前段播完自动接下一段。图内 exitTime 边是它的
  可视化形态，常规用法优先画进图里；
- `Anim.CrossFade(g, "idle", 0.2f)` —— 延迟切换（帧动画无姿态混合，fade 是
  延迟不是渐变）。

## 帧事件：命名事件 + 打点数值

帧事件 = "clip 播到第几帧时发一个事件"，攻击判定帧、发射帧、脚步声这类
**时机有视觉含义**的逻辑用它；冷却、DoT 跳动这类纯玩法计时不要塞进来（写脚本
计时器/数值表）。

事件有**全局名字**：项目里有一张注册表 `Assets/Tables/anim-events.tab`
（`名字 → id` 稳定分配，id 分配后永不复用）。打点、订阅全用名字，id 魔法数字
不再进脚本。

### 打点三步（Animation 面板，双击 `.clip` 或在动画集左列选中段）

1. 胶片带选中目标帧（或点事件行里已有的 chip 跳过去），右键帧格
   「添加帧事件…」，或点预览条事件行的「＋ 打点」；
2. 模态里填**帧号**（0 起，与 `.clip` 里 `events[].frame` 同基）和**事件名**——
   注册表在场就是下拉选（项 = `名字（#id）`）；要起新名字直接点内联
   「新建事件…」，建行即写表，零门槛。还可以带一个**数值载荷（val）**——
   打点想携带一个数（伤害系数、发射数量）就填它，不填 = 0；
3. C# 侧按名订阅（推荐挂在角色实体脚本上，实例销毁自动退订）：

```csharp
// GameMain.Configure 里对账一次（拼错/缺表 = 启动红字，不再静默）
Lemon.AnimEvents.Define("attack-hit");

// DuelBehaviour.cs 同款——不带载荷：
OnFrame("attack-hit", Strike);
// 带载荷（回调收 GameEventMsg：P0 = 帧号、P1 = 打点 val）：
OnFrame("attack-hit", m => StrikeScaled(m.P1));
```

未注册名字（表缺/拼错）= 启动红字 + 该订阅挂空（不影响其他订阅）；静态版
`Anim.OnFrame(g, name, cb)` 也存在，但实例场景请用上面的 `LemonBehaviour.OnFrame`
助手（自动退订，防实例反复生成销毁下无界累积）。

### 面板配套件与护栏

- 事件帧在胶片带右上角有**旗点**（多个事件带细环；Space 预览播放扫过时提亮 =
  命中反馈）；预览条事件行列全部打点（显名，点 chip 跳帧、× 删除、右键编辑）；
- **事件跟着帧走**：插帧/删帧/换序/拖拽重排时打点自动随帧移动，删掉带事件的
  帧则事件一起删；
- 同一帧同一事件不允许重复（运行时会双发，编辑器直接拒）；
- **反向 lint 三件**（编辑器Console/保存门）：打点用了未注册 id（红 badge
  「未命名」）、注册表里的事件全项目无人订阅、订阅了却没有任何打点——配置
  漂移在编辑期就看得见；
- 旧档裸 id 打点**不迁移不重写**：显示层兜住（红 tint `帧·#N（未命名）`），
  想命名随时在模态里补注册。被 any 边打断的段不会再发后续帧事件（见下 FAQ）。

## 调试

- **Play 实时调参**：播放中选实体 → Inspector 动画参数区直接拖/激发，即时看
  状态切换；
- **当前态高亮**：Play 中打开状态机工作台，当前状态节点黄框实时高亮（画布只读）；
- **Play 中改档提示**：Play 中在外部改了 `.controller`/`.clip`/`.ani`（watcher
  500ms 内捕获）→ Console 出一条 info，AnimController 工作台顶部追加红字横幅
  「已在磁盘上被修改——当局不生效，退 Play 后再进 Play 生效」。当前局用的始终
  是进 Play 时刻的快照（确定性根基）；退 Play 后横幅自动消；
- **Console 红字速查**：

| 提示 | 含义 | 处置 |
|---|---|---|
| 集内无段 'X'（红字一次） | 该角色 `.ani` 缺绑 X 段 | 有意缺绑可忽略；否则补段 |
| Play 装配：脚本类型未注册 | 场景挂了脚本类但没在 GameMain 注册 | Configure() 里补 Register |
| 参数 'x' 解析失败 | 写了控制器词表里没有的参数名 | 检查拼写或补参数 |
| AnimEvents 'x' 未注册（启动红字） | Define/OnFrame 的名字不在 anim-events.tab | 检查拼写；或在打点模态「新建事件」入表 |
| 终态出边将打断驻留语义（黄字） | 终态挂了条件出边 | 复活流属合法，确认即略；否则删边 |

- **验收参考**：`Scenes/anytest.scene` + `AnyStateProbeBehaviour.cs` 是带断言的
  端到端样例（自动驱动 + `[anytest]` 日志逐项 PASS/FAIL），照抄结构可给自己的
  状态机做无头回归；
- **命名事件样例**：`DuelBehaviour.cs` 的 `OnFrame("attack-hit", m => …)` +
  `anim-events.tab` + Attack.clip 帧 6 打点（`val: 7` 载荷）是注册表全链参考；
- **按段名播放（去 GUID）样例**：vs-survivor 模板 `mob.ani`（walk/hit 两段）+
  Mob.prefab 的 AnimGraph 绑定 + `PlayerCombat.cs` 的 `Anim.Play(victim, "hit")`
  ——clip GUID 只活在绑定表里，脚本零魔法常量。

## 常见问题

**几个角色动画数量不一样，要各建一份状态机吗？**
不要。一份状态机取状态词表并集，每个角色一份 `.ani` 按名绑定；缺的段天然不可达
（条件可以由脚本不写 trigger 封住），这正是按原型共享的设计。

**攻击中被打断，攻击伤害打点还会出吗？**
不会。切段即离开 Attack 段，帧事件只在 Attack 的 clip 播放中触发——被打断时点
未到就不出伤，符合直觉；需要"出手后不可打断"就把受击门做进伤害脚本（出手帧后
忽略 hit）。

**为什么不能自环（A→A）？**
图里的切换没有重启语义——同段命中是 no-op，自环边只会白白吃掉 trigger 还遮住
后面的边。重播当前动画用 `Anim.Play(g, "同段名")`（同段 = 重播，脚本域语义）。

**想让死亡优先于受击怎么办？**
把 any→Death 排在 any→Hit 前面（Any 边按文件序首条命中），再在伤害脚本加死亡门
（见上例）。

## 边界（v1 不做）

blend tree（参数加权混合）、layers（并行层）、sub-FSM（子状态机）不在 v1 范围，
也没有"接近再议"的隐藏开关——这三样是耦合系统，做半套比不做更糟。替代做法：
混合用脚本在两个实体/两个参数间插值；分层用多实体；超 15 态的大图先考虑拆原型。
评估重启的完整论证见 [ADR-013](../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md) D4。

相邻章节：资产管线见 [07-assets.md](07-assets.md)，脚本 API 总览见
[05-scripting.md](05-scripting.md)，编辑器布局见 [03-editor.md](03-editor.md)。
