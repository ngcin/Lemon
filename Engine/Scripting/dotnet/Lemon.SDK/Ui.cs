// Lemon.SDK — Game RT UI 最小通道（M5 批① D6；04 §4 事件/UI 面）
// C# 写、宿主读（编辑器 GameView 叠加；M8 打包 HUD 同通道）。World 级定长 8 槽：
// 命中 key 覆写、空槽即占、满槽忽略；text 截断 47 字符。呈现层专用，不影响模拟
// 状态哈希/回放。典型用法（成长闭环）：订阅 Pickup/LevelUp → Ui.Set("xp", "LV 3 45/120", 0.45f)。
namespace Lemon;

public static class Ui
{
    /// <summary>写一行 HUD。key 稳定即同行覆写（如 "xp"）；frac ∈ [0,1] 附进度条，<0 纯文本。
    /// 仅域线程 tick 期间有效（与 Native 表同窗口约定）。</summary>
    public static void Set(string key, string text, float frac = -1f)
        => Native.UiSet(key, text, frac);
}
