// Lemon.SDK — 帧事件注册表面（M8 批⑬；Assets/Tables/anim-events.tab 单一权威）--
// 治魔法数字 id（clip 写 1、脚本订 2 = 全场静默无事发生）：名字装载期解析为 u16
// id，热路径仍是 GameEvent.AnimFrame 队列逐位比较（零新增通道、零反射零字符串
// ——金回放零重录前提）。id 由注册表插入时分配、永不变更 → 命中缓存安全。
//   * Define(name)：GameMain.Configure 对账——声明本游戏消费的事件名并预解析；
//     未注册（表缺失/名字拼错）= 启动红字（批⑬a 出口：不再静默）。
//   * 以表为准：SDK 侧不另立 id 源，「Define/表 双源冲突」收窄为两类红字——
//     Define/OnFrame 名字未注册（本层）与表内同名/同 id 行冲突（引擎装载期）。
//   * 换域清缓存（Events.Reset 同窗口）；PlayReset 不清（id 稳定 + 表为快照，
//     跨局订阅续用）。
using System;
using System.Collections.Generic;

namespace Lemon;

/// <summary>帧事件名门面：Configure 期 Define 对账 + 订阅期名字解析缓存。</summary>
public static class AnimEvents
{
    /// <summary>已解析命中缓存（名字 → id）。id 分配后永不变更 = 缓存安全；
    /// 未命中不缓存（表可后到/编辑器重扫——重试桥零成本，调用点本身 warn-once）。</summary>
    private static readonly Dictionary<string, ushort> s_ids = new();

    /// <summary>Define 声明面（去重序保持）——批⑬b 反向 lint 的订阅者集合来源之一。</summary>
    private static readonly List<string> s_defined = new();

    /// <summary>红字去重（每名字一次）。</summary>
    private static readonly HashSet<string> s_warned = new();

    /// <summary>声明消费事件并预解析（GameMain.Configure 期调用）。
    /// 返回 false = 未注册（启动红字一次：表缺失或名字拼错——修表或改拼写）。
    /// 声明面本身不拦运行（OnFrame 未声明也可用；声明收集归 lint ②）。</summary>
    public static bool Define(string name)
    {
        if (!s_defined.Contains(name)) s_defined.Add(name);
        if (TryIdOf(name, out ushort _)) return true;
        if (s_warned.Add("define:" + name))
            Console.Error.WriteLine(
                $"[lemon][error] AnimEvents.Define: 事件 '{name ?? "(null)"}' 未注册" +
                $"（Assets/Tables/anim-events.tab 无此行或表缺失）——Anim.OnFrame 订阅将挂空");
        return false;
    }

    /// <summary>订阅者声明面（批⑬b lint 用；批⑬a 起 Define/OnFrame 共同维护）。</summary>
    internal static IReadOnlyList<string> DefinedNames => s_defined;

    /// <summary>名字 → id。命中走缓存；未命中问桥（装载期低频，Anim.OnFrame/
    /// Define 调用点各自红字去重）。</summary>
    internal static bool TryIdOf(string name, out ushort id)
    {
        if (name != null && s_ids.TryGetValue(name, out ushort hit)) { id = hit; return true; }
        id = 0;
        if (name == null) return false;
        int v = Native.AnimEventIdByName(name);
        if (v <= 0 || v > 65535) return false;
        id = (ushort)v;
        s_ids[name] = id; // id 永不变更：命中缓存一次终身有效（换域清）
        return true;
    }

    /// <summary>换域清缓存（DomainManager.LoadScript 调 Events.Reset 同批）：
    /// 新域 Configure 重跑 Define，旧域解析态不得跨域。</summary>
    internal static void Reset()
    {
        s_ids.Clear();
        s_defined.Clear();
        s_warned.Clear();
    }

    /// <summary>订阅期红字（Anim.OnFrame 共用）：未注册 = 该订阅挂空（不影响其他订阅）。</summary>
    internal static void WarnUnresolved(string api, string name)
    {
        if (s_warned.Add("onframe:" + name))
            Console.Error.WriteLine(
                $"[lemon][error] {api}: 事件 '{name ?? "(null)"}' 未注册" +
                $"（Assets/Tables/anim-events.tab 无此行或表缺失）——该订阅挂空");
    }
}
