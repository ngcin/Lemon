// Lemon.SDK — Animator 状态控制（M6a 批①；03 §8.1 换段语义）---------------------
// uint clipId 重载 = 纯字段读写（Animator2D 镜像经 Native.Read/Write 往返）——
// 零 C ABI：clipId 本就 = clip 资产 GUID 低 32 位，无需查表通道（与 spriteId 不同）。
// T3c 起字符串重载 = 按名解析，走唯一低频桥 clipByName：实体当前段所在 .ani 动画集
// 内查段名（跨集同名互不干扰）；未命中回退 GUID hex（旧脚本兼容）。
// 语义（引擎侧 AnimatorSystem 消费，详见 03 §8.1）：
//   Play      立即切段（time 归零；同段 = 重播；打断在途队列）
//   Queue     当前段收尾（非 loop 钳末帧）或回绕点（loop）切段——受击组合拳 =
//             Play(hit, loop:false) + Queue(walk)：立即播受击、播完自动回行走
//   CrossFade 倒计时切段（fade 秒后切；非 loop 当前段提前收尾立即切）。帧动画
//             无姿态混合——fade 语义 = 延迟切换而非 alpha 混合（04 §3.2 声明）
//   Pause/Resume = playOnStart 0/1（time/curFrame/spriteId/队列全冻结）
// 仅域线程 tick 期间有效（与 Native 表同窗口约定）；低频语法糖口径同 GameObject。
using System;
using System.Collections.Generic;
using Lemon.Interop;

namespace Lemon;

public static class Anim
{
    /// <summary>T3b-2 循环模式：Once 钳末帧 / Loop 回绕 / PingPong 往返（0..n-1..0）。
    /// 与 Animator2D.Loop 字节同值域（0/1 旧语义不变）。</summary>
    public enum LoopMode : byte
    {
        Once = 0,
        Loop = 1,
        PingPong = 2,
    }

    /// <summary>clip 资产 GUID（16 位 hex）→ clipId（GUID 低 32 位直取，零跨界）。
    /// 非法 hex = 0 + 红字一次（TryParse 口径，与 GuidOf/ResolveClip/Audio 一致——
    /// 原 ulong.Parse 裸抛，#59）。</summary>
    public static uint ClipId(string clipGuidHex)
    {
        if (clipGuidHex != null &&
            ulong.TryParse(clipGuidHex, System.Globalization.NumberStyles.HexNumber,
                           null, out var v))
            return (uint)v;
        if (s_missed.Add("clipid:" + (clipGuidHex ?? "(null)")))
            Console.Error.WriteLine(
                $"[lemon][warn] Anim.ClipId: '{clipGuidHex ?? "(null)"}' 非 GUID hex → 0");
        return 0;
    }

    /// <summary>立即播放段（time 归零、打断在途队列）。loop=false 播完钳末帧——
    /// 配 Queue 做受击→回行走；同段重复调用 = 重播。</summary>
    public static void Play(GameObject g, uint clipId, bool loop = true)
        => Play(g, clipId, loop ? LoopMode.Loop : LoopMode.Once);

    /// <summary>T3b-2：LoopMode 版（PingPong = 往返）。</summary>
    public static void Play(GameObject g, uint clipId, LoopMode mode)
    {
        if (!g.TryGetComponent<Animator2D>(out var an)) return;
        an.ClipId = clipId;
        an.Time = 0f;
        an.Loop = (byte)mode;
        an.PlayOnStart = 1;
        an.NextClipId = 0;
        an.FadeRemain = 0f;
        an.Ended = 0; // T3d 批③：切段归零段末边沿（AnimFinished 只在新段收尾时再发）
        g.SetComponent(an);
    }

    // ---- T3c：字符串重载按名解析（集作用域）--------------------------------

    static readonly HashSet<string> s_missed = new();

    /// <summary>段名/GUID-hex 双解析：先实体当前段所在 .ani 动画集内按名（跨集
    /// 同名互不干扰），未命中回退 GUID hex（旧脚本兼容，合法 hex 恒可达）。都失败
    /// = 红字一次（每实体×名字去重，防逐帧刷屏）→ 返回 0，调用方 no-op——不能拿
    /// 0 写组件（clipId 0 = 挂 Animator 关闭语义）。</summary>
    static uint ResolveClip(GameObject g, string nameOrGuidHex)
    {
        long id = Native.ClipByName(g.Entity.Id, nameOrGuidHex);
        if (id > 0) return (uint)id;
        if (nameOrGuidHex != null &&
            ulong.TryParse(nameOrGuidHex, System.Globalization.NumberStyles.HexNumber,
                           null, out var guid))
            return (uint)guid;
        if (s_missed.Add(g.Entity.Id + ":" + nameOrGuidHex))
            Console.Error.WriteLine(
                $"[lemon][warn] Anim: 实体 {g.Entity.Id} 的动画集内无段 " +
                $"'{nameOrGuidHex ?? "(null)"}'（且非 GUID hex）→ no-op");
        return 0;
    }

    /// <summary>立即播放段（time 归零、打断在途队列）。loop=false 播完钳末帧——
    /// 配 Queue 做受击→回行走；同段重复调用 = 重播。字符串参数 = 段名（集内解析）
    /// 或 GUID hex（旧脚本），解析失败 no-op。</summary>
    public static void Play(GameObject g, string nameOrGuidHex, bool loop = true)
    {
        uint id = ResolveClip(g, nameOrGuidHex);
        if (id != 0) Play(g, id, loop);
    }

    /// <summary>T3b-2：LoopMode 版（PingPong = 往返）。</summary>
    public static void Play(GameObject g, string nameOrGuidHex, LoopMode mode)
    {
        uint id = ResolveClip(g, nameOrGuidHex);
        if (id != 0) Play(g, id, mode);
    }

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

    /// <summary>排队版字符串重载（段名/GUID-hex，解析失败 no-op）。</summary>
    public static void Queue(GameObject g, string nameOrGuidHex, bool loop = true)
    {
        uint id = ResolveClip(g, nameOrGuidHex);
        if (id != 0) Queue(g, id, loop);
    }

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

    /// <summary>字符串重载（段名/GUID-hex，解析失败 no-op）。</summary>
    public static void CrossFade(GameObject g, string nameOrGuidHex, float fade, bool loop = true)
    {
        uint id = ResolveClip(g, nameOrGuidHex);
        if (id != 0) CrossFade(g, id, fade, loop);
    }

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

    /// <summary>是否在播（= playOnStart 开关；暂停 = false）。三态口径（M8 ⑮c）：
    /// Playing（在播）/ Paused（Pause 后）/ Finished（非 loop 段播完钳末帧）——
    /// 前两态由本 API 判、末态由 <see cref="IsFinished"/> 判，互斥面组合完整。</summary>
    public static bool IsPlaying(GameObject g)
    {
        return g.TryGetComponent<Animator2D>(out var an) && an.PlayOnStart != 0;
    }

    /// <summary>非 loop 段是否已播完（直读 Animator2D.Ended 段末边沿镜像：引擎
    /// Once 段钳 total 置位、Play 切段归零——零新桥零引擎改动）；loop 段恒 false
    /// （ended 语义使然）。播完钳末帧后组件仍"在播"（playOnStart=1）——
    /// IsPlaying 与本 API 不互斥，Finished 态的完整判 = IsFinished(g)。</summary>
    public static bool IsFinished(GameObject g)
    {
        return g.TryGetComponent<Animator2D>(out var an) && an.Ended != 0;
    }

    /// <summary>是否有在途换段队列（Queue/CrossFade 已排队未切）。</summary>
    public static bool Queued(GameObject g)
    {
        return g.TryGetComponent<Animator2D>(out var an) && an.NextClipId != 0;
    }

    // ---- T3d 批①/②：AnimGraph 绑定 + 参数黑板（ADR-013 D1/D2）----------------
    // 绑定/参数是配置面：建议编辑期在场景/prefab 里配好（Inspector 绑定槽）。
    // 运行时改绑经 SetComponent 直写——结构命令（AddComponent）是缓冲语义，
    // 实体缺组件时本组 API = 红字一次 + no-op（不隐式建实体结构）。

    static readonly HashSet<string> s_paramMissed = new();

    /// <summary>随局/换域清告警去重表（L17 review 2026-10-09）：键含实体 id
    ///（entt 单调递增），解析失败模式持续时无界累积并根住字符串；PlayReset/
    /// 域 Reset 原不清这两张表（清的是 Scripting/Events/UI/Behaviours/SceneOps/Time）。</summary>
    internal static void ResetWarnTables() {
        s_missed.Clear();
        s_paramMissed.Clear();
    }

    /// <summary>GUID hex（16 位）→ ulong。非法 hex = 0（= 未绑）。</summary>
    public static ulong GuidOf(string guidHex)
        => guidHex != null &&
           ulong.TryParse(guidHex, System.Globalization.NumberStyles.HexNumber, null, out var v)
            ? v : 0ul;

    /// <summary>绑定动画集（按名解析作用域 = 该集；GUID hex 16 位）。改绑下一
    /// tick 生效（AnimGraphSystem/桥按 tick 读绑定）。</summary>
    public static void SetSet(GameObject g, string setGuidHex)
    {
        if (!g.TryGetComponent<AnimGraph>(out var gr)) { WarnNoGraph(g, "SetSet"); return; }
        gr.SetGuid = GuidOf(setGuidHex);
        g.SetComponent(gr);
    }

    /// <summary>绑定状态机（.controller GUID hex 16 位；AnimGraphSystem 图评估
    /// 消费）。改绑不清 inited——新 controller 参数表下一 tick 起按新词面评估。</summary>
    public static void SetController(GameObject g, string controllerGuidHex)
    {
        if (!g.TryGetComponent<AnimGraph>(out var gr)) { WarnNoGraph(g, "SetController"); return; }
        gr.ControllerGuid = GuidOf(controllerGuidHex);
        g.SetComponent(gr);
    }

    static void WarnNoGraph(GameObject g, string what)
    {
        if (s_paramMissed.Add(g.Entity.Id + ":" + what))
            Console.Error.WriteLine(
                $"[lemon][warn] Anim.{what}: 实体 {g.Entity.Id} 无 AnimGraph 组件 → no-op" +
                $"（编辑期在 Inspector 绑定槽配置）");
    }

    static bool ParamSlot(GameObject g, string name, out int slot)
    {
        slot = Native.AnimParamSlot(g.Entity.Id, name);
        if (slot >= 0) return true;
        if (s_paramMissed.Add(g.Entity.Id + ":param:" + name))
            Console.Error.WriteLine(
                $"[lemon][warn] Anim: 实体 {g.Entity.Id} 参数 '{name}' 解析失败" +
                $"（未绑 controller/词表无名）→ no-op");
        return false;
    }

    static unsafe bool WriteParam(GameObject g, int slot, float value)
    {
        if (!g.TryGetComponent<AnimParams>(out var pm))
        {
            if (s_paramMissed.Add(g.Entity.Id + ":params"))
                Console.Error.WriteLine(
                    $"[lemon][warn] Anim: 实体 {g.Entity.Id} 无 AnimParams 组件 → no-op" +
                    $"（与 AnimGraph 成对配置）");
            return false;
        }
        switch (slot)
        {
            case 0: pm.P0 = value; break;
            case 1: pm.P1 = value; break;
            case 2: pm.P2 = value; break;
            case 3: pm.P3 = value; break;
            case 4: pm.P4 = value; break;
            case 5: pm.P5 = value; break;
            case 6: pm.P6 = value; break;
            case 7: pm.P7 = value; break;
            default: return false;
        }
        g.SetComponent(pm);
        return true;
    }

    static unsafe float ReadParam(GameObject g, int slot)
    {
        if (!g.TryGetComponent<AnimParams>(out var pm)) return 0f;
        return slot switch
        {
            0 => pm.P0, 1 => pm.P1, 2 => pm.P2, 3 => pm.P3,
            4 => pm.P4, 5 => pm.P5, 6 => pm.P6, 7 => pm.P7,
            _ => 0f,
        };
    }

    /// <summary>写 float 参数（图出边条件消费；槽位 = controller 参数表定序）。</summary>
    public static void SetParam(GameObject g, string name, float value)
    {
        if (ParamSlot(g, name, out int slot)) WriteParam(g, slot, value);
    }

    /// <summary>写 bool 参数（0/1）。</summary>
    public static void SetParam(GameObject g, string name, bool value)
        => SetParam(g, name, value ? 1f : 0f);

    /// <summary>读参数（float/bool 同径；无组件/无槽 = 0）。</summary>
    public static float GetParam(GameObject g, string name)
        => ParamSlot(g, name, out int slot) ? ReadParam(g, slot) : 0f;

    /// <summary>激发 trigger（写 1；AnimGraphSystem 评估命中即清——未命中的
    /// trigger 保持待发，跨状态有效，Unity 同款）。</summary>
    public static void Trigger(GameObject g, string name)
        => SetParam(g, name, 1f);

    // ---- M8 批⑬：帧事件命名订阅（AnimFrame 队列上的糖，零新增通道）----------
    // 订阅期一次解析 name→id（AnimEvents 缓存），热路径 m.User 逐位比较 u16。
    // 未注册（表缺失/拼错）= 启动红字 + 该订阅挂空（不影响其他订阅——批⑬a 出口：
    /// 拼错名字不再静默）。**静态订阅**（域级生命周期）：行为体实例请用
    /// LemonBehaviour.OnFrame（OnDestroy 自动退订——裸静态订阅在实例反复生成/销毁
    /// 下无界增长，M15 同病程）。
    public static void OnFrame(GameObject g, string eventName, Action cb)
    {
        if (cb == null || g.Entity.Id == 0) return;
        if (!AnimEvents.TryIdOf(eventName, out ushort id)) {
            AnimEvents.WarnUnresolved("Anim.OnFrame", eventName ?? "");
            return;
        }
        Events.Subscribe(GameEvent.AnimFrame, m => {
            if (m.User == id && m.Src.Id == g.Entity.Id) cb();
        });
    }

    /// <summary>带载荷版（M8 ⑬c）：回调收 GameEventMsg——P0 = 帧号、
    /// P1 = 打点数值载荷（.clip "val"；无 val 旧档 = 0）。</summary>
    public static void OnFrame(GameObject g, string eventName, Action<GameEventMsg> cb)
    {
        if (cb == null || g.Entity.Id == 0) return;
        if (!AnimEvents.TryIdOf(eventName, out ushort id)) {
            AnimEvents.WarnUnresolved("Anim.OnFrame", eventName ?? "");
            return;
        }
        Events.Subscribe(GameEvent.AnimFrame, m => {
            if (m.User == id && m.Src.Id == g.Entity.Id) cb(m);
        });
    }
}
