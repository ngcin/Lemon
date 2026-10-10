// Lemon.SDK — 世界空间表现通道（M6a 批①；06 §8 恒定原则：恒走 sprite 管线）------
// C# 写、视图侧（编辑器 GameView / lemon-game）读出。呈现层专用：不影响模拟
// 状态哈希/回放（FxChannel 头说明）。
// M7c 批①：飘字动效参数化（FxStyle：缩放/寿命/漂移/Pop 弹跳曲线）+ 贴图血条
// （FxBarSkin：bg/fg 贴图 guid + 延迟条 + 高度）+ Fx.Crit/Fx.Miss 默认值糖。
// 池化上限（引擎侧最老者淘汰）：飘字 256 / 血条 128——写超不炸、旧者让位。
// 典型用法（svr-test 战斗向）：
//   Fx.Crit($"暴击 {dmg:0}", pos);                          // 黄字 Pop 弹跳（糖）
//   Fx.Text($"{dmg:0}", pos, 0xFF5060F0, FxStyle.Pop(1.1f)); // 全自定义动效
//   Fx.Bar(enemy, hp.Cur / hp.Max, fg, 40f, bossBarSkin);    // 贴图血条+延迟白条
// 仅域线程 tick 期间有效（与 Native 表同窗口约定）。
using System;
using Lemon.Interop;

namespace Lemon;

/// <summary>飘字轨迹曲线（与引擎 FxCurve 对齐）。</summary>
public enum FxCurve : byte
{
    /// <summary>现状默认：开局 70% 匀速上浮。</summary>
    Linear = 0,
    /// <summary>出生 1.4× 回落 1.0 + ease-out 陡升上浮（暴击弹跳）。</summary>
    Pop = 1,
}

/// <summary>飘字动效参数（M7c 批①；默认值 = M6a 现状行为）。</summary>
public readonly struct FxStyle
{
    /// <summary>字号缩放（1 = 基准字号）。</summary>
    public readonly float Scale;
    /// <summary>寿命秒（≤0 = 默认 0.8s）。</summary>
    public readonly float Life;
    /// <summary>水平恒速漂移 px/s（暴击散布用）。</summary>
    public readonly float DriftX;
    /// <summary>轨迹曲线。</summary>
    public readonly FxCurve Curve;

    public FxStyle(float scale = 1f, float life = 0.8f, float driftX = 0f,
                   FxCurve curve = FxCurve.Linear)
    {
        Scale = scale; Life = life; DriftX = driftX; Curve = curve;
    }

    /// <summary>现状默认（匀升 0.8s）。</summary>
    public static FxStyle Default => new();
    /// <summary>Pop 弹跳便捷式（scale 可选加成）。</summary>
    public static FxStyle Pop(float scale = 1f, float life = 1.0f, float driftX = 0f)
        => new(scale, life, driftX, FxCurve.Pop);
}

/// <summary>血条皮肤（M7c 批①；默认值 = 白精灵染色现状路径）。
/// BgGuid/FgGuid = 贴图资产 16 位 hex guid（null = 白精灵）；前景按比例横向
/// UV 裁剪（图片不压扁）；LagColor ≠ 0 = 延迟条（白色残条向血量线性收敛）。</summary>
public readonly struct FxBarSkin
{
    public readonly string? BgGuid;
    public readonly string? FgGuid;
    /// <summary>延迟条配色 RGBA（0 = 无延迟条）。</summary>
    public readonly uint LagColor;
    /// <summary>条高世界像素（0 = 默认 4px；贴图形态建议 = width × 图高/图宽）。</summary>
    public readonly float Height;
    /// <summary>头顶锚定修正世界 px（+ = 下移；0 = 现状）。引擎自动锚定按精灵
    /// 整帧高计，帧内透明边距会把条悬空抬离可见头顶——按美术帧实测边距给值
    ///（如 440×420 帧边距 57px → +57）。</summary>
    public readonly float AnchorDy;

    public FxBarSkin(string? bgGuid = null, string? fgGuid = null, uint lagColor = 0,
                     float height = 0f, float anchorDy = 0f)
    {
        BgGuid = bgGuid; FgGuid = fgGuid; LagColor = lagColor; Height = height;
        AnchorDy = anchorDy;
    }

    /// <summary>白精灵现状路径（无贴图无延迟条）。</summary>
    public static FxBarSkin Default => new();
    /// <summary>受击显伤便捷式：贴图 bg/fg + 白色延迟条。</summary>
    public static FxBarSkin Textured(string bgGuid, string fgGuid, float height,
                                     uint lagColor = 0xE0F0F0F0u, float anchorDy = 0f)
        => new(bgGuid, fgGuid, lagColor, height, anchorDy);
}

public static class Fx
{
    /// <summary>世界空间飘字（伤害/治疗/短文本；16 字节截断——中文约 5 字、
    /// 位图字体渲染）。pos = 首字符中心锚点（文本向右延伸，世界坐标）。高频命中
    /// 场景直接打点即可——池化接管。颜色 = RGBA uint（同
    /// SpriteRenderer.colorRGBA；Lemon.Ui 的 ABGR 惯例色不能直接搬）。</summary>
    public static void Text(string text, Vec2 pos, uint color = 0xFFF0F0F0u)
        => Native.FxPopup(text, pos.X, pos.Y, color);

    /// <summary>数字快路径（伤害整数最常用形态；内联格式省一次 string 拼接）。</summary>
    public static void Text(float number, Vec2 pos, uint color = 0xFFF0F0F0u)
        => Native.FxPopup(number.ToString("0"), pos.X, pos.Y, color);

    /// <summary>动效全参（M7c 批①）：曲线/缩放/寿命/漂移全自定义。</summary>
    public static void Text(string text, Vec2 pos, uint color, FxStyle style)
        => Native.FxPopupEx(text, pos.X, pos.Y, color, style.Scale, style.Life,
                            style.DriftX, (byte)style.Curve);

    // L15（review 2026-10-09）：呈现层扰动专用 PCG32 子流（全引擎唯一随机源
    // 纪律，Pcg32.cs:2；Fx 不入 StateHash = 不影响回放，但流本身仍位级确定）
    private static readonly Pcg32 s_critJitter = new(0xF11Eu, 0x51u);

    /// <summary>暴击糖（默认值集合，游戏侧可全自定义）：黄字 Pop 弹跳 + 随机
    /// 水平散布（命中点 ±，同帧多发不打成一条线）。色值注意通道是 RGBA 序
    ///（r|g<<8|b<<16|a<<24）——0xFF4AD2FF = 黄（255,210,74）；按 ARGB 语感写的
    /// 0xFFFFD24A 解出来是青绿（走查轮④实抓）。</summary>
    public static void Crit(string text, Vec2 pos, float scale = 1.3f)
        => Native.FxPopupEx(text, pos.X, pos.Y, 0xFF4AD2FFu, scale, 1.0f,
                            (s_critJitter.Float01() - 0.5f) * 60f, (byte)FxCurve.Pop);

    /// <summary>闪避糖：灰白小号短飘。</summary>
    public static void Miss(Vec2 pos, float scale = 0.85f)
        => Native.FxPopupEx("MISS", pos.X, pos.Y, 0xFFB8C0C8u, scale, 0.6f, 0f,
                            (byte)FxCurve.Linear);

    /// <summary>世界血条（按实体锚定：条画在实体位上方 2px，宽 width 世界像素）。
    /// 同实体重复调用 = 刷新比例续命（受击显伤条 3s 无刷新自隐；玩家条每帧
    /// 刷新即常显）；frac 钳 [0,1]；实体销毁后条不再渲染（槽等 sticky 过期回收）。</summary>
    public static void Bar(GameObject g, float frac, uint color = 0xFF30B0F0u,
                           float width = 32f)
    {
        if (g.Entity.IsNull) return;
        Native.FxBar(g.Entity.Id, frac, color, width);
    }

    /// <summary>皮肤全参（M7c 批①）：贴图 bg/fg + 延迟条 + 高度 + 锚点修正。</summary>
    public static void Bar(GameObject g, float frac, uint color, float width, FxBarSkin skin)
    {
        if (g.Entity.IsNull) return;
        Native.FxBarEx(g.Entity.Id, frac, color, width, skin.BgGuid, skin.FgGuid,
                       skin.LagColor, skin.Height, skin.AnchorDy);
    }
}
