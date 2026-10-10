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
        public required System.Type Type; // 同名冲突判据（#58：t.Name 身份 + FullName 分辨）
        public LifecycleBits Mask;
        public int Order; // [ExecutionOrder]（数值排序键，同 Order 按注册序）
        public readonly List<LemonBehaviour> Instances = new();
        public readonly List<bool> StartPending = new();
        public readonly List<int> BadStreak = new();
        public readonly List<bool> Disabled = new();
        // 实体 id → Instances 下标（review 2026-10-09 #M12）：Detach 原对全部类型槽
        // ×全实例线性扫描，批量销毁 N 实体 = O(N×总实例数)（清屏双重放大）。
        // 下标随移除平移同步修正（与 RemoveAt 平移同阶 = 保序删除的理论下界——
        // swap-remove 会乱 tick 序，不可用）。同实体同槽唯一（Attach 红字拒绝双挂）
        // = 每实体每槽至多一条目。
        public readonly Dictionary<ulong, int> ByEntity = new();
    }

    internal static readonly List<TypeSlot> Slots = new();
    private static readonly List<TypeSlot> s_ordered = new(); // (Order, 注册序) 预排序：tick 零分配
    private static int s_attached;

    // 实体 → 槽号集合（#M12 外层索引）：槽号在实例生命周期内稳定（不随槽内
    // RemoveAt 漂移）；Detach 据此跳过无关槽（原实现每实体扫全部类型槽）。
    // List 池化复用——批量销毁路径稳态零分配。
    private static readonly Dictionary<ulong, List<int>> s_slotsByEntity = new();
    private static readonly Stack<List<int>> s_slotListPool = new();

    private static List<int> SlotsOf(ulong id)
    {
        if (!s_slotsByEntity.TryGetValue(id, out var list)) {
            list = s_slotListPool.Count > 0 ? s_slotListPool.Pop() : new List<int>(4);
            s_slotsByEntity[id] = list;
        }
        return list;
    }

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
        // (Order, 注册序) 数值全序（Prowl2D/Unity 同名特性语义；#17——原两桶
        // 分法把 Order 当 0/非0 标签，负 Order 不会早于默认桶、非零 Order 之间
        // 不比较，与注释承诺不符）。索引排序保证确定性且比较器无 O(n) 查找。
        s_ordered.Clear();
        var idx = new List<int>(Slots.Count);
        for (int i = 0; i < Slots.Count; i++) idx.Add(i);
        idx.Sort((a, b) => Slots[a].Order != Slots[b].Order
            ? Slots[a].Order.CompareTo(Slots[b].Order) : a.CompareTo(b));
        foreach (var i in idx) s_ordered.Add(Slots[i]);
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

        // 类型身份 = 裸类名 t.Name（.scene className 持久键，04 M4.4）。跨命名空间
        // 同名类并存会让 TypeIdOf 静默路由到先注册者（#58）——当场响亮拒绝第二个。
        for (int i = 0; i < Slots.Count; i++) {
            if (Slots[i].Name != t.Name) continue;
            if (Slots[i].Type == t) return; // 同类型重复注册 = 幂等 no-op
            Console.Error.WriteLine(
                $"[lemon][error] Behaviours.Register: 类名 '{t.Name}' 冲突" +
                $"（{Slots[i].Type.FullName} 已注册；{t.FullName} 未注册）——类型标识按裸类名，同名不同命名空间不可并存");
            return;
        }

        Slots.Add(new TypeSlot {
            Name = t.Name,
            Factory = () => new T(),
            Type = t,
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

    /// <summary>挂载（结构命令 AttachScript 应用时由 Entry 调用；域线程）。
    /// 同实体同类型唯一（M6a 批⓪ 决策 4）：已挂 = 红字跳过——正常路径不会走到
    /// （EnterPlay 前 ClearInstances、热重载前换域清表、AddComponent 门面幂等），
    /// 命中即生命周期泄漏，当场响亮优于静默双实例双 tick（VS 模板 +2 刃事故类）。</summary>
    internal static void Attach(int typeId, EntityHandle e)
    {
        if ((uint)typeId >= (uint)Slots.Count) return;
        var slot = Slots[typeId];
        for (int i = 0; i < slot.Instances.Count; i++)
            if (slot.Instances[i].gameObject.Entity.Id == e.Id) {
                Console.Error.WriteLine(
                    $"[lemon][error] behaviour '{slot.Name}' already attached to entity " +
                    $"{e.Id} — duplicate attach skipped (lifecycle leak?)");
                return;
            }
        LemonBehaviour b;
        try { b = slot.Factory(); } // 用户构造器（#15）：唯一无护栏生命周期——红字跳过，
        catch (Exception ex) {      // C++ 侧槽已写、实例未挂 = 脚本哑火，必须可见
            Console.Error.WriteLine( // 全异常链（批⑨ 后修：TargetInvocation 等包装的
                $"[lemon][error] behaviour '{slot.Name}' 构造器异常（实例未挂载）: {ex}"); // 内层才是真因）
            return;
        }
        b.gameObject = new GameObject(e);
        int idx = slot.Instances.Count;
        slot.Instances.Add(b);
        slot.StartPending.Add(true);
        slot.BadStreak.Add(0);
        slot.Disabled.Add(false);
        slot.ByEntity[e.Id] = idx;   // #M12 双层索引登记（槽内下标 + 槽号）
        SlotsOf(e.Id).Add(typeId);
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

    /// <summary>统一实例移除（#M12 索引化）：OnDestroy（未禁用）+ 订阅退订 + 四表
    /// RemoveAt + 双层索引修正（槽内 ByEntity 平移修正、全局槽号表摘除——空表
    /// 归还池）。slotIdx = Slots 下标（全局表键值）。</summary>
    private static void RemoveInstance(int slotIdx, TypeSlot slot, int idx, ulong id)
    {
        if (!slot.Disabled[idx]) SafeCall(slot, idx, slot.Instances[idx], LifecycleBits.OnDestroy);
        slot.Instances[idx].ClearSubscriptions(); // M15：实例级订阅随实例退订
        slot.Instances.RemoveAt(idx);
        slot.StartPending.RemoveAt(idx);
        slot.BadStreak.RemoveAt(idx);
        slot.Disabled.RemoveAt(idx);
        slot.ByEntity.Remove(id);
        for (int j = idx; j < slot.Instances.Count; j++) // 平移修正（与 RemoveAt 同阶）
            slot.ByEntity[slot.Instances[j].gameObject.Entity.Id] = j;
        if (s_slotsByEntity.TryGetValue(id, out var slots)) {
            slots.Remove(slotIdx);
            if (slots.Count == 0) {
                s_slotsByEntity.Remove(id);
                slots.Clear();
                s_slotListPool.Push(slots);
            }
        }
        --s_attached;
    }

    /// <summary>卸载（Destroy 命令应用时由 Entry 调用；域线程）。实体销毁 → OnDestroy。
    /// 销毁序 = 槽注册序升序（同实体同槽唯一）——与原全扫实现逐位同构（回放
    /// 确定性：OnDestroy 的 op 入队/事件推/native RNG 消耗序不可变）。</summary>
    internal static void Detach(EntityHandle e)
    {
        if (!s_slotsByEntity.TryGetValue(e.Id, out var slots) || slots.Count == 0) return;
        // 降序排序 + 倒序循环 = 升序处理序，且 RemoveInstance 摘当前项时移除的
        // 恒为已处理侧尾部（不打扰未处理下标）。R-a1（b11c review）：原实现升序
        // Sort + 倒序循环 = 降序执行，多槽实体 OnDestroy 序倒序 = 回放确定性破坏。
        slots.Sort((a, b) => b.CompareTo(a));
        for (int i = slots.Count - 1; i >= 0; i--) {
            int s = slots[i];
            if (!Slots[s].ByEntity.TryGetValue(e.Id, out int idx)) continue; // 防御（恒命中）
            RemoveInstance(s, Slots[s], idx, e.Id);
        }
    }

    /// <summary>批量销毁（review 2026-10-09 #M11：C++ 侧连续段/待销毁收集单次投递）。
    /// 序 = 输入实体序 × 槽注册序——与逐实体 Detach 逐位同构（OnDestroy 可观察序
    /// 不变，回放确定性）。</summary>
    internal static void DetachBatch(ReadOnlySpan<EntityHandle> ents)
    {
        foreach (ref readonly var e in ents) Detach(e);
    }

    /// <summary>单类型卸载（M6a 批⓪ T3：op5 DetachScript → lemon_scripts_detach 路径；
    /// GameObject.RemoveComponent 脚本分路）。只卸 (typeId, 实体) 一槽实例：OnDestroy
    /// + 实例级订阅退订；未挂/已卸 = 幂等 no-op。</summary>
    internal static void DetachOne(int typeId, EntityHandle e)
    {
        if ((uint)typeId >= (uint)Slots.Count) return;
        var slot = Slots[typeId];
        if (!slot.ByEntity.TryGetValue(e.Id, out int idx)) return;
        RemoveInstance(typeId, slot, idx, e.Id);
    }

    /// <summary>实例查询（M6a 批⓪ T3：GameObject.GetComponent 脚本分路）。
    /// 未注册/未挂 = null；含 Disabled 实例（Unity GetComponent 同口径）。
    /// 注意帧边界：AddComponent 命令帧首才应用，同帧 GetComponent = null。</summary>
    internal static LemonBehaviour? GetInstance(string className, EntityHandle e)
    {
        int id = TypeIdOf(className);
        if (id < 0) return null;
        var slot = Slots[id];
        return slot.ByEntity.TryGetValue(e.Id, out int idx) ? slot.Instances[idx] : null;
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
        ClearEntityIndex();
    }

    /// <summary>硬清实例表（编辑器 EnterPlay 期由 lemon_play_reset 调；M5 批④后修）。
    /// 根因：Stop 弃 playWorld 时 C# 侧无人 Detach（Detach 只挂单实体 Destroy 命令
    /// 路径），下次 EnterPlay 同实体 id 再 Attach = 同实体双实例双 tick（VS 模板
    /// 实测：每局 +2 环绕刃、击杀宝石翻倍）。语义 = Unity "Enter Play = 新域"
    /// 的实例面：只清实例/热重载包，类型注册表保留（同域未换装，免重 Configure）；
    /// OnDestroy 不调——实体已随旧 World 销毁，句柄悬空，调了只会对死世界读写
    /// （与 LoadScript 的 Behaviours.Reset 同口径）。实例级订阅逐实例退订（勿整表
    /// Events.Reset——那会连带清掉 Configure 期静态订阅，Stop→Play 后全哑）。</summary>
    internal static void ClearInstances()
    {
        foreach (var slot in Slots) {
            foreach (var b in slot.Instances) b.ClearSubscriptions();
            slot.Instances.Clear();
            slot.StartPending.Clear();
            slot.BadStreak.Clear();
            slot.Disabled.Clear();
            slot.ByEntity.Clear();
        }
        s_attached = 0;
        s_hotBags.Clear(); // 跨局残留的待恢复包 = 状态串局（同键 (类名,实体)）
        ClearEntityIndex();
    }

    /// <summary>实体索引整体清场（#M12）：槽号表清空归还池（槽内 ByEntity 随槽对象
    /// Clear/丢弃，由调用方处理——Reset 丢弃槽对象、ClearInstances 逐槽清）。</summary>
    private static void ClearEntityIndex()
    {
        foreach (var list in s_slotsByEntity.Values) {
            list.Clear();
            s_slotListPool.Push(list);
        }
        s_slotsByEntity.Clear();
    }
}
