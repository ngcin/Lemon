// Lemon.SDK — 2D 光照门面（M8 批③，ADR-019 M4）------------------------------------------------
// 纯 C# sugar：全部走既有组件读/整写通道（Native.Read/Write）与 A 档补间
//（Native.TweenTo）——零 vtable 新槽、零引擎改动；写 Light2D/BlobShadow2D 字段
// 即入 StateHash ⇒ 昼夜补间可回放（ADR-019 M4 口径）。
// 镜像默认值坑三连（Components.cs 头注）在此正面解法：**整写门面（SetGlobalAmbient
// /BlobShadow.Set）显式置全字段**；读改写门面（SetColor 等）不踩坑。
// 定位灯实体 = 调用方职责（序列化引用/Query 扫描——SDK 无按组件反查 API，v1 不加）。
using System;
using Lemon.Interop;

namespace Lemon;

public static class Lighting
{
    /// <summary>光源类型（与引擎 ecs::kLightType* 同值）。</summary>
    public enum Kind : uint { Global = 0, Point = 1, Spot = 2, SpriteMask = 3 }

    /// <summary>混合模式（与引擎 ecs::kLightBlend* 同值；Max 已砍——ADR-019 D3）。</summary>
    public enum Blend : uint { Additive = 0, Screen = 1, Subtract = 2 }

    /// <summary>启用位（Light2D.Flags bit0；其余位保留）。</summary>
    public const ushort Enabled = 1;

    /// <summary>RGBA 打包（r|g&lt;&lt;8|b&lt;&lt;16|a&lt;&lt;24——引擎全库同序）。</summary>
    public static uint Rgba(byte r, byte g, byte b, byte a = 255)
        => r | ((uint)g << 8) | ((uint)b << 16) | ((uint)a << 24);

    // ---- 环境光（昼夜循环的锚点：Global 灯 = ambient 底色）----

    /// <summary>整写为 Global 环境光并启用（昼夜脚本的正道入口：显式置 Type/Color/
    /// Flags——C# default 三坑（Type=0 恰为 Global 是唯一"碰巧对"的坑，Color=黑/
    /// Flags=禁用必踩）全绕开）。组件已在 = 即时整写返回 true；组件缺失 = 走 op2
    /// 暂存 AddComponent 返回 <b>false</b>（结构变更下一帧帧头才生效，**引擎 C++
    /// 默认值落场**——Light2D = Point/白/启用，不是无害的禁用态，第二拍前该灯
    /// 已可见！下一 tick 再调一次本方法即落值；Instantiate+Add 场景的标准两拍）。
    /// </summary>
    public static bool SetGlobalAmbient(GameObject g, uint colorRGBA, float intensity)
    {
        Light2D lt = default;
        lt.Type = (uint)Kind.Global;
        lt.Blend = (uint)Blend.Additive;
        lt.Color = colorRGBA;
        lt.Intensity = intensity;
        lt.Range = 256f;
        lt.Angle = 1.57079632f;
        lt.InnerRatio = 0.5f;
        lt.Flags = Enabled;
        return WriteOrAdd(g, in lt);
    }

    /// <summary>昼夜过渡 = color + intensity 双补间（引擎 TweenTable 元数据驱动，
    /// 零新通道；字段入 StateHash ⇒ 可回放）。返回 intensity 补间句柄（等它即等
    /// 整组——同 duration 同 ease 同时完）。组件缺/旧宿主 = no-op 返回 0。</summary>
    public static long DayNight(GameObject g, uint toColorRGBA, float toIntensity,
                                float seconds, Tween.Ease ease = Tween.Ease.OutQuad)
    {
        if (!g.TryGetComponent<Light2D>(out _)) return 0;
        Tween.To<Light2D>(g, "color", toColorRGBA, seconds, ease);
        return Tween.To<Light2D>(g, "intensity", toIntensity, seconds, ease);
    }

    // ---- 开关与字段 sugar（读改写整写；组件缺失 = false no-op）----
    // Enable/Disable/Toggle 返回**操作后启用态**（幂等查询语义：Disable 已禁用也返
    // false——不是"是否做了事"的成功布尔）；IsEnabled 纯查询。

    /// <summary>启用灯（Flags 置 bit0；返回操作后启用态——组件缺失 = false）。</summary>
    public static bool Enable(GameObject g) => SetFlags(g, f => (ushort)(f | Enabled));

    /// <summary>禁用灯（提取侧滤除——帧内即从光照段消失；返回操作后启用态 = false）。</summary>
    public static bool Disable(GameObject g) => SetFlags(g, f => (ushort)(f & ~Enabled));

    /// <summary>翻转启用位，返回翻转后状态。</summary>
    public static bool Toggle(GameObject g) => SetFlags(g, f => (ushort)(f ^ Enabled));

    /// <summary>灯是否启用（组件缺失 = false）。</summary>
    public static bool IsEnabled(GameObject g)
        => g.TryGetComponent<Light2D>(out var lt) && (lt.Flags & Enabled) != 0;

    /// <summary>改色（保其余字段；枪口闪暖光/状态染色用）。</summary>
    public static bool SetColor(GameObject g, uint colorRGBA)
        => Mutate(g, lt => lt.Color = colorRGBA);

    /// <summary>改强度（保其余字段；呼吸灯可改用 Tween.To&lt;Light2D&gt;）。</summary>
    public static bool SetIntensity(GameObject g, float intensity)
        => Mutate(g, lt => lt.Intensity = intensity);

    /// <summary>改半径 px（Point/Spot 光域；SpriteMask 蒙版四边形半边）。</summary>
    public static bool SetRange(GameObject g, float rangePx)
        => Mutate(g, lt => lt.Range = rangePx);

    // ---- blob 阴影 sugar ----

    /// <summary>BlobShadow2D 子门面（椭圆暗斑：lightRT 上 Subtract——独立于灯存在）。</summary>
    public static class BlobShadow
    {
        /// <summary>整写四字段并"启用"（opacity &gt; 0 = 在场——blob 无 flags 字段，
        /// opacity=0 即关，M8 批③口径）。组件已在 = 即时整写 true；缺失 = op2 暂存
        /// 返回 false（引擎 C++ 默认值先落场——radius 32/opacity 0.5，第二拍前暗斑
        /// 已可见；下一 tick 再调落值，SetGlobalAmbient 同款两拍时序）。</summary>
        public static bool Set(GameObject g, float radius, float aspect, float offsetY,
                               float opacity)
        {
            BlobShadow2D bs = default;
            bs.Radius = radius;
            bs.Aspect = aspect;
            bs.OffsetY = offsetY;
            bs.Opacity = System.Math.Clamp(opacity, 0f, 1f);
            return WriteOrAdd(g, in bs);
        }

        /// <summary>只改暗斑强度 [0,1]（0 = 关——帧内消失）。组件缺失 = false。</summary>
        public static bool SetOpacity(GameObject g, float opacity)
        {
            if (!g.TryGetComponent<BlobShadow2D>(out var bs)) return false;
            bs.Opacity = System.Math.Clamp(opacity, 0f, 1f);
            g.SetComponent(in bs);
            return true;
        }
    }

    // ---- 内部 ----

    /// 组件已在 = 即时整写 true；缺失 = op2 暂存 AddComponent（下一帧帧头以
    /// 引擎 C++ 默认值落场——Emplace 成员初始化，非 C# default 零值）返回
    /// false——同 tick 写不进是 op2 暂存语义，非缺陷
    private static bool WriteOrAdd<T>(GameObject g, in T c) where T : unmanaged
    {
        if (!g.TryGetComponent<T>(out _)) {
            g.AddComponent<T>();
            return false;
        }
        g.SetComponent(in c);
        return true;
    }

    private static bool Mutate(GameObject g, Action<Light2D> change)
    {
        if (!g.TryGetComponent<Light2D>(out var lt)) return false;
        change(lt);
        g.SetComponent(in lt);
        return true;
    }

    private static bool SetFlags(GameObject g, Func<ushort, ushort> change)
    {
        if (!g.TryGetComponent<Light2D>(out var lt)) return false;
        lt.Flags = change(lt.Flags);
        g.SetComponent(in lt);
        return (lt.Flags & Enabled) != 0;
    }
}
