# C# 脚本

玩法逻辑写 C#（.NET 10），工程就是项目里的 `Game/`（标准 csproj，引用
`Lemon.SDK.dll`——向导自动锚本机路径）。编辑器内改代码**热重载**约 2 秒生效。

## LemonBehaviour 生命周期

脚本组件继承 `LemonBehaviour`，挂在实体上（Inspector 的 Script 槽，同实体同类型唯一；
每实体最多 8 个脚本）：

```csharp
using Lemon;

public sealed class PlayerMovement : LemonBehaviour
{
    private Stats stats;

    protected override void Start()   // 首帧 Update 之前统一跑
        => stats = gameObject.GetComponent<Stats>();

    protected override void Update()  // 60Hz 固定步长（≙ Unity FixedUpdate 语义）
    {
        if (!gameObject.Alive) return;
        // ... 每帧逻辑
    }
}
```

| 回调 | 时机 |
|---|---|
| `Awake` / `OnEnable` | 挂载/启用 |
| `Start` | 首帧 Update 前 |
| `Update` | 60Hz 固定步长（无 FixedUpdate——模拟即固定步） |
| `LateUpdate` | 同 tick 末尾 |
| `OnDestroy` | 移除/销毁 |
| `OnHotReloadOut/In(StateBag)` | 热重载状态迁移（仅值类型可入包） |

## GameObject 门面

| API | 语义 |
|---|---|
| `gameObject.Alive` | 句柄有效性自查 |
| `TryGetComponent<T>(out T)` / `GetComponent<T>()` | 读组件（拷贝语义，低频糖） |
| `gameObject.SetComponent(in T)` | 整写回（改字段的标准姿势：读出来 → 改 → 整写） |
| `AddComponent<T>()` / `RemoveComponent<T>()` | 加/删组件（结构命令，**帧首应用**——同帧查不到） |
| `gameObject.Destroy()` | 销毁实体（帧首应用） |
| `GameObject.From(handle)` | 事件回调里从句柄构门面 |

> **结构命令两拍**：Add/Remove/Destroy 当帧入队、下一帧帧头应用——同帧
> `GetComponent` 查不到刚加的组件，这是语义不是 bug（[光照页](08-lighting.md)
> 的 `SetGlobalAmbient` 两拍时序同族）。

## 档位模型：两通道纪律

C++/C# 边界只有两条通道（依赖向下的硬纪律）：

1. **批量 API**——逐实体操作（GameObject 门面）与批量系统。
2. **事件队列**——引擎 → C# 的批量派发（`Events.Subscribe` / 实例级
   `Subscribe`，后者 OnDestroy 自动退订）。

热路径（万级实体遍历）用**档② 批量系统**：实现 `IForEachSystem`（Query +
ForEach(Chunk)），C++ 一次遍历、按块回调、原地写回零拷贝：

```csharp
public sealed class DriftSystem : IForEachSystem
{
    public string Name => "Drift";
    public Query Query => Query.With<Transform2D, Velocity>();
    public unsafe void ForEach(ref readonly Chunk chunk)
    {
        var t = chunk.Span<Transform2D>();
        var v = chunk.Span<Velocity>();
        for (int i = 0; i < chunk.Length; i++)
            t[i].pos += v[i].v * Time.DeltaTime;
    }
}
```

注册在 `GameMain.Configure`（脚本程序集入口）里完成；系统随机数用
`Scripting.Rng(index, worldSeed)`（引擎子流，保证回放确定性——**别用系统随机**）。
异常隔离：连续 60 帧异常的系统自动禁用。

## SDK 门面总览（Lemon.*）

| 域 | 门面 | 一句话 |
|---|---|---|
| 场景 | `SceneManager` / `SceneOps` | 换场（同步/异步）、实体操作（[场景](06-scenes.md)） |
| 输入 | `Input` | Axis + 六按钮（Up/Down/Left/Right/Attack/Confirm/Pause） |
| 时间 | `Time` | 帧时间/缩放（`Time.Scale = 0` 冻结模拟） |
| 补间 | `Tween` | 字段补间（存活补间拥有字段，在途禁写同字段） |
| 表现 | `Fx` / `Lighting` | 飘字血条（[FX](09-fx.md)）/ 光照（[光照](08-lighting.md)） |
| 界面 | `Ui` / `UI` | 快速 HUD / RmlUi 门面（[界面](10-ui.md)） |
| 音频 | `Audio` | 播放/分组音量（[音频](11-audio.md)） |
| 资产 | `Assets` / `Anim` | GUID 解析、prefab 实例化、动画控制 |
| 数据 | `Save` / `Table` / `Balance` / `StateBag` | 存档 KV / 配置表 / 数值平衡 |
| 宿主 | `Scripting` / `Events` / `GameObject` | 批量系统注册 / 事件 / 实体门面 |

## 确定性

脚本写组件字段即入状态哈希流 ⇒ **可回放**（同输入必同结果）。想让某状态不进
回放（纯表现），用引擎提供的呈现通道（Fx/UI/Audio 天然不入哈希），不要绕。

> 设计依据：[04 C# 分册](../EngineDesign/04-CSharp-Scripting.md)（三档 API/
> 事件队列桥/GC 纪律/热重载）；SDK 本体 = `Engine/Scripting/dotnet/Lemon.SDK/`。
