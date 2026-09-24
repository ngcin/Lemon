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
        // M5 批①：Time.Scale × Lemon.Ui.Set 验收（typeId 4，表尾注册同上约定）
        Lemon.Behaviours.Register<ScaleUiProbeBehaviour>();
        // M5 批②：WaveStart 订阅 → Ui.Set 波次行（typeId 5，表尾注册同上约定）
        Lemon.Behaviours.Register<WaveBannerBehaviour>();
        // M5 批④：存档 + HUD 完整版 + Confirm 位（typeId 6，表尾注册同上约定）
        Lemon.Behaviours.Register<SaveCardsProbeBehaviour>();
        // M11：Awake/OnDestroy 内 native 调用（typeId 7，表尾注册同上约定）
        Lemon.Behaviours.Register<AwakeUiProbeBehaviour>();
        // M15：Subscribe 助手 + Detach 自动退订（typeId 8，表尾注册同上约定）
        Lemon.Behaviours.Register<SubProbeBehaviour>();
        // F-08.2（2026-09-24）：C++ 路径销毁的 OnDestroy（typeId 9，表尾注册同上约定）
        Lemon.Behaviours.Register<CppDestroyProbeBehaviour>();
        // M6a 批⓪ T3：GameObject 统一门面双路由（typeId 10，表尾注册同上约定）
        Lemon.Behaviours.Register<DualRouteProbeBehaviour>();
        // M6a 批①：Anim 状态控制 + Fx 通道（typeId 11，表尾注册同上约定）
        Lemon.Behaviours.Register<AnimFxProbeBehaviour>();
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

/// <summary>F-08.2（2026-09-24）验收：C++ 路径销毁（系统直接 scene.Destroy）也必须
/// 触发 OnDestroy——旧链只有脚本命令路径通知，托管实例/实例级订阅残留到换域。
/// 回报 Custom 177；由 script-tests 从 C++ 侧 Destroy 后对拍恰好一次。</summary>
public sealed class CppDestroyProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void OnDestroy()
        => Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 177, default, default);
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

/// <summary>M5 批①：Time.Scale（native 表往返）× 缩放 dt 链 × Lemon.Ui.Set 验收。
/// Custom user 编码 = 段标签 + 精确值（二进制可精确表示，C++ 侧精确断言）：
/// 帧1 置 Scale=0.5 → 报 300+5；帧2 缩放 DeltaTime 0.25×0.5=0.125 → 报 400+125，
/// 同帧 Ui.Set("xp","LV3 45/120",0.45)；帧3 复位 Scale=1 → 报 600+10 后自毁。</summary>
public sealed class ScaleUiProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void Update()
    {
        if (Lemon.Time.FrameCount == 1) {
            Lemon.Time.Scale = 0.5f;
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(300 + Lemon.Time.Scale * 10f), default, default);
        } else if (Lemon.Time.FrameCount == 2) {
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(400 + Lemon.Time.DeltaTime * 1000f), default, default);
            Lemon.Ui.Set("xp", "LV3 45/120", 0.45f);
        } else if (Lemon.Time.FrameCount == 3) {
            Lemon.Time.Scale = 1f;
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(600 + Lemon.Time.Scale * 10f), default, default);
            gameObject.Destroy();
        }
    }
}

/// <summary>M5 批②：WaveStart 订阅样例（引擎事件 → Game RT UI 的最小闭环）。
/// 收到 WaveStart（P0=波序号 0 起、P1=本波计划总数）→ Ui.Set 波次横幅行；
/// C++ 侧（script-tests TestWaveStartToUi）人工推包后断言 RtUi 槽。
/// Medium 轮 M15 迁移：裸 Events.Subscribe → Subscribe 助手（实例销毁自动退订，
/// 反复挂载不再累积订阅）。</summary>
public sealed class WaveBannerBehaviour : Lemon.LemonBehaviour
{
    public WaveBannerBehaviour()
    {
        Subscribe(Lemon.Interop.GameEvent.WaveStart, m =>
        {
            int index = (int)m.P0;
            int planned = (int)m.P1;
            Lemon.Ui.Set("wave", $"WAVE {index + 1} x{planned}", -1f);
        });
    }
}

/// <summary>M5 批④：存档通道 + HUD 完整版（着色槽/Clear/卡片/CardPick 消费语义）
/// + InputButton.Confirm 验收（typeId 6，表尾注册同上约定）。
/// 帧1 Save.SetString + Ui.Set 着色 + Ui.Set/Clear；帧2 读回（含原始字节档与
/// HasKey）+ ShowCards；帧3 CardPick 消费 ×2（C++ 帧间置 pick=1）+ HideCards +
/// Input.Confirm + Save.Flush（无钩子宿主 = 安全 no-op）；帧4 ShowDialog（批④后
/// 修④单按钮对话框，B/C 留空）；帧5 对话框 pick=0 消费 + Hide 后自毁。</summary>
public sealed class SaveCardsProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void Update()
    {
        var fc = Lemon.Time.FrameCount;
        if (fc == 1) {
            Lemon.Save.SetString("probe", "hello-4");
            Lemon.Save.Set("raw", new byte[] { 1, 2, 3, 255 });
            Lemon.Ui.Set("hp", "68/100", 0.68f, 0xFF30B0F0u);
            Lemon.Ui.Set("tmp", "erase-me");
            Lemon.Ui.Clear("tmp");
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 700, default, default);
        } else if (fc == 2) {
            string? s = Lemon.Save.GetString("probe");
            byte[]? b = Lemon.Save.Get("raw");
            bool ok = s == "hello-4" && Lemon.Save.HasKey("probe") &&
                      !Lemon.Save.HasKey("nope") && b != null && b.Length == 4 && b[3] == 255;
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(800 + (ok ? 1 : 0)), default, default);
            Lemon.Ui.ShowCards("升级三选一", "移速 +10%", "磁力 +25%", "射速 +20%");
        } else if (fc == 3) {
            int pick = Lemon.Ui.CardPick();  // C++ 帧间置 1（模拟按钮/数字键）
            int again = Lemon.Ui.CardPick(); // 消费语义：同选择只回报一次
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(900 + pick * 10 + (again + 1)), default, default);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(960 + (Lemon.Input.Confirm ? 5 : 0)), default, default);
            Lemon.Ui.HideCards();
            Lemon.Save.Flush(); // 无 IO 钩子宿主：红字一次后 no-op（不炸即验收）
        } else if (fc == 4) {
            Lemon.Ui.ShowDialog("阵亡了，再战一局？", "复活"); // 批④后修④：对话框复活
        } else if (fc == 5) {
            int pick = Lemon.Ui.CardPick(); // C++ 帧间置 0（对话框唯一按钮）
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                              (ushort)(980 + (pick < 0 ? 9 : pick)), default, default);
            Lemon.Ui.HideCards();
            gameObject.Destroy();
        }
    }
}

/// <summary>M11 验收（typeId 7）：Awake/OnDestroy 内 native 调用。修复前
/// AttachBehaviour/结构命令期 native 窗口（g_world/g_scene）未设，Ui.Set 静默
/// 空转——Awake 与 Attach 同步执行，托管调用返回后 C++ 侧应立即可见。</summary>
public sealed class AwakeUiProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void Awake() => Lemon.Ui.Set("awake", "alive", 1.0f);

    protected override void OnDestroy() => Lemon.Ui.Set("awake", "dead", 0.0f);
}

/// <summary>M15 验收（typeId 8）：Subscribe 助手 + Detach 自动退订。挂载→销毁→
/// 再挂载后推一次 Custom 900：旧实例订阅不得残留（残留 = 一次推送两份 901 回执）。
/// 若用裸 Events.Subscribe 订阅（旧行为），首个已销毁实例的 handler 仍在静态表里，
/// 回执会翻倍。</summary>
public sealed class SubProbeBehaviour : Lemon.LemonBehaviour
{
    public SubProbeBehaviour()
        => Subscribe(Lemon.Interop.GameEvent.Custom, m => {
            if (m.User != 900) return;
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 901, default, default);
        });
}

/// <summary>M6a 批⓪ T3 验收（typeId 10）：GameObject 统一门面双路由——
/// AddComponent/GetComponent/RemoveComponent&lt;T&gt; 值组件（IComponent → op2/3 +
/// 原生读）∪ 脚本组件（LemonBehaviour → op4/5 + 实例表）。订阅 Custom 相位驱动
///（C++ 注入 user 码），结果经事件回报（15xx = ok，25xx = 反例；帧边界断言：
/// AddComponent 同帧 GetComponent = null）。InputMover 无自毁副作用（Counting
/// 第 2 帧自毁会带走宿主实体，不选）。</summary>
public sealed class DualRouteProbeBehaviour : Lemon.LemonBehaviour
{
    protected override void Awake()
        => Subscribe(Lemon.Interop.GameEvent.Custom, OnPhase);

    private void OnPhase(Lemon.GameEventMsg m)
    {
        switch ((int)m.User) {
        case 1500: // 值组件 op2 + 脚本 op4 ×2（幂等 get-or-add：待决命令去重）
            gameObject.AddComponent<Lemon.Interop.Health>();
            gameObject.AddComponent<InputMoverBehaviour>();
            gameObject.AddComponent<InputMoverBehaviour>();
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                gameObject.GetComponent<InputMoverBehaviour>() == null
                    ? (ushort)1541 : (ushort)2541, default, default);
            break;
        case 1501: // 脚本实例命中 + 值组件读回 → 卸双份（op5 + op3）
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                gameObject.GetComponent<InputMoverBehaviour>() != null
                    ? (ushort)1542 : (ushort)2542, default, default);
            var h = gameObject.GetComponent<Lemon.Interop.Health>();
            // op2 默认构造 = C++ Health{max=100,cur=100}：值分路读回真字节（非零非垃圾）
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom,
                h.Max == 100f && h.Cur == 100f ? (ushort)1543 : (ushort)2543, default, default);
            gameObject.RemoveComponent<InputMoverBehaviour>();
            gameObject.RemoveComponent<Lemon.Interop.Health>();
            break;
        case 1502: // 自卸（合法：Unity Destroy(this) 同语义；帧首应用后本实例退场）
            gameObject.RemoveComponent<DualRouteProbeBehaviour>();
            break;
        }
    }

    protected override void OnDestroy()
        => Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 1544, default, default);
}

/// <summary>M6a 批①：Lemon.Anim 状态控制 + Lemon.Fx 通道验收（typeId 11 表尾）。
/// C++ 侧（TestAnimFxSdk）：管线不含 AnimatorSystem——本测断言 SDK 字段契约
/// （写什么落什么，帧间无引擎消费扰动）；队列消费语义由 engine-tests
/// TestVerifyAnimatorQueue（全管线）覆盖。帧序（Custom user 编码）：
/// 帧1 Play(hit,loop:false)+Queue(walk) → 1100；帧2 Pause → 1200；
/// 帧3 Resume + Fx.Text/Fx.Bar → 1300；帧4 CrossFade(walk,0.5) → 1400；
/// 帧5 IsPlaying/Queued 轮询 → 1511 后自毁。</summary>
public sealed class AnimFxProbeBehaviour : Lemon.LemonBehaviour
{
    private const uint kWalk = 0x77, kHit = 0x88;

    protected override void Update()
    {
        var fc = Lemon.Time.FrameCount;
        if (fc == 1) {
            Lemon.Anim.Play(gameObject, kHit, false);
            Lemon.Anim.Queue(gameObject, kWalk, true);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 1100, default, default);
        } else if (fc == 2) {
            Lemon.Anim.Pause(gameObject);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 1200, default, default);
        } else if (fc == 3) {
            Lemon.Anim.Resume(gameObject);
            Lemon.Fx.Text("12", new Lemon.Vec2(10f, 20f), 0xFF5060F0u);
            Lemon.Fx.Bar(gameObject, 0.5f, 0xFF30B0F0u, 40f);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 1300, default, default);
        } else if (fc == 4) {
            Lemon.Anim.CrossFade(gameObject, kWalk, 0.5f, true);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, 1400, default, default);
        } else if (fc == 5) {
            int code = (Lemon.Anim.IsPlaying(gameObject) ? 1 : 0) * 10
                     + (Lemon.Anim.Queued(gameObject) ? 1 : 0);
            Lemon.Events.Push(Lemon.Interop.GameEvent.Custom, (ushort)(1500 + code),
                              default, default);
            gameObject.Destroy();
        }
    }
}
