// Lemon.SDK — 运行时属性补间（A 档 tween，2026-09-28；03 §8.3 / 批文件
// 2026-09-28-b2-tween-a-runtime.md）------------------------------------------------
// 语义（引擎侧 TweenSystem 消费——插在脚本批之后：当 tick 发起的补间本 tick 即
// 首写；存活补间**拥有字段**：同帧脚本写被覆写、同字段新建 = 顶替、Kill/Once
// 完成后归还脚本；timeScale=0 冻结）。
// 字段寻址 = Inspector/序列化同名（如 Transform2D 的 "pos"/"scale"/"rot"）；
// 类型白名单 Float / Vec2 / UInt32 颜色（字节通道插值）。低频语法糖口径同
// Lemon.Anim（仅域线程 tick 期间有效；旧宿主未注册新表项时安全降级 no-op）。
// 完成通知双通道：Events.Subscribe(GameEvent.TweenFinished)（userArg = 句柄）
// 或 Alive(handle) 轮询；Yoyo 模式永续无完成事件。
using System;
using Lemon.Interop;

namespace Lemon;

public static class Tween
{
    /// <summary>缓动曲线（与引擎 TweenEase 同值；OutBack/Elastic 会过冲——弹跳手感来源）。</summary>
    public enum Ease : byte
    {
        Linear = 0, InQuad, OutQuad, InOutQuad, OutCubic, InOutCubic,
        OutBack, OutElastic, OutBounce,
    }

    /// <summary>Once 单程到终值即完（发 TweenFinished）/ Yoyo 三角波永续往返（Kill 停，
    /// 无完成事件——idle 呼吸、悬浮 bobbing 用）。</summary>
    public enum Mode : byte { Once = 0, Yoyo }

    // ---- 通用按字段（字段名 = Inspector 同名；返回句柄 0 = 建链失败 no-op）----

    /// <summary>float 字段补间（如 Transform2D 的 "rot"）。</summary>
    public static unsafe long To<T>(GameObject g, string field, float to, float duration,
                                    Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        where T : unmanaged
    {
        float* to4 = stackalloc float[4];
        to4[0] = to;
        return (long)Native.TweenTo(g.Entity.Id, ComponentTable.Id<T>(), field, to4, duration,
                                     (byte)ease, (byte)mode);
    }

    /// <summary>Vec2 字段补间（如 Transform2D 的 "pos"/"scale"）。</summary>
    public static unsafe long To<T>(GameObject g, string field, Vec2 to, float duration,
                                    Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        where T : unmanaged
    {
        float* to4 = stackalloc float[4];
        to4[0] = to.X; to4[1] = to.Y;
        return (long)Native.TweenTo(g.Entity.Id, ComponentTable.Id<T>(), field, to4, duration,
                                    (byte)ease, (byte)mode);
    }

    /// <summary>UInt32 颜色字段补间（SpriteRenderer 的 "colorRGBA"；布局 r|g<<8|b<<16|a<<24，
    /// 四字节通道各自插值）。</summary>
    public static unsafe long To<T>(GameObject g, string field, uint toColorRGBA, float duration,
                                    Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        where T : unmanaged
    {
        float* to4 = stackalloc float[4];
        for (int i = 0; i < 4; i++) to4[i] = (toColorRGBA >> (8 * i)) & 0xFFu;
        return (long)Native.TweenTo(g.Entity.Id, ComponentTable.Id<T>(), field, to4, duration,
                                    (byte)ease, (byte)mode);
    }

    // ---- 常用糖（字段名固定；参数只留值/时长/曲线）----

    /// <summary>位置补间（Transform2D.pos）。</summary>
    public static long Position(GameObject g, Vec2 to, float duration,
                                Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        => To<Interop.Transform2D>(g, "pos", to, duration, ease, mode);

    /// <summary>缩放补间（Transform2D.scale，xy 独立）。</summary>
    public static long Scale(GameObject g, Vec2 to, float duration,
                             Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        => To<Interop.Transform2D>(g, "scale", to, duration, ease, mode);

    /// <summary>均匀缩放补间（xy 同值——弹出/受击放大用，配 OutBack）。</summary>
    public static long Scale(GameObject g, float uniform, float duration,
                             Ease ease = Ease.OutBack, Mode mode = Mode.Once)
        => Scale(g, new Vec2(uniform, uniform), duration, ease, mode);

    /// <summary>旋转补间（Transform2D.rot，弧度）。</summary>
    public static long Rotation(GameObject g, float toRad, float duration,
                                Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        => To<Interop.Transform2D>(g, "rot", toRad, duration, ease, mode);

    /// <summary>颜色补间（SpriteRenderer.colorRGBA；受击闪白/渐隐用）。</summary>
    public static long Color(GameObject g, uint toColorRGBA, float duration,
                             Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
        => To<Interop.SpriteRenderer>(g, "colorRGBA", toColorRGBA, duration, ease, mode);

    /// <summary>仅透明度补间（保 RGB 换 A；alpha ∈ [0,1]——渐隐渐现）。当前色读失败
    /// （组件缺/旧宿主）= no-op 返回 0。</summary>
    public static long Alpha(GameObject g, float alpha, float duration,
                             Ease ease = Ease.OutQuad, Mode mode = Mode.Once)
    {
        if (!g.TryGetComponent<Interop.SpriteRenderer>(out var sr)) return 0;
        byte a = (byte)(System.Math.Clamp(alpha, 0f, 1f) * 255f + 0.5f);
        uint to = (sr.ColorRGBA & 0x00FFFFFFu) | ((uint)a << 24);
        return Color(g, to, duration, ease, mode);
    }

    // ---- 轮询 / 终止 ----

    /// <summary>句柄存活（Once 完成后翻 false；Yoyo 恒 true 直到 Kill）。</summary>
    public static bool Alive(long handle)
        => handle != 0 && Native.TweenAlive((ulong)handle) != 0;

    /// <summary>删同实体同字段的存活补间（返回移除数；字段冻结在当前值）。</summary>
    public static int Kill<T>(GameObject g, string field) where T : unmanaged
        => Native.TweenKill(g.Entity.Id, ComponentTable.Id<T>(), field);

    /// <summary>删该实体全部补间（死亡/重生清场用；返回移除数）。</summary>
    public static int KillAll(GameObject g)
        => Native.TweenKillEntity(g.Entity.Id);
}
