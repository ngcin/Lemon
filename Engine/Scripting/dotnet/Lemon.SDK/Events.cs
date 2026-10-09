// Lemon.SDK — 事件桥（04 §4：帧末一次 drain，C# 批量拷走后托管内分发）
// 数据流：C++ #15 拉取脚本 pending（当帧入队）→ sink 派发全队列 → 两段零拷贝转发 C#。
// Push 语义：脚本 push 的事件经 #15 头部拉取，当帧派发（与 C++ 系统同帧同队列）。
// GC 纪律（04 §5）：事件全 struct 化，订阅走静态表（无每帧分配）。
using System;
using System.Collections.Generic;
using Lemon.Interop;

namespace Lemon;

/// <summary>事件托管视图（EventPacket 的只读门面；struct 零分配）。</summary>
public readonly unsafe struct GameEventMsg
{
    public readonly GameEvent Type;
    public readonly ushort User;
    public readonly EntityHandle Src, Dst;
    public readonly float P0, P1, P2, P3;
    public readonly ulong UserArg;

    internal GameEventMsg(in EventPacket p)
    {
        Type = p.Type; User = p.User; Src = p.Src; Dst = p.Dst;
        P0 = p.Payload[0]; P1 = p.Payload[1]; P2 = p.Payload[2]; P3 = p.Payload[3];
        UserArg = p.UserArg;
    }
}

public static unsafe class Events
{
    private static readonly List<Action<GameEventMsg>>[] s_handlers =
        new List<Action<GameEventMsg>>[16]; // GameEvent 上限（Custom 区往后扩按需）

    private static readonly List<EventPacket> s_pending = new(); // 脚本 push → #15 头部拉取
    private static readonly object s_pendingLock = new();
    private static readonly int[] s_received = new int[16];

    public static void Subscribe(GameEvent type, Action<GameEventMsg> handler)
    {
        int t = (int)type;
        (s_handlers[t] ??= new List<Action<GameEventMsg>>(4)).Add(handler);
    }

    /// <summary>退订（M15：原只增不删——按实例订阅的行为体反复生成/销毁 =
    /// 订阅表无界增长 + 根住已毁实例。behaviour 内订阅优先用 LemonBehaviour.Subscribe
    /// 助手（OnDestroy 自动退订）；本方法供静态订阅/手动管理用）。</summary>
    public static void Unsubscribe(GameEvent type, Action<GameEventMsg> handler)
    {
        s_handlers[(int)type]?.Remove(handler);
    }

    /// <summary>脚本推事件（Custom 用户区：user = 资产注册 id）。当帧 #15 派发。</summary>
    public static void Push(GameEvent type, ushort user, EntityHandle src, EntityHandle dst,
                            float p0 = 0, float p1 = 0, float p2 = 0, float p3 = 0,
                            ulong userArg = 0)
    {
        EventPacket p = default;
        p.Type = type; p.User = user; p.Src = src; p.Dst = dst;
        p.Payload[0] = p0; p.Payload[1] = p1; p.Payload[2] = p2; p.Payload[3] = p3;
        p.UserArg = userArg;
        lock (s_pendingLock) s_pending.Add(p);
    }

    /// <summary>诊断：某类型累计派发数（测试用）。</summary>
    public static int ReceivedCount(GameEvent type) => s_received[(int)type];

    // ---- Lemon.Entry 域线程侧 ----
    internal static unsafe void DispatchPackets(EventPacket* pkts, int n)
    {
        for (int i = 0; i < n; i++) {
            EventPacket* p = pkts + i;
            int t = (int)p->Type;
            s_received[t < 16 ? t : 15]++;
            var list = t < 16 ? s_handlers[t] : null;
            if (list == null) continue; // 无订阅者类型跳过（04 §4 预筛）
            var msg = new GameEventMsg(*p);
            foreach (var h in list) {
                try { h(msg); }
                catch (Exception e) { // 异常隔离（04 §7）：单订阅者异常不阻断派发
                    Console.Error.WriteLine($"[lemon][error] event handler {t}: {e.Message}");
                }
            }
        }
    }

    /// <summary>#15 头部拉取脚本 pending（拷入调用方缓冲后清空）。</summary>
    internal static unsafe int PullPending(EventPacket* dst, int cap)
    {
        lock (s_pendingLock) {
            int n = Math.Min(s_pending.Count, cap);
            for (int i = 0; i < n; i++) dst[i] = s_pending[i];
            if (n == s_pending.Count) s_pending.Clear();
            else s_pending.RemoveRange(0, n);
            return n;
        }
    }

    /// <summary>换域清空（DomainManager.LoadScript 调用）：订阅表/待发/计数全清
    /// ——新域 Configure 重跑，旧域静态订阅必须随域退役。</summary>
    internal static void Reset()
    {
        lock (s_pendingLock) s_pending.Clear();
        for (int i = 0; i < s_handlers.Length; i++) s_handlers[i]?.Clear();
        for (int i = 0; i < s_received.Length; i++) s_received[i] = 0;
    }

    /// <summary>进 Play 域复位（lemon_play_reset 调）：只清待发队列与诊断计数，
    /// 保留订阅表——同域未换装，静态订阅（GameMain.Configure 期注册）须跨局存活
    /// （原误用 Reset：Stop→Play 后静态订阅全哑）。实例级订阅由
    /// Behaviours.ClearInstances 逐实例 ClearSubscriptions 退订，不依赖本方法。</summary>
    internal static void PlayReset()
    {
        lock (s_pendingLock) s_pending.Clear();
        for (int i = 0; i < s_received.Length; i++) s_received[i] = 0;
        // 批⑨ 后补：用户静态跨局复位通道。静态默认随域存活（Stop→Play 不清）——
        // 「每局一份」语义的静态（流程壳守卫/DDOL 驱动句柄等）没有自清时机
        // （ClearInstances 只清 SDK 实例表、OnDestroy 在 Stop 快拆不达），真机
        // 首例 = svr-test 二次 Play 全灭（批⑨ 真人走查）。订阅面 = Configure 期
        // 静态订阅（跨局存活，与事件订阅同生命周期）。逐订阅异常隔离（红字保进程）。
        var hook = PlayResetHook;
        if (hook == null) return;
        foreach (Action h in hook.GetInvocationList())
            try { h(); }
            catch (Exception ex) {
                Console.Error.WriteLine("[lemon][error] PlayResetHook 订阅异常（已拦）：" +
                                        ex.Message);
            }
    }

    /// <summary>进 Play 域复位广播（每局一次，装配新实例前）。「每局一份」静态的
    /// 自清通道——订阅归 GameMain.Configure（跨局存活面）。批⑨ 后补。</summary>
    public static event Action? PlayResetHook;
}
