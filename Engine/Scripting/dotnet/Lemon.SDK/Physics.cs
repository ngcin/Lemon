// Lemon.SDK — 物理层调参入口（2026-09-26 调参下放批）
// 分离力参数场景侧化：引擎默认（radius 32 / strength 140 / maxNeighbors 10 /
// densityCap 12）保持不动 = 基准场零漂移；超大规模场景（如五万对穿人海，
// ~98% 实体压在 maxNeighbors 截断线上）经本入口按场景收紧，换取分离段耗时
// 近线性下降。仅改 SeparationSystem 运行时参数（不入 StateHash，回放无关）；
// 建议在脚本初始化/进 Play 首帧一次性调用（每帧调用亦合法但无必要）。
// 旧宿主未注册表项时安全降级丢弃。
namespace Lemon;

public static class Physics
{
    /// <summary>覆盖分离力参数（软碰撞排斥：半径/强度/每实体邻居处理上限/密度
    /// 衰减阈值）。各参可传 -1 表示保持现值；maxNeighbors/densityCap 传 0 表示
    /// 关闭分离力。高密度人海场景推荐 maxNeighbors 4–6（实测 ~98% 实体在
    /// 默认截断线 10 上，收紧近乎线性省时；低于真实密度的截断只改堆叠紧度）。
    /// densityCap 语义：有效邻居数超过该值时总力按 cap/n 衰减——应 ≤ maxNeighbors
    /// 才有触达可能（默认组合 12&gt;10 即永不触发）。</summary>
    public static void Separation(float radius = -1f, float strength = -1f,
                                  int maxNeighbors = -1, int densityCap = -1)
        => Native.SetSeparationParams(radius, strength, maxNeighbors, densityCap);
}
