// TestScript — 用户脚本程序集样例（M3-2b 域生命周期 + M3-3 批量系统档②）。
// 装配入口约定：public static class GameMain.Configure()（DomainManager 加载后调用）。
using Lemon;
using Lemon.Interop;

public static class GameMain
{
    public static void Configure()
    {
        Lemon.Scripting.Register(new AddVelocitySystem());
        Lemon.Scripting.Register(new AgingSystem());
        // M3-4：事件订阅 → 收到 Hit 时回推 Custom（验收事件双向）
        Lemon.Events.Subscribe(Lemon.Interop.GameEvent.Hit, m =>
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 42, m.Src, m.Dst));
        // M3-5：档① 注册
        Lemon.Behaviours.Register<CountingBehaviour>();
        // M4.4 SDK 增量验收（编辑器 --script 冒烟专用；此处仅注册不装配，
        // 对既有 script-tests 断言零影响）
        Lemon.Behaviours.Register<SpawnerBehaviour>();
        Lemon.Behaviours.Register<InputMoverBehaviour>();
        // M5 清障①：Time API 验收（表尾追加——既有 typeId 零扰动）
        Lemon.Behaviours.Register<TimeProbeBehaviour>();
    }
}

/// <summary>M4.4：Instantiate.Spawn + Assets.SpriteOf（GUID→导入 sprite）刷怪验收。
/// 编辑器冒烟播种实体的 ScriptBox.className = "SpawnerBehaviour"。</summary>
public sealed class SpawnerBehaviour : Lemon.LemonBehaviour
{
    private const string kSpriteGuid = "5bd31a7c10e9f2c8"; // 冒烟项目预置 .meta 固定 GUID
    private int _tick;
    private uint _spriteId;

    protected override void Start()
        => _spriteId = Lemon.Assets.SpriteOf(kSpriteGuid);

    protected override void Update()
    {
        if (++_tick < 5 || _tick > 40) return; // 第 5..40 帧每帧 1 只（冒烟断言 36 只）
        var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();
        var at = new Lemon.Vec2(t.Pos.X + 40 + _tick * 2, t.Pos.Y - 20);
        Lemon.Instantiate.Spawn(_spriteId, at);
    }

    // M4.5 热重载状态迁移验收：_tick 入包 → 换装后续跑（总刷怪数不翻倍）
    protected override void OnHotReloadOut(Lemon.StateBag bag)
    {
        bag.Set("tick", _tick);
        bag.Set("spriteId", _spriteId);
    }

    protected override void OnHotReloadIn(Lemon.StateBag bag)
    {
        if (bag.TryGet("tick", out int t)) _tick = t;
        if (bag.TryGet("spriteId", out uint s)) _spriteId = s;
    }
}

/// <summary>M4.4：Input 语义读取验收（GameView 聚焦时 WASD 播放进 Play World）。</summary>
public sealed class InputMoverBehaviour : Lemon.LemonBehaviour
{
    public const float Speed = 240f;

    protected override void Update()
    {
        var v = gameObject.GetComponent<Lemon.Interop.Transform2D>();
        v.Pos = v.Pos + Lemon.Input.Axis * (Speed * (1f / 60f));
        gameObject.SetComponent(v);
    }
}

/// <summary>每实体速度 X += dt*60（dt=0.25 时每步恰好 +15，验收断言用精确值）。</summary>
public sealed class AddVelocitySystem : IForEachSystem
{
    public string Name => "AddVelocity";
    public Query Query => Query.With<Velocity>();
    public unsafe void ForEach(in Chunk chunk)
    {
        var v = chunk.Span<Velocity>();
        for (int i = 0; i < chunk.Length; i++) v[i].V.X += 60f * 0.25f;
    }
}

/// <summary>双组件查询（Velocity AND Projectile）：投射物 age += dt。</summary>
public sealed class AgingSystem : IForEachSystem
{
    public string Name => "Aging";
    public Query Query => Query.With<Projectile, Velocity>();
    public unsafe void ForEach(in Chunk chunk)
    {
        var p = chunk.Span<Projectile>();
        for (int i = 0; i < chunk.Length; i++) p[i].Age += 0.25f;
    }
}

/// <summary>档① 验收：生命周期计数经事件回报（Custom user 100+n）。</summary>
public sealed class CountingBehaviour : Lemon.LemonBehaviour
{
    private int _updates;

    protected override void Awake()
        => Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 100, default, default);

    protected override void Start()
        => Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 101, default, default);

    protected override void Update()
    {
        if (++_updates == 2) {
            // 第 2 帧：读 Transform2D（NativeApi 语法糖）+ 自毁（结构命令）
            var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, (ushort)(200 + (int)t.Scale.X),
                              default, default);
            gameObject.Destroy();
        }
    }

    protected override void OnDestroy()
        => Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 102, default, default);
}

/// <summary>M5 清障①：Time API 验收。固定步长 dt=0.25 下三帧回报
/// Custom 250（DeltaTime×1000）/ 500（Time×1000，帧 2）/ 3（FrameCount），
/// 第 3 帧自毁（复用 CountingBehaviour 的"测完即走"模式，不留活实体）。</summary>
public sealed class TimeProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void Update()
    {
        // 0.25f 二进制精确 → ×1000 无舍入，C++ 侧可精确断言
        if (Lemon.Time.FrameCount == 1)
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(Lemon.Time.DeltaTime * 1000f), default, default);
        else if (Lemon.Time.FrameCount == 2)
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(Lemon.Time.Elapsed * 1000f), default, default);
        else if (Lemon.Time.FrameCount == 3) {
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)Lemon.Time.FrameCount, default, default);
            gameObject.Destroy();
        }
    }
}

// ---- M3-2b 域生命周期入口（保留）----
public static class TestScript
{
    private static double s_accum;

    public static double Tick(float dt)
    {
        s_accum += dt;
        return s_accum;
    }
}
