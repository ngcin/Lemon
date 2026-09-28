# Lemon 引擎设计 — 04 C# 脚本层

> 目标：C# 写玩法像 Unity 一样顺手，同时引擎内核保持 C++ 性能。
> **基调（ADR-009）：API 表面像 Unity/Prowl2D，底层实现永远性能优先（C++ 内核、批量边界、热路径零托管分配）。**
> 方案：CoreCLR 经 hostfxr 宿主进引擎进程（**直接移植 Luma `Scripting/CoreCLRHost.h/.cpp` + `Luma.SDK` 的 ScriptLoadContext，MIT**），混合模型三档 API，边界只有"批量 API + 事件队列"两条通道。C# 门面与 Unity **命名级对齐**（GameObject 正名 / LemonBehaviour / Unity 生命周期，ADR-009）。

---

## 1. 宿主架构（CoreCLR Hosting）

```mermaid
flowchart LR
    subgraph Cpp["C++ 引擎进程"]
        Host[CoreCLRHost<br/>nethost→hostfxr→runtime]
        FT[GCHandle 函数指针表<br/>Create/Start/Update/SetProp/Invoke/Destroy]
        BQ[批量 API 导出(C ABI)]
        EQ[事件队列导出]
    end
    subgraph Cs["托管侧（.NET 10）"]
        Sdk[Lemon.SDK<br/>GameObject/Scene/Input/Asset 门面]
        ALC[ScriptLoadContext<br/>可卸载域 + 影子复制]
        Usr[用户程序集<br/>Game.Script.dll]
    end
    Host -->|loadbyte[]| ALC --> Usr
    FT <--> Usr
    Sdk -->|P/Invoke| BQ & EQ
```

启动流程（hostfxr 标准路径，Luma CoreCLRHost 同款）：

1. `nethost` 定位 `hostfxr`（Windows `LoadLibrary` / POSIX `dlopen` 双路径）；
2. `hostfxr_initialize_for_runtime_config`（SDK 的 `runtimeconfig.json`）→ 取 `get_runtime_delegate` 加载 `hostpolicy`；
3. `load_assembly_and_get_function_pointer` 拿到托管入口（`Lemon.SDK.Bootstrap`）；
4. Bootstrap 反向把 C# 侧函数表注册回 C++（GCHandle 固定委托），C++ 从此经函数表调用托管对象，**无 COM/无反射热路径**。

关键配置：ServerGC=false（工作站 GC，低延迟并发模式）+ `System.GC.RetainVM` 调优；SDK 目标 `net10.0`，随引擎分发确切版本的运行时（不做系统依赖）。

## 2. 混合模型三档 API（核心设计）

| 档 | 形态 | 适用 | 性能 |
|---|---|---|---|
| **① 脚本组件** | C# 类（`LemonBehaviour`）挂实体，Unity 命名生命周期（ADR-009） | 玩家、Boss、门、剧情触发器等"少量而复杂"的逻辑 | 中（每脚本实例每帧一次托管调用，量 ≤ 数百） |
| **② 批量系统** | C# `IForEachSystem`，C++ 遍历、按块回调 | "中量实体 + 自定义逻辑"（特殊弹幕、自定义怪 AI 变体） | 高（每块一次调用，块 ≥ 64 实体） |
| **③ 原生系统** | C++ 系统 + 数据组件，C# 只**配置/订阅事件** | 万级怪海/弹幕/粒子的热路径 | 峰值（零跨语言） |

**心智模型**："像 Unity 一样挂脚本起步；哪里慢，就把哪里降档为批量系统或原生配置。"三档共享同一组件数据（C# 经 slice 读写 C++ 组件），迁移是搬代码不是改架构。

### 2.1 脚本组件（档①）——LemonBehaviour，Unity 命名级对齐（ADR-009）

```csharp
public abstract class LemonBehaviour        // ≈ Unity / Prowl2D 的 MonoBehaviour
{
    public GameObject gameObject { get; }   // 挂载宿主（Entity 句柄的门面正名）
    public Transform transform { get; }     // 变换视图（见 §3）

    // 生命周期：Unity 原名（语义差异见 §3.2 对齐清单）
    protected virtual void Awake()      {}  // 池取用后重置（≙ Unity Awake + Prowl2D OnAddedToScene）
    protected virtual void OnEnable()   {}
    protected virtual void Start()      {}  // 所有 Awake 之后统一跑（查找依赖安全）
    protected virtual void Update()     {}  // 每逻辑帧 60Hz 固定步长（≙ Unity FixedUpdate 语义，§3.2）
    protected virtual void LateUpdate() {}  // 同一 tick 末尾（相机跟随等脚本间收尾）
    protected virtual void OnDisable()  {}
    protected virtual void OnDestroy()  {}
    // 触发器与事件（来自事件队列，批量派发）
    protected virtual void OnTriggerEnter(GameObject other) {}
    protected virtual void OnTriggerExit(GameObject other)  {}
    protected internal virtual void OnGameEvent(in GameEvent e) {}
}
```

- C++ 侧每个含脚本的实体挂一个 `ScriptBox {int32_t typeId; GCHandle token;}` 组件；`ScriptRegistry` 维护 typeId → C# 工厂。**ScriptBox 不入 ComponentRegistry**（M3 落地决策：桥运行时态入注册表会进 StateHash/序列化，破坏回放与 .scene 语义——作为普通组件挂 Scene 层）。
  > **M4.4 落地（脚本序列化格式定稿）**：ScriptBox 扩为 `{typeId, flags, scriptGuid,
  > className[24]}`（Scripting/ScriptBox.h，从 ScriptHost.h 拆出轻量包含）；`.scene`
  > 实体附加 `"script": {"guid": <u64>, "class": "<TypeName>"}` 成员——**typeId 注册序
  > 不持久**（代码增删即漂移），className 是持久键，装载后由宿主按名解析（编辑器
  > `lemon_behaviours_list` 导出类型名表；`--script` 装配，EnterPlay 统一 Attach）。
  > scriptGuid 供资产侧追踪/热重载目标（0 = 未关联 .cs 资产，仅类名装配）。
  >
  > **M6a 批⓪ 落地（多脚本）**：ScriptBox 扩为内嵌定长槽数组 `ScriptBox{uint32 notified
  > （实体级销毁通知位）; uint8 count; ScriptSlot slots[8];}`，槽 = 旧单槽同构
  > `{typeId, flags, scriptGuid, className[24]}`（40B/槽，disabled 位槽级、销毁通知
  > 位实体级——恰好一次语义 F-08.2 不变）。`.scene` schemaVersion 1→2：实体附加复数
  > `"scripts": [{"guid", "class"}, ...]`；读侧双读（复数优先/旧单数兼容，旧 prefab
  > 不走迁移链靠双读隐式升级）；`.scene` Load 走迁移链 v1→v2（03 §13 首例）。
  > **同实体同类型唯一**（批⓪ 决策 4：入口禁止、格式宽容——加载遇同名重复保序留
  > 首见 + 告警；每实例参数落地时再评估放开）。
- **调度（SceneDispatcher 位掩码方案，Prowl2D 已验证，ADR-009）**：
  - 注册期反射**一次**算出每类型 override 集 → 位掩码（Awake/Update/… 各占一位）；**未 override 的生命周期零成本**（整类实例直接跳过）；
  - dense 数组：活动 ScriptBox 按 typeId 分桶连续存放（挂载即池），同类型委托连续调用（icache 友好）；增删只入队，**每帧至多一次重排**；
  - `[ExecutionOrder]`（Prowl2D/Unity 同名特性）映射为管线内排序标签（同类型实例间排序用）；
  - **一帧固定序（M3 落地，ADR-010 D1）：Start/Update（档①）→ 档② 批量 → LateUpdate**——域线程单线程执行，序即确定性；每帧一次往返实测 ~66µs（macOS，05 §预算内）；
  - spike-03 的 `MethodInfo.Invoke`（0.057µs/call，最差情况基线）在 M3 换为按位掩码缓存的强类型委托直调。

### 2.2 批量系统（档②）

```csharp
public interface IForEachSystem
{
    Query Query { get; }                        // 声明读/写组件集
    void ForEach(ref readonly Chunk chunk);     // C++ 一次遍历，按块回调；chunk 提供组件 span
}
// 用户示例：自定义"环绕玩家的回旋镖"
public sealed class BoomerangSystem : IForEachSystem
{
    public Query Query => Query.With<Projectile, Transform2D>().Write<Transform2D>();
    public void ForEach(ref readonly Chunk c)
    {
        var pos = c.Span<Transform2D>();  var pr = c.Span<Projectile>();
        for (int i = 0; i < c.Length; i++)     // 块内纯托管循环，无逐次 P/Invoke
            pos[i].pos = Orbit(playerPos, pr[i].angle);
    }
}
```

- 执行点 = 系统管线 #14（`CSharpBatchSystem`），Query 在注册时翻译为 EnTT 视图条件。
- 预算护栏：单系统超 1.5 ms 连续 30 帧在 Console 警告并建议降档③。

### 2.3 原生系统（档③）

用户不改 C++：把行为表达为**数据**（Behavior 组件配置 + Team 表 + 导演波次表），仅订阅事件：

```csharp
Director.OnWaveStart += w => Hud.ShowBanner(w.Name);
Events.Subscribe<EntityHit>(e => { if (e.Target == player) Cam.Shake(0.2f); });
```

## 3. Lemon.SDK 门面（C# API 草案，Unity 命名级对齐 ADR-009）

```csharp
namespace Lemon;
public static class Engine   { public static Version Version; public static void Quit(); }
public static class Time     { public static float Delta { get; }  // 当前逻辑步 dt（固定 1/60）
                               public static float Scale { get; set; } }  // ≈ timeScale
public static class Input    { public static bool Down(Key k); public static Vec2 Axis(string name);
                               public static bool Pressed(ActionId a); }        // 虚拟轴/动作映射
public static class Audio    { public static SoundHandle Play(string asset, in AudioOpts o); }
public static class Assets   { public static AssetRef<T> Load<T>(Guid id) where T : class, IAsset; }
public static class Scene    { public static void Load(string name);
                               public static GameObject Instantiate(string prefab, in Vec2 pos);  // ≈ Unity Instantiate（内部走池）
                               public static GameObject FindWithTag(string tag);
                               public static int FindGameObjectsWithTag(string tag, Span<GameObject> out_); } // 零分配重载
public static class Events   { public static void Subscribe<T>(Action<T> h) where T : struct, IGameEvent; }
public static class Profiler { public static void Begin(string zone); public static void End(); } // Tracy 联动

public interface IComponent {}               // 数据组件（struct）与 LemonBehaviour 的共同标记（双路由用）

public readonly struct GameObject : IEquatable<GameObject>   // 门面正名（ADR-009）；底层即 EntityHandle(uint64) 的别名
{
    public bool Alive { get; }
    public Transform transform { get; }              // 读写经 Hierarchy/Transform2D 合成（03 §2）
    public string Tag { get; set; }  public int Layer { get; set; }
    public bool CompareTag(string tag);
    public void SetParent(GameObject parent, bool worldPositionStays = true);
    public int childCount { get; }   public GameObject GetChild(int i);

    // 双路由（ADR-009；M6a 批⓪ 已落地）：数据组件与脚本组件同一个 API 面。C# 不允许
    // 仅按泛型约束重载 → 门面统一 where T : notnull，运行时按 typeof(T) 是否
    // LemonBehaviour 分路（值组件经 ComponentTable.Type→id 反查 + 开放泛型策略类
    // 反射绑定一次的强类型读——零装箱；低频 API，热路径仍推 TryGetComponent/Chunk）
    public void AddComponent<T>() where T : notnull;    // T:IComponent struct → op2 命令；T:LemonBehaviour → op4（幂等 get-or-add）
    public bool TryGetComponent<T>(out T c) where T : unmanaged;   // 值组件快路径（无装箱）
    public T? GetComponent<T>() where T : notnull;      // T:LemonBehaviour → 实例引用（未挂 = null）；值组件 → 拷贝（缺 = throw）
    public bool RemoveComponent<T>() where T : notnull; // T:IComponent → op3；T:LemonBehaviour → op5（单实例 OnDestroy + 退订）
    // public T GetComponentInChildren<T>() —— 仍缓：无消费者，不扩面（批⓪ 决策）
    // 警告文化：文档标注"GetComponent 系列是语法糖，逐帧调用违反性能文化（与 Unity 官方'缓存 GetComponent'建议同理），
    // 热路径请用 Chunk span（§2.2）"
}

public readonly struct Transform               // 视图结构：逐属性访问是语法糖，热路径用 Chunk span
{
    public Vec2 position { get; set; }         // 世界坐标（写 = 自动换算本地）
    public Vec2 localPosition { get; set; }
    public float rotation { get; set; }        // 弧度；rotationDeg 提供度数版（Unity 心智）
    public Vec2 localScale { get; set; }
    public Transform parent { get; }
}
```

> **多脚本/实体（M6a 批⓪ 已落地，2026-09-24；原 M5 条目 2026-09-22 登记）**：每实体
> `ScriptBox` 内嵌 8 槽 `ScriptSlot` 数组（同类型唯一——见 §3.2 不对齐清单）；`.scene`
> schema v2 `scripts: []`（旧单数读侧兼容）；Inspector Script 段列表化（逐槽 combo/
> 移除）；SDK 门面 `AddComponent<LemonBehaviour>` 双路由已通（op4 幂等 get-or-add）。
> 调度侧零改动（dense 按 typeId 分桶本按活动实例遍历）。跨类型 Update 执行序 =
> **注册序**（`GameMain.Configure` 内 `Register<T>` 顺序；`[ExecutionOrder]` 桶间
> 排序，同 Order 按注册序），槽序只影响 Inspector 展示与序列化键序。

### 3.1 协程替代：async/await + C++ 定时器（ADR-009 基调——表面像 Unity，机器走 C++）

- Unity `StartCoroutine/WaitForSeconds` → C# `async/await` + **Lemon 主线程同步上下文**（Prowl2D `Tasks/MainThreadContext` 思想）：
  - `await LemonAwait.Delay(1.5f)` / `LemonAwait.NextFrame()` / `LemonAwait.WaitUntil(fn)`；
  - 底层是 **C++ 定时器队列**（03 §11 `TimerFire` 事件）经事件队列帧末回调续跑——托管侧只有状态机，无轮询、无每帧分配；
  - **限定低频胶水**（开场演出、对话时序、教学流程）；热路径禁用（异步状态机属托管分配，GC 纪律 §5 红字覆盖）。

### 3.2 对齐 / 不对齐清单（期望管理，写进 SDK 文档首页与智能提示）

**对齐（命名与形态一致）**：

- `GameObject` / `Transform`（position/rotation/scale/parent）/ `Tag/Layer/CompareTag` / `SetParent(worldPositionStays)` / `GetChild/childCount`；
- `AddComponent / GetComponent / TryGetComponent / GetComponentInChildren`（双路由）；
- 生命周期 `Awake/OnEnable/Start/Update/LateUpdate/OnDisable/OnDestroy/OnTriggerEnter/OnTriggerExit`；
- `Scene.Instantiate/Destroy`、`FindWithTag`、`Time.timeScale`、`SortingLayer/SortingOrder`（02 §3.1）；
- `Time.DeltaTime / Time.Elapsed / Time.FrameCount`（M5 清障① 已落地：固定步长 dt / 局累计秒 / 局帧号；域线程 TickBody 首行推进，档①②同帧同值，进 Play/换域归零）。`Time.Scale`（M5 批① 已落地：经 native 表 `get/setTimescale` 读写 `World::TimeScale`——C++ `Step` 内缩放 dt、clamp [0,8]，DeltaTime 拿到的即缩放值，=0 冻结暂停但 FrameCount 照推）。

**不对齐（语义差异，显式声明）**：

- **`Update` = 60Hz 固定步长**（≙ Unity FixedUpdate 语义）；**无 FixedUpdate、无变步长 Update**——渲染插值由引擎做（01 §2）；
- **无协程 / Invoke / SendMessage / BroadcastMessage** → `async/await`（§3.1）、`Events.Subscribe`、`OnGameEvent`；
- **Enter Play 不换域（与 Unity 默认相反），"局"边界由引擎显式清**：Play↔Edit 切换不重载程序集（热重载连续性），编辑器 EnterPlay 调 `lemon_play_reset` **硬清 behaviour 实例表 + `Events` 订阅/待发/计数 + 热重载待恢复包**（类型注册表保留；Time 同刻归零）。脚本侧含义：**构造器里的 `Events.Subscribe` 每局恰好一次**、实例字段每局全新——静态字段（`static`）仍跨局存活（刻意：换装/诊断计数用；勿存"局内"状态）。漏清的实测症状（批④后修）：同实体双实例双 tick、构造器订阅逐局累积（击杀掉落翻倍）。
- **无 ScriptableObject** → 资产即 JSON + `AssetRef<T>`（06）；
- **无刚体/关节物理回调** → 触发器与命中走查询层事件（03 §5/§6）；
- `GetComponent` 系列是语法糖（热路径用 Chunk span，§2.2）；
- **脚本结构变更当帧读旧、帧首生效**（M3 落地语义）：Create/Destroy/AddComponent/RemoveComponent 全走命令缓冲，在下一帧 Essential（DestroyCommit 同拍）应用——脚本当帧新建的实体下一帧可见，与两阶段销毁同语义（03 §2）；占位 id（高位标记）仅在本批命令内可解析。**帧边界推论（M6a 批⓪ 门面）**：`AddComponent<LemonBehaviour>` 同帧 `GetComponent` = null（命令未应用）；门面侧幂等 get-or-add 已含待决命令去重（重复调用不双挂）；
- **同实体同类型脚本唯一**（M6a 批⓪ 决策，与 Unity 相反）：同类型重复挂载在三个入口被拦——Inspector 同名置灰、`AddComponent<LemonBehaviour>` 幂等 get-or-add、`Behaviours.Attach` 红字断言（真泄漏响亮）；格式宽容（scripts[] 可存重复项，加载保序留首见 + 告警）。无每实例参数下重复表达力为零、StateBag 键 `(class, entity)` 会冲突——每实例字段（05 §5）落地时再评估放开；
- **`RemoveComponent<LemonBehaviour>` = 卸单槽单实例**（op5，M6a 批⓪）：OnDestroy + 实例级订阅退订 + 槽保序移除（区别于实体销毁时按实体清全量）；自卸（`RemoveComponent<自己的类型>`）合法，帧首生效（≈ Unity `Destroy(this)`）；
- **`Anim.CrossFade(fade)` 无姿态混合**（M6a 批①）：帧动画本质是换帧不是姿态——fade 语义 = 倒计时延迟切段（非 loop 当前段提前收尾立即切），不做双精灵 alpha 混合。`Anim.Play/Queue/Pause/Resume/IsPlaying/Queued` 为静态类方法（非 Unity `GetComponent<Animator>()` 实例面——Animator2D 是数据组件，切段 = 纯字段写零 C ABI）；Queue 是 Lemon 特有（受击段播完自动回行走的标准组合拳）；
- 脚本异常自动隔离禁用（§7），引擎永不崩。

### 3.3 Unity → Lemon 移植指南（一页）

1. `MonoBehaviour` → `LemonBehaviour`；2. `FixedUpdate` 代码并入 `Update`（同为固定步长）；3. `StartCoroutine/WaitForSeconds` → `async/await` + `LemonAwait.Delay`；4. `SendMessage/UnityEvent` → `Events.Subscribe<T>`；5. `Vector3` → `Vec2`（transform.position 直接同形）；6. Inspector 序列化字段 → 同为 public 字段（`[ShowInInspector]`，05 §5）；7. Prefab 概念一致（PrefabLink + override，03 §2）；8. `Physics2D.OverlapCircle/Raycast` → 查询层同款 API（03 §5）。

**预期**：典型脚本移植 ≈ 改基类名 + using + Vector3→Vec2。

- 全部 API 落在两条通道上：低频控制类（Scene/Assets/Instantiate）走句柄 + GUID；数据类（Get/Set/Chunk）走 blittable slice。
- **不自动生成整套绑定**（排除 XPremo 式全家桶）：SDK 手写（API 面小而稳，~150 个导出），后期用 Source Generator 只生成"组件 struct ↔ 注册表"的镜像（Inspector/序列化共用，见 05 §5）。
  **分期落地（ADR-010 D4）**：M3 已交付 headless 核心子集（Chunk / 组件 CRUD / 事件 drain+push / RNG / Time / Log / SceneOps / LemonBehaviour 生命周期 / 异常隔离，实测 1264 checks）；Input / Audio / Assets / Instantiate(prefab) / LemonAwait / Profiler 随 M4/M5 消费者落地。
- **native 函数表**（`NativeApiVtable` ↔ `NativeApi.cs` 逐字节一致；表尾追加 = 旧宿主零扰动，SDK 侧判空）：
  M4.4 追加 4 项（GetInput/SpriteOfGuid/SpawnSprite/InstantiatePrefab）；**M5 批① 追加 3 项**——
  `get/setTimescale`（Time.Scale ↔ World）+ `rtUiSet`（Lemon.Ui.Set → World.RtUi 定长 8 槽，
  GameView Play 叠加画，M8 打包 HUD 复用）；**M5 批④ 追加 8 项**——存档 4 项
  （`saveSet/saveGetLen/saveGet/saveFlush` → World.Saves 内存 KV + 宿主 IO 钩子落盘）+
  HUD 完整版 4 项（`rtUiClear` 删行 / `rtUiSetEx` 着色版（ABGR）/ `uiCards` 三选一
  显隐 / `uiCardPick` 消费式选择回读）。SDK 新面：`Lemon.Save`（Set/Get/
  SetString/GetString/HasKey/Flush，06 §10 形状裁剪）、`Lemon.Ui` 扩
  （着色 Set/Clear/ShowCards/HideCards/CardPick）、`GameObject.From(EntityHandle)`
  （事件回读 src/dst 组件）、`InputButton.Confirm`（bit5 = R 键）。
  **批④后修④（2026-09-24）**：`Ui.ShowDialog(title, okLabel)`——单按钮对话框
  （卡片通道复用，B/C 留空即不渲染按钮；死亡复活/结算重开等确认型交互首选，
  点击或数字键 1 → `CardPick() == 0`）；同批修复 `CopyUtf8` 无条件回退吃 CJK
  结尾串末字的潜伏 bug（改为仅真截断时回退，孤立前导字节一并去）；**脚本实体
  死亡不被引擎自动销毁**（03 销毁两阶段注记）——复活类逻辑在脚本侧自理。
  **M6a 批① 追加 2 项**：`fxPopup`（飘字 → World.Fx 飘字池）+ `fxBar`（世界
  血条 → World.Fx 键控槽；06 §8 恒定原则——恒走 sprite 管线不进 UI 框架）。
  SDK 新面：`Lemon.Anim`（T3d 批①②追加：SetSet/SetController 绑定写 +
  SetParam/GetParam/Trigger 参数黑板——trigger 消费即清在引擎图评估内；事件侧
  GameEvent 尾加 AnimFrame/AnimFinished 两值（帧打点/段末，m.User=事件 id、
  m.UserArg=clipId）。基础面 Play/Queue/CrossFade/Pause/Resume/IsPlaying/Queued/
  ClipId——纯 Animator2D 字段读写，clipId = clip 资产 GUID 低 32 位客户端自算，
  换段语义见 03 §8.1）、`Lemon.Fx`（Text 飘字/数字 + Bar 世界血条——呈现层专用
  不入 StateHash，池化上限飘字 256/血条 128 最老者淘汰）。
  **A 档补间（2026-09-28 用户插入项）追加 4 项**——`tweenTo`（建补间 → 单调
  句柄；0 = 失败：实体亡/组件缺/字段名未命中/类型白名单外，warn-once）/
  `tweenKill`（同实体同字段，field 空 = 该组件全部）/`tweenKillEntity`/
  `tweenAlive`（低频轮询）→ World.Tweens，TweenSystem 推进（语义见 03 §8.3：
  **存活补间拥有字段**——同帧脚本写被覆写、同字段新建顶替、Kill/完成后归还）。
  SDK 新面：`Lemon.Tween`（通用 `To<T>(g, 字段名, float/Vec2/uint, dur, ease,
  mode)` + Position/Scale(+uniform)/Rotation/Color/Alpha 糖 + Alive/Kill/KillAll；
  字段名 = Inspector/序列化同名，白名单 Float/Vec2/UInt32 颜色）；事件侧
  GameEvent 尾加 `TweenFinished`（Once 完成恰一次，userArg = 句柄）。
  **字符串跨界一律 UTF-8**（SDK `CopyUtf8`：ASCII 快路径零分配 + 多字节不切断——
  批④ 前逐 char 截字节只对 ASCII 正确，中文 HUD 会乱码）。

## 4. 事件队列桥（C++ → C# 批量派发）

- 帧末一次 `lemon_events_drain(ptr, capacity)`：C# 侧 `NativeQueue<EventPacket>` 拷走整段（`unsafe` 固定指针 memcpy），随后托管内分发到 `Events.Subscribe<T>` 与脚本组件 `OnGameEvent`。
- 数量护栏：单帧事件 > 50k（压测 A 死亡潮）时分批派发并跳过无订阅者类型（订阅表预筛）。
- **事件回调与 Update 同 native 窗口（M5 批②）**：`#16 ScriptEventDispatch` 派发期间 `g_world/g_scene` 照常置位——订阅方在回调内可调 `Ui.Set/Time.Scale/Instantiate`（此前窗口只盖 `TickBatch`，回调内 native 调用会静默空转）。落地样例：`TestScript.WaveBannerBehaviour`（`Events.Subscribe(GameEvent.WaveStart)` → `Ui.Set` 波次横幅，payload 约定见 03 §8/§11）。

## 5. 边界成本预算与 GC 纪律

| 操作 | 预算 | 措施 |
|---|---|---|
| 单次 P/Invoke（blittable 参数） | ~20–40 ns | 批量 API 把每帧调用次数压到两位数 |
| Chunk 回调（块 ≥ 64 实体） | 摊薄 < 1 ns/实体 | 块内托管循环零跨界 |
| 事件派发（万条） | ≤ 0.8 ms | 预筛 + struct 事件（无委托分配，订阅走静态表） |
| 脚本组件 Update（500 实例） | ≤ 0.5 ms | 位掩码跳过未 override；同类型委托连续调用；参数 struct 传 |

GC 纪律（引擎侧强制 + 面板验证）：

1. 脚本组件与批量系统内**禁 `new`（class）**——SDK 提供 `PooledList<T>`/`TempList<T>`（帧清空）；
2. 事件与组件访问全 struct 化（`readonly struct`/`in` 参数）；
3. F3 面板显示每帧托管分配字节数（GC 分配 API 采样），> 0 热路径直接红字；
4. Gen2 无增长纳入压测 A 验收（00 文档红线）。

## 6. 热重载（编辑器内改 C#，2 秒生效）

> **已由 [ADR-010](../ADR/ADR-010-M3-Scope-Thread-RNG.md) 修订（2026-09-19）**：完整热重载
> （本节全部内容）推迟至 **M4 编辑器**落地——热重载的真实消费者是编辑器 Play 循环，M3
> headless 无从验收。M3 实现 DomainManager 的 Load/Unload 命令并跑「装载→运行→卸载→
> WeakReference 确认回收」自检（§2 线程模型同样按 ADR-010 定稿：域线程统一执行）。

方案（Luma ScriptLoadContext 同款，补强状态迁移）：

1. 文件监视（资产库 FileWatcher）发现 `.cs` 变更 → 调 `dotnet watch build`（M4 前用外置 dotnet CLI；后期内嵌 Roslyn `CSharpCompilation`，Prowl `RoslynScriptBackend` 作参考实现）；
2. 新程序集经 `ScriptLoadContext`（Collectible ALB + 影子复制 DLL）加载；
3. **状态迁移协议**：旧脚本实例逐个调 `OnHotReloadOut(ref StateBag)`（用户把可保留状态写进 bag）→ 换类型注册 → `OnHotReloadIn(in StateBag)` 重建；无法迁移的状态默认丢弃并 Console 提示；
4. 旧 ALB `Unload()` + 强制 GC ×2 + 断言释放（泄漏检测：ALB 卸载失败红字，防"热重载十次后崩"）；
5. 编辑器 Play 中热重载可用（改完立刻看到效果，迭代核心体验）。

时间预算：编译 + 换装 ≤ 2 s（中型项目），超时提示走整域重建。

> **M4.5 落地注记（2026-09-20）**：本节按 ADR-010 A 线（整域重建）交付，实测
> 编译+换装+重装配 **1.22–1.24s**（Play/Edit 双态各一例，含外置 dotnet build 增量
> 编译）；旧 ALC 每次未回收计一次泄漏（红字告警 + Profiler 常驻计数，B 线探针转绿
> 后自然归零）。**StateBag 字段粒度白名单**（实现 = `Lemon.SDK/StateBag.cs`）：
> 可迁移 = 基元值类型 / 枚举 / `Lemon.Vec2`（SDK 常驻 ALC 身份，装箱值不携带旧域
> 类型）；不可迁移 = 用户自定义 struct（类型定义在旧域，装箱即 pin）与一切托管对象
> 引用。`TryGet` 类型不匹配/缺失 = 丢弃（false），不抛异常不阻断换装。时机：
> `OnHotReloadOut` = 卸载前最后一次调用（旧域）；`OnHotReloadIn` = 新实例
> Awake/OnEnable 之后、首次 Start/Update 之前。触发链：Game/ 源码 FileWatcher
> （500ms 轮询 + 0.4s 防抖 + obj/bin 排除，防 build 自写自触发）→ dotnet build →
> `lemon_dm_reload` → 编辑器按 className 重装配（Play 世界原位换实例 + StateBag
> 恢复；Edit 世界刷新 typeId）。换装与上次帧间 pending 的 SceneOps 命令会被
> Reset 清弃（可接受：换装瞬间的排队结构命令丢失，不坏档）。

## 7. 调试与错误报告

- **断点调试**：CoreCLR 原生能力——编辑器以 debug 模式启动脚本域，Rider/VS 附加进程即可断点（Luma README 实证同路径）；SDK PDB 随引擎分发。
- **异常隔离**：每个脚本组件/批量系统的托管调用包 `try/catch`：首次异常 → 红字 Console + 该组件本帧跳过；同组件连续 60 帧异常 → 自动禁用（`ScriptBox.disabled`）并保留现场（暂停 Play 是可选项）。**引擎进程永不因脚本异常崩溃**。
- **崩溃转译**：托管未捕获异常 → 转结构化错误进日志（含脚本名/行号），不穿越 C ABI 抛 C++ 异常。

## 8. 脚本工程与发布

- 项目内 `Game/` 目录 = 标准 `csproj`（SDK 引用 `Lemon.SDK.dll`）；用户可用任意 IDE/编辑器。
- 发布时 `Tools/packager` 把用户 DLL + 依赖合并进数据包；**AOT 可选**（NativeAOT 编译用户程序集提升启动与防反编译——CoreCLR 解释加载为主路径，AOT 为发布选项，M7 评估）。

## 9. 与参考实现的对照（本册）

| 项 | 来源 | 处置 |
|---|---|---|
| hostfxr 加载/GCHandle 函数表/ALB 卸载 | Luma `Scripting/CoreCLRHost.*`、`Scripting/binding/Luma.SDK/ScriptLoadContext.cs` | **直接移植**（MIT），函数表按我们三档模型裁剪 |
| YAML 属性桥 | Luma `SetPropertyFn/InvokeMethodFn` | **不采用**——我们用 blittable struct 直传，避免字符串序列化热路径 |
| 组件生命周期约定 | yami `event.ts:775 ScriptManager` + roadmap Phase 1 结论 | 借鉴接口形状（两阶段 start/update + 事件分发）——命名按 ADR-009 升级为 Unity 原名 |
| 脚本调度（位掩码零成本跳过 / dense 数组 / 每帧至多一次重建 / ExecutionOrder） | Prowl2D `Prowl.Runtime/GameObject/SceneDispatcher.cs`（自有，已验证） | 思想移植：C++ ScriptBox 池 + 注册期位掩码（§2.1） |
| C# API 命名面（GameObject 正名 / LemonBehaviour / Unity 生命周期 / AddComponent 双路由） | Prowl2D `Prowl.Runtime/GameObject/{GameObject,MonoBehaviour}.cs` + Unity | 命名级对齐（ADR-009），实现自有 |
| async/await 主线程上下文（协程替代） | Prowl2D `Prowl.Runtime/Tasks/MainThreadContext*` | 思想移植 + C++ 定时器队列底座（§3.1） |
| Roslyn 编译集成 | Prowl `Prowl.Editor/Projects/Scripting/RoslynScriptBackend.cs` | M3 后期内嵌编译时参考（MIT） |
| 调试/异常策略 | Luma（断点）+ 自研（异常隔离/禁用） | 混合 |
