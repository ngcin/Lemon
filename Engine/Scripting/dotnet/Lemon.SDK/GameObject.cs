// Lemon.SDK — GameObject / LemonBehaviour（档①，04 §2.1；Unity 命名级对齐 ADR-009）
// M3 分期口径（ADR-010 D4）：门面核心集；Input/Audio/Assets/Instantiate(prefab) M4+；
// GetComponent 系列是语法糖（低频），热路径用 Chunk span（04 §3）。
using Lemon.Interop;

namespace Lemon;

/// <summary>实体句柄门面（Unity 正名；底层 EntityHandle u64）。</summary>
public readonly struct GameObject
{
    /// <summary>底层实体句柄（供 SceneOps 等桥 API；游戏代码用 GameObject 门面方法）。</summary>
    internal readonly EntityHandle Entity;

    internal GameObject(EntityHandle e) { Entity = e; }

    public bool Alive => Native.IsAlive(Entity.Id) != 0;

    /// <summary>读组件（拷贝语义；语法糖——热路径用 Chunk）。</summary>
    public bool TryGetComponent<T>(out T c) where T : unmanaged => Native.Read(Entity.Id, out c);

    public T GetComponent<T>() where T : unmanaged
    {
        if (!Native.Read(Entity.Id, out T c)) throw new System.InvalidOperationException(
            $"component {typeof(T).Name} missing on entity");
        return c;
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
}
