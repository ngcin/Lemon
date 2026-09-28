// Lemon.SDK — 成长参数（M6a 批② T4；ADR-012 D3）
// World 级单参数面（Time.Scale 同款）：默认值 = 引擎原硬编码，Start 期从 balance.tab
// 覆盖 = 项目级数值配置外置；改动不入 StateHash（World 持有 + 非 ECS——效果经
// XpProgress.xpToNext 字段演化入哈希，与作者改场景数值同类）。
namespace Lemon;

public static class Balance
{
    /// <summary>XP 曲线系数（xpToNext 逐级 ×k；引擎默认 1.25）。旧宿主读恒默认、写丢弃。</summary>
    public static float XpCurveK
    {
        get => Native.XpCurveK();
        set => Native.SetXpCurveK(value);
    }
}
