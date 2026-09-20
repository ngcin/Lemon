// Lemon.SDK — 档① 调度（04 §2.1：注册期位掩码 + dense 分桶 + 异常隔离）
// 执行序（一帧内固定，ADR-010 D1 域线程）：帧首 Essential 结构命令（挂载→Awake/OnEnable）
// → #14：Start（首帧）→ Update → 档② 批量系统 → LateUpdate → #15 事件。
// 位掩码：注册期反射一次算出 override 集——未 override 的生命周期整类零成本（Prowl2D 验证方案）。
using System;
using System.Collections.Generic;
using System.Reflection;
using Lemon.Interop;

namespace Lemon;

[AttributeUsage(AttributeTargets.Class)]
public sealed class ExecutionOrderAttribute : Attribute
{
    public int Order { get; }
    public ExecutionOrderAttribute(int order) { Order = order; }
}

public static class Behaviours
{
    [Flags]
    internal enum LifecycleBits : ushort
    {
        None = 0, Awake = 1 << 0, OnEnable = 1 << 1, Start = 1 << 2, Update = 1 << 3,
        LateUpdate = 1 << 4, OnDestroy = 1 << 5,
    }

    internal sealed class TypeSlot
    {
        public required string Name;
        public required Func<LemonBehaviour> Factory;
        public LifecycleBits Mask;
        public int Order; // [ExecutionOrder]（M3：桶间排序标签，同 Order 按注册序）
        public readonly List<LemonBehaviour> Instances = new();
        public readonly List<bool> StartPending = new();
        public readonly List<int> BadStreak = new();
        public readonly List<bool> Disabled = new();
    }

    internal static readonly List<TypeSlot> Slots = new();
    private static readonly List<TypeSlot> s_ordered = new(); // (Order, 注册序) 预排序：tick 零分配
    private static int s_attached;

    // 热重载待恢复包（M4.5；键 = (类名, 实体 id)）。独立于 Reset()——LoadScript 换域
    // 清注册表时本表必须存活，直到新域 Attach 消费（或下次换装覆盖）。
    private static readonly Dictionary<(string Class, ulong Entity), StateBag> s_hotBags = new();
    private static int s_hotDropped; // In 阶段缺键/类型不匹配丢弃计数（诊断）

    /// <summary>换装前捕获全部实例的可迁移状态（域线程；DomainManager.ReloadScript 调）。</summary>
    internal static int CaptureForHotReload()
    {
        // 上轮未消费的包 = 实例已不存在或类被删 → 计丢弃并清场
        s_hotDropped += s_hotBags.Count;
        s_hotBags.Clear();
        int n = 0;
        foreach (var slot in Slots)
            foreach (var b in slot.Instances) {
                var bag = new StateBag();
                try { b.OnHotReloadOut(bag); } // 异常不阻断换装：丢弃该实例状态
                catch { continue; }
                if (bag.Count > 0) {
                    s_hotBags[(slot.Name, b.gameObject.Entity.Id)] = bag;
                    ++n;
                }
            }
        return n;
    }

    /// <summary>诊断：上次换装 In 阶段丢弃的字段数（类型不匹配/缺失）。</summary>
    public static int HotReloadDroppedFields => s_hotDropped;

    private static void RebuildOrder()
    {
        s_ordered.Clear();
        for (int pass = 0; pass < 2; pass++) // pass0: Order==0（默认），pass1: 显式 Order
            for (int i = 0; i < Slots.Count; i++)
                if ((Slots[i].Order == 0) == (pass == 0)) s_ordered.Add(Slots[i]);
    }

    /// <summary>注册脚本类型（GameMain.Configure 内调用；typeId = 注册序，跨帧稳定）。</summary>
    public static void Register<T>() where T : LemonBehaviour, new()
    {
        LifecycleBits mask = LifecycleBits.None;
        var t = typeof(T);
        MethodInfo? OverrideOf(string name)
        {
            var m = t.GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic |
                                       BindingFlags.Public,
                                binder: null, Type.EmptyTypes, modifiers: null);
            return m is { IsVirtual: true } && m.DeclaringType != typeof(LemonBehaviour) ? m : null;
        }
        if (OverrideOf("Awake") != null) mask |= LifecycleBits.Awake;
        if (OverrideOf("OnEnable") != null) mask |= LifecycleBits.OnEnable;
        if (OverrideOf("Start") != null) mask |= LifecycleBits.Start;
        if (OverrideOf("Update") != null) mask |= LifecycleBits.Update;
        if (OverrideOf("LateUpdate") != null) mask |= LifecycleBits.LateUpdate;
        if (OverrideOf("OnDestroy") != null) mask |= LifecycleBits.OnDestroy;

        Slots.Add(new TypeSlot {
            Name = t.Name,
            Factory = () => new T(),
            Mask = mask,
            Order = t.GetCustomAttribute<ExecutionOrderAttribute>()?.Order ?? 0,
        });
        RebuildOrder();
    }

    public static int TypeCount => Slots.Count;

    /// <summary>类型名表（M4.4 编辑器装配通路：Inspector 列表/className 解析）。</summary>
    public static string[] RegisteredNames
    {
        get {
            var names = new string[Slots.Count];
            for (int i = 0; i < Slots.Count; i++) names[i] = Slots[i].Name;
            return names;
        }
    }

    /// <summary>类名 → typeId（注册序；不存在 = -1）。</summary>
    public static int TypeIdOf(string name)
    {
        for (int i = 0; i < Slots.Count; i++)
            if (Slots[i].Name == name) return i;
        return -1;
    }

    /// <summary>类型 → typeId（泛型版；未注册 = -1）。</summary>
    public static int TypeIdOf<T>() where T : LemonBehaviour, new() => TypeIdOf(typeof(T).Name);

    /// <summary>挂载（结构命令 AttachScript 应用时由 Entry 调用；域线程）。</summary>
    internal static void Attach(int typeId, EntityHandle e)
    {
        if ((uint)typeId >= (uint)Slots.Count) return;
        var slot = Slots[typeId];
        var b = slot.Factory();
        b.gameObject = new GameObject(e);
        int idx = slot.Instances.Count;
        slot.Instances.Add(b);
        slot.StartPending.Add(true);
        slot.BadStreak.Add(0);
        slot.Disabled.Add(false);
        ++s_attached;
        SafeCall(slot, idx, b, LifecycleBits.Awake);
        SafeCall(slot, idx, b, LifecycleBits.OnEnable);
        // 热重载恢复（M4.5）：同 (类名, 实体) 的待恢复包 → OnHotReloadIn。
        // Start/Update 照常跑（需要保持的状态由脚本自己写进包；Awake/In 之后 Start 之前）。
        if (s_hotBags.Remove((slot.Name, e.Id), out var bag)) {
            try { b.OnHotReloadIn(bag); }
            catch (Exception ex) {
                Console.Error.WriteLine($"[lemon][error] behaviour '{slot.Name}' OnHotReloadIn: {ex.Message}");
            }
        }
    }

    /// <summary>卸载（Destroy 命令应用时由 Entry 调用；域线程）。实体销毁 → OnDestroy。</summary>
    internal static void Detach(EntityHandle e)
    {
        foreach (var slot in Slots)
            for (int i = slot.Instances.Count - 1; i >= 0; i--) {
                if (slot.Instances[i].gameObject.Entity.Id != e.Id) continue;
                if (!slot.Disabled[i]) SafeCall(slot, i, slot.Instances[i], LifecycleBits.OnDestroy);
                slot.Instances.RemoveAt(i);
                slot.StartPending.RemoveAt(i);
                slot.BadStreak.RemoveAt(i);
                slot.Disabled.RemoveAt(i);
                --s_attached;
            }
    }

    /// <summary>诊断：活动实例数。</summary>
    public static int AttachedCount => s_attached;

    internal static void TickStartUpdate(float dt)
    {
        foreach (var slot in Ordered())
            for (int i = 0; i < slot.Instances.Count; i++) {
                if (slot.Disabled[i]) continue;
                if (slot.StartPending[i]) {
                    slot.StartPending[i] = false;
                    SafeCall(slot, i, slot.Instances[i], LifecycleBits.Start);
                }
                SafeCall(slot, i, slot.Instances[i], LifecycleBits.Update);
            }
    }

    internal static void TickLateUpdate(float dt)
    {
        foreach (var slot in Ordered())
            for (int i = 0; i < slot.Instances.Count; i++)
                if (!slot.Disabled[i])
                    SafeCall(slot, i, slot.Instances[i], LifecycleBits.LateUpdate);
    }

    // dense 分桶：桶按 (ExecutionOrder, 注册序) 固定排序（确定性）；桶内插入序。
    // 预排序 + 索引遍历（迭代器版本每帧堆分配枚举器 ~258B——GC 判据实测抓出，M3-7）
    private static List<TypeSlot> Ordered() => s_ordered;

    private const int kDisableAfter = 60; // 04 §7：连续 60 帧异常自动禁用

    private static void SafeCall(TypeSlot slot, int i, LemonBehaviour b, LifecycleBits bit)
    {
        if ((slot.Mask & bit) == 0) return; // 未 override：整类零成本
        try {
            switch (bit) {
            case LifecycleBits.Awake: b.Awake(); break;
            case LifecycleBits.OnEnable: b.OnEnable(); break;
            case LifecycleBits.Start: b.Start(); break;
            case LifecycleBits.Update: b.Update(); break;
            case LifecycleBits.LateUpdate: b.LateUpdate(); break;
            case LifecycleBits.OnDestroy: b.OnDestroy(); break;
            }
            slot.BadStreak[i] = 0;
        } catch (Exception e) {
            Console.Error.WriteLine($"[lemon][error] behaviour '{slot.Name}' {bit}: {e.Message}");
            if (++slot.BadStreak[i] >= kDisableAfter) {
                slot.Disabled[i] = true;
                Console.Error.WriteLine($"[lemon][error] behaviour '{slot.Name}' disabled after " +
                                        $"{kDisableAfter} failing frames");
            }
        }
    }

    internal static void Reset()
    {
        Slots.Clear();
        s_ordered.Clear();
        s_attached = 0;
    }
}
