// Lemon.SDK — GameObject / LemonBehaviour（档①，04 §2.1；Unity 命名级对齐 ADR-009）
// M3 分期口径（ADR-010 D4）：门面核心集；Input/Audio/Assets/Instantiate(prefab) M4+；
// GetComponent 系列是语法糖（低频），热路径用 Chunk span（04 §3）。
using System;
using System.Collections.Generic;
using Lemon.Interop;

namespace Lemon;

/// <summary>实体句柄门面（Unity 正名；底层 EntityHandle u64）。</summary>
public readonly struct GameObject
{
    /// <summary>底层实体句柄（供 SceneOps 等桥 API/事件回读；游戏代码优先用门面方法）。
    /// M5 批④ 起 public：事件回调里 GameObject.From(m.Src) 与自持句柄对账需要读 Id。</summary>
    public readonly EntityHandle Entity;

    internal GameObject(EntityHandle e) { Entity = e; }

    /// <summary>从底层句柄构造门面（M5 批④：事件回调里访问 src/dst 实体的组件——
    /// GameEventMsg.Src/Dst 是裸 EntityHandle）。句柄失效时 Alive=false，自查。</summary>
    public static GameObject From(EntityHandle e) => new(e);

    public bool Alive => Native.IsAlive(Entity.Id) != 0;

    /// <summary>读组件（拷贝语义；语法糖——热路径用 Chunk）。</summary>
    public bool TryGetComponent<T>(out T c) where T : unmanaged => Native.Read(Entity.Id, out c);

    // ---- M6a 批⓪ T3：组件/脚本统一门面（Unity 习惯名，typeof(T) 双路由）----
    // AddComponent/GetComponent/RemoveComponent 一个名字管两类：值组件（IComponent
    // 镜像 struct → op2/3 + 原生读写）∪ 脚本组件（LemonBehaviour 子类 → op4/5 +
    // Behaviours 实例表）。C# 约束限制：unmanaged 不能上提到 notnull 门面 → 值
    // 组件读经开放泛型策略类反射实例化一次（接口直调零装箱；逐 T 静态缓存）。
    // 低频语法糖口径（04 §2.1）；热路径仍推 TryGetComponent/Chunk。

    /// <summary>加组件（结构命令，帧首应用）。值组件 = op2；脚本 = op4（幂等
    /// get-or-add：实例在/待决命令在 = no-op——同类型唯一决策 4 的门面侧闸）。
    /// 未注册脚本/非组件类型 = 抛（注册期错误当场响亮）。</summary>
    public void AddComponent<T>() where T : notnull
    {
        var t = typeof(T);
        if (typeof(LemonBehaviour).IsAssignableFrom(t)) {
            int id = Behaviours.TypeIdOf(t.Name);
            if (id < 0) throw new System.InvalidOperationException(
                $"behaviour '{t.Name}' not registered (Behaviours.Register in GameMain.Configure)");
            if (Behaviours.GetInstance(t.Name, Entity) != null) return;
            if (SceneOps.HasPendingAttach(Entity, id)) return;
            SceneOps.AttachScript(Entity, id);
            return;
        }
        if (ComponentTable.TryId(t, out byte cid)) {
            SceneOps.SubmitRaw((byte)SceneOpType.AddComponent, cid, Entity.Id);
            return;
        }
        throw new System.InvalidOperationException(
            $"AddComponent<{t.Name}>: 既非 IComponent 组件也非 LemonBehaviour");
    }

    /// <summary>读组件。脚本分路 = 挂载实例引用（未挂 = null；含 Disabled，Unity
    /// 同口径；注意帧边界——AddComponent 同帧查 = null）；值组件分路 = 拷贝
    /// （缺 = throw，旧 unmanaged 契约保持）。</summary>
    public T? GetComponent<T>() where T : notnull
    {
        if (Route<T>.IsBehaviour)
            return (T?)(object?)Behaviours.GetInstance(typeof(T).Name, Entity);
        return Route<T>.ReadStruct(Entity.Id);
    }

    /// <summary>移除组件（结构命令，帧首应用）。脚本分路 = op5（单槽：OnDestroy+
    /// 实例级订阅退订）；值组件 = op3。返回 = 是否已入队（类型未注册/未绑定 = false）。</summary>
    public bool RemoveComponent<T>() where T : notnull
    {
        var t = typeof(T);
        if (typeof(LemonBehaviour).IsAssignableFrom(t)) {
            int id = Behaviours.TypeIdOf(t.Name);
            if (id < 0) return false;
            SceneOps.DetachScript(Entity, id);
            return true;
        }
        if (ComponentTable.TryId(t, out byte cid)) {
            SceneOps.SubmitRaw((byte)SceneOpType.RemoveComponent, cid, Entity.Id);
            return true;
        }
        return false;
    }

    /// <summary>双路由判定缓存（逐 T 一次静态构造；反射无逐调用开销）。</summary>
    private static class Route<T> where T : notnull
    {
        public static readonly bool IsBehaviour =
            typeof(LemonBehaviour).IsAssignableFrom(typeof(T));
        private static readonly IStructReader<T>? s_reader = Build();
        private static IStructReader<T>? Build()
        {
            if (IsBehaviour || !typeof(T).IsValueType) return null;
            try {
                // sealed 类实例化必非 null（ArgumentException 分支已拦约束拒绝）
                return (IStructReader<T>)System.Activator.CreateInstance(
                    typeof(StructReader<>).MakeGenericType(typeof(T)))!;
            } catch (System.ArgumentException) {
                return null; // 非 unmanaged/IComponent（MakeGenericType 约束拒绝）
            }
        }
        public static T ReadStruct(ulong e)
        {
            if (s_reader == null) throw new System.InvalidOperationException(
                $"GetComponent<{typeof(T).Name}>: 既非 IComponent 组件也非 LemonBehaviour");
            return s_reader.Read(e);
        }
    }

    private interface IStructReader<T> where T : notnull { T Read(ulong e); }

    private sealed class StructReader<U> : IStructReader<U> where U : unmanaged, IComponent
    {
        public U Read(ulong e)
        {
            if (!Native.Read(e, out U c)) throw new System.InvalidOperationException(
                $"component {typeof(U).Name} missing on entity");
            return c;
        }
    }

    /// <summary>写回组件（整 struct 覆盖；语法糖）。</summary>
    public void SetComponent<T>(in T c) where T : unmanaged => Native.Write(Entity.Id, in c);

    /// <summary>销毁本实体（结构命令，帧首应用；≈ Unity Destroy 的延迟语义）。</summary>
    public void Destroy() => SceneOps.Destroy(Entity);
}

/// <summary>档① 脚本组件基类（≈ Unity MonoBehaviour，ADR-009 命名对齐）。
/// 生命周期（Unity 原名；Update = 60Hz 固定步长，无 FixedUpdate——04 §3.2）。</summary>
public abstract class LemonBehaviour
{
    public GameObject gameObject { get; internal set; }

    protected internal virtual void Awake() { }
    protected internal virtual void OnEnable() { }
    protected internal virtual void Start() { }      // 首帧 Update 之前统一跑
    protected internal virtual void Update() { }     // 60Hz 固定步长（≙ Unity FixedUpdate 语义）
    protected internal virtual void LateUpdate() { } // 同 tick 末尾
    protected internal virtual void OnDestroy() { }
    // 热重载状态迁移（04 §6；M4.5 A 线整域重建）：换装前旧实例写包、新实例读包。
    // 时机：Out = 卸载前（旧域最后一次调用）；In = 新实例 Awake/OnEnable 之后、
    // 首次 Start/Update 之前。仅值类型可入包（StateBag 白名单），其余丢弃。
    protected internal virtual void OnHotReloadOut(StateBag bag) { }
    protected internal virtual void OnHotReloadIn(StateBag bag) { }
    // 触发器/事件回调（OnTriggerEnter/Exit/OnGameEvent）随 M4 事件桥扩展接入

    // ---- 实例级事件订阅（M15）----
    // 惰性分配：订阅发生在挂载/构造期（非每帧），分配不入 GC 热路径纪律账。
    private List<(GameEvent type, Action<GameEventMsg> handler)> _subscriptions;

    /// <summary>订阅事件，本实例 OnDestroy 时自动退订（M15：裸 Events.Subscribe 只增
    /// 不删，行为体按实例订阅（如构造器订阅波次横幅）反复生成/销毁 = 订阅表无界增长
    /// + 根住已毁实例 + 根住旧 ALC 直到下次换域）。静态订阅（GameMain.Configure 等
    /// 域级订阅）不适用本助手，用 Events.Unsubscribe 手动管理。</summary>
    protected void Subscribe(GameEvent type, Action<GameEventMsg> handler)
    {
        Events.Subscribe(type, handler);
        (_subscriptions ??= new List<(GameEvent, Action<GameEventMsg>)>(2)).Add((type, handler));
    }

    /// <summary>Behaviours.Detach 收尾调用：清本实例全部订阅（OnDestroy 后）。</summary>
    internal void ClearSubscriptions()
    {
        if (_subscriptions == null) return;
        foreach (var (type, handler) in _subscriptions) Events.Unsubscribe(type, handler);
        _subscriptions.Clear();
    }

    // ---- 批⑦（ADR-017 D5/D7）：跨场景幸存标记 ------------------------------

    /// <summary>标记跨场景幸存（Unity 手感：派生类内裸调 DontDestroyOnLoad(gameObject)；
    /// D7 静态落点）。仅根生效：非根 = 引擎 WARN 后作用于根树（根树整体幸存，Unity
    /// 兼容）；DDOL 位只落根一点（批⑦ D1 根位式）——后挂子实体随根幸存、移出 DDOL
    /// 树随新归属清场。重复管理器防重 = 游戏侧 if (instance == null) 惯例，引擎不造
    /// 单例基类。</summary>
    public static void DontDestroyOnLoad(GameObject go)
    {
        if (go.Entity.Id == 0) return; // GameObject 为 struct：句柄零 = 无效
        Native.MarkDontDestroyOnLoad(go.Entity.Id);
    }
}
