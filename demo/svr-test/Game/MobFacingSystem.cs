// Lemon 引擎 — demo/svr-test 怪物朝向（换装批⑪ 配套）
//
// 素材默认朝右（fox run 帧），向左移动需翻面。
//
// 为什么用批量系统而不是每怪一个 MonoBehaviour：怪物 prefab 由引擎
// PrefabCache::Spawn 走原生工厂出生，而该路径**不做 scripts[] 槽解析**
// （PrefabCache.h:52 明载——"原生工厂 spawn 的树不带脚本实例"）。
// 挂脚本的怪永远不会跑 Update。所以走档②批量系统：注册一次，按
// Velocity+SpriteRenderer 整批扫，与出生路径无关。
//
// 只读 Velocity（引擎 ChaseSystem 写）不改，纯表现层。不写 Velocity
// 就不产生模拟态影响；写 Flags 属渲染提取面，回放哈希按组件全量
// ComputeStateHash 覆盖 SpriteRenderer，玩家/怪朝向一致可复现。
//
// 死区 1px：贴脸/无目标时 Velocity=0，保持上次朝向不回弹，避免每帧闪。
using Lemon;
using Lemon.Interop;

namespace Game;

/// <summary>怪物朝向：按 Velocity.x 翻面。仅作用于 team 1（怪物）——team 0 的
/// 子弹/飞剑同有 Velocity+SpriteRenderer，但它们另有各自的表现语义，不在此翻。</summary>
public sealed class MobFacingSystem : IForEachSystem
{
    public string Name => "MobFacing";

    public Query Query => Query.With<Velocity, SpriteRenderer, Meta>();

    // SpriteRenderer.flags 位（与 Engine/Components/RenderComponents.h 同步）
    private const byte kSrFlipX = 0x01;

    /// <summary>注册到脚本系统表（GameMain.Configure 调用；执行点 = 管线批量系统位）。</summary>
    public static void Register() => Scripting.Register(new MobFacingSystem());

    public unsafe void ForEach(in Chunk chunk)
    {
        var vel = chunk.Span<Velocity>();
        var sr = chunk.Span<SpriteRenderer>();
        var meta = chunk.Span<Meta>();
        for (int i = 0; i < chunk.Length; i++) {
            if (meta[i].Team != 1) continue; // 只翻怪物（子弹/飞剑不动）
            byte want = (byte)(sr[i].Flags & ~kSrFlipX);
            if (vel[i].V.X < -1f) want |= kSrFlipX; // 向左翻面；向右回正；静止保上次
            if (want != sr[i].Flags) sr[i].Flags = want;
        }
    }
}