// Lemon.SDK — Animator 状态控制（M6a 批①；03 §8.1 换段语义）---------------------
// 纯字段读写（Animator2D 镜像经 Native.Read/Write 往返）——零 C ABI：clipId 本就
// = clip 资产 GUID 低 32 位，无需查表通道（与 spriteId 不同）。
// 语义（引擎侧 AnimatorSystem 消费，详见 03 §8.1）：
//   Play      立即切段（time 归零；同段 = 重播；打断在途队列）
//   Queue     当前段收尾（非 loop 钳末帧）或回绕点（loop）切段——受击组合拳 =
//             Play(hit, loop:false) + Queue(walk)：立即播受击、播完自动回行走
//   CrossFade 倒计时切段（fade 秒后切；非 loop 当前段提前收尾立即切）。帧动画
//             无姿态混合——fade 语义 = 延迟切换而非 alpha 混合（04 §3.2 声明）
//   Pause/Resume = playOnStart 0/1（time/curFrame/spriteId/队列全冻结）
// 仅域线程 tick 期间有效（与 Native 表同窗口约定）；低频语法糖口径同 GameObject。
using System;
using Lemon.Interop;

namespace Lemon;

public static class Anim
{
    /// <summary>clip 资产 GUID（16 位 hex）→ clipId（GUID 低 32 位直取，零跨界）。</summary>
    public static uint ClipId(string clipGuidHex)
        => (uint)ulong.Parse(clipGuidHex, System.Globalization.NumberStyles.HexNumber);

    /// <summary>立即播放段（time 归零、打断在途队列）。loop=false 播完钳末帧——
    /// 配 Queue 做受击→回行走；同段重复调用 = 重播。</summary>
    public static void Play(GameObject g, uint clipId, bool loop = true)
    {
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.ClipId = clipId;
        an.Time = 0f;
        an.Loop = loop ? (byte)1 : (byte)0;
        an.PlayOnStart = 1;
        an.NextClipId = 0;
        an.FadeRemain = 0f;
        g.SetComponent(an);
    }

    public static void Play(GameObject g, string clipGuidHex, bool loop = true)
        => Play(g, ClipId(clipGuidHex), loop);

    /// <summary>排队：当前段收尾（非 loop）或回绕点（loop）切到目标段。
    /// 后到的 Queue/CrossFade 覆写前队；Play 清队列。</summary>
    public static void Queue(GameObject g, uint clipId, bool loop = true)
    {
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.NextClipId = clipId;
        an.NextLoop = loop ? (ushort)1 : (ushort)0;
        an.FadeRemain = -1f;
        g.SetComponent(an);
    }

    public static void Queue(GameObject g, string clipGuidHex, bool loop = true)
        => Queue(g, ClipId(clipGuidHex), loop);

    /// <summary>倒计时切段：fade 秒后切到目标段（非 loop 当前段提前收尾立即切）。
    /// fade ≤ 0 = 立即切（同 Play）。</summary>
    public static void CrossFade(GameObject g, uint clipId, float fade, bool loop = true)
    {
        if (fade <= 0f) {
            Play(g, clipId, loop);
            return;
        }
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.NextClipId = clipId;
        an.NextLoop = loop ? (ushort)1 : (ushort)0;
        an.FadeRemain = fade;
        g.SetComponent(an);
    }

    public static void CrossFade(GameObject g, string clipGuidHex, float fade, bool loop = true)
        => CrossFade(g, ClipId(clipGuidHex), fade, loop);

    /// <summary>暂停（time/curFrame/spriteId/换段队列全冻结；= playOnStart 0）。</summary>
    public static void Pause(GameObject g)
    {
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.PlayOnStart = 0;
        g.SetComponent(an);
    }

    /// <summary>恢复播放（= playOnStart 1）。</summary>
    public static void Resume(GameObject g)
    {
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.PlayOnStart = 1;
        g.SetComponent(an);
    }

    /// <summary>是否在播（= playOnStart 开关；暂停 = false）。"非 loop 段播完"SDK
    /// 侧不可判（total 在引擎 clip 表）——播完的可观测证据 = 排队的下一段已切
    /// （Queued 变 false 且 ClipId 已变），或自查 Time 不再增长。</summary>
    public static bool IsPlaying(GameObject g)
    {
        return g.TryGetComponent<Animator2D>(out var an) && an.PlayOnStart != 0;
    }

    /// <summary>是否有在途换段队列（Queue/CrossFade 已排队未切）。</summary>
    public static bool Queued(GameObject g)
    {
        return g.TryGetComponent<Animator2D>(out var an) && an.NextClipId != 0;
    }
}
