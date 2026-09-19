// BenchScript — M3 验收场"环绕弹幕"（08 出口判据，ADR-010 修订版）
// 构成：档② BoomerangSystem（Projectile+Transform2D 双组件查询、原地写回）
//      + 档① PlayerBehaviour（Lissajous 走位，每帧 NativeApi 写 Transform2D）
//      + 毒脚本 PoisonSystem（每帧抛异常 → 异常隔离 → 连续 60 帧自动禁用，引擎不崩）。
// 确定性：Update 先于批量（一帧固定序）；块/实体序稳定（无结构变更）；
//         金档回放依赖引擎分发运行时版本（ADR-010 D3）。
using Lemon;
using Lemon.Interop;

public static class Arena
{
    public static Vec2 PlayerPos; // 档①每帧更新，档②读——跨档共享热点（零分配）
    public static float Time;
}

public static class GameMain
{
    public static void Configure()
    {
        Behaviours.Register<PlayerBehaviour>(); // typeId 0
        Scripting.Register(new BoomerangSystem());
        Scripting.Register(new PoisonSystem()); // 验收"异常脚本不崩引擎"
    }
}

/// <summary>玩家：Lissajous 走位（档①；Update 经 NativeApi 写组件——语法糖通道）。</summary>
public sealed class PlayerBehaviour : LemonBehaviour
{
    protected override void Update()
    {
        Arena.Time += 0.25f * 60.0f * (1.0f / 60.0f); // dt=0.25s 慢时钟（验收口径固定步）
        float t = Arena.Time;
        Vec2 pos = new(System.MathF.Sin(t * 0.7f) * 180.0f, System.MathF.Cos(t * 1.1f) * 120.0f);
        var tr = gameObject.GetComponent<Transform2D>();
        tr.Pos = pos;
        gameObject.SetComponent(in tr);
        Arena.PlayerPos = pos;
    }
}

/// <summary>环绕弹幕（档②）：弹幕绕玩家轨道推进 + age 步进（原地写回，零跨界/零分配）。</summary>
public sealed class BoomerangSystem : IForEachSystem
{
    public string Name => "Boomerang";
    public Query Query => Query.With<Projectile, Transform2D>();

    public unsafe void ForEach(in Chunk chunk)
    {
        var pr = chunk.Span<Projectile>();
        var tr = chunk.Span<Transform2D>();
        Vec2 c = Arena.PlayerPos;
        for (int i = 0; i < chunk.Length; i++) {
            float age = pr[i].Age + 0.0166667f;
            pr[i].Age = age;
            float a = age * 1.7f + i * 0.0005f; // 索引相位：确定性（块/实体序稳定）
            float r = 42.0f + (i % 7) * 11.0f;
            tr[i].Pos = new Vec2(c.X + System.MathF.Cos(a) * r, c.Y + System.MathF.Sin(a) * r);
        }
    }
}

/// <summary>毒脚本：每帧抛异常——04 §7 异常隔离 + 60 帧自动禁用的活体验收。</summary>
public sealed class PoisonSystem : IForEachSystem
{
    public string Name => "Poison";
    public Query Query => Query.With<Projectile>();
    public void ForEach(in Chunk chunk)
        => throw new System.InvalidOperationException("poison: acceptance test");
}
