// Lemon.SDK — 31 组件镜像 struct（与 lemon::ecs 组件头逐字节对齐；M3-1 布局护栏锁定）。
// 纪律：
//   * 字段顺序/类型与 ComponentCatalog.cpp 登记表 1:1（改动 = 破回放，须两侧同步）；
//   * 布局一致性由 lemon-script-tests 双向校验（C# 报告表 vs C++ ComponentMeta），
//     手写镜像 + 自动校验；代码生成（Source Generator）按 04 §3 留 M4；
//   * C# fixed 缓冲只支持基元类型 → StatusEffects/Inventory 的结构数组用不透明字节块
//     镜像（总尺寸由布局校验兜底；typed 访问随 M3-3 Chunk API 提供手动偏移版本）。
using System.Runtime.InteropServices;

namespace Lemon.Interop;

/// <summary>组件标记接口（M6a 批⓪ T3）：镜像 struct 实现之，供 GameObject 统一
/// 门面（AddComponent/GetComponent/RemoveComponent&lt;T&gt;）与 LemonBehaviour 分路。
/// 纯标记（零成员）——不参与布局/桥协议，LayoutTables 探针口径不变。</summary>
public interface IComponent { }

/// <summary>实体句柄（镜像 lemon::ecs::Entity：u64，0 = null，低 32 位=entt+1）。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EntityHandle
{
    public ulong Id;
    public static EntityHandle Null => default;
    public readonly bool IsNull => Id == 0;
}

// ---- Core（注册 id 0..4）----
[StructLayout(LayoutKind.Sequential)]
public struct Transform2D : IComponent   // 20B
{
    public Vec2 Pos;
    public float Rot;       // 弧度
    public Vec2 Scale;
}

[StructLayout(LayoutKind.Sequential)]
public struct Velocity : IComponent      // 8B
{
    public Vec2 V;
}

[StructLayout(LayoutKind.Sequential)]
public struct Hierarchy : IComponent     // 32B
{
    public EntityHandle Parent, FirstChild, Next, Prev;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Meta : IComponent   // 48B（M4.1 增 Guid，C++ CoreComponents.h 同步）
{
    public ulong PrefabId;
    public uint Team;
    public ushort Layer;
    public fixed byte Tag[24]; // Blob24：短标签（NUL 结尾约定同 C++）
    public ulong Guid;         // 持久实体 GUID（0 = 运行时生成实体）
}

[StructLayout(LayoutKind.Sequential)]
public struct DestroyQueueTag : IComponent // 1B（空 tag 组件）
{
}

// ---- Render（注册 id 5..8）----
[StructLayout(LayoutKind.Sequential)]
public struct SpriteRenderer : IComponent // 24B（M6a 批⓪：尾加 SpriteGuid，12→24 与 C++ 同步）
{
    public uint SpriteId;     // 进程内派生缓存；真源 = SpriteGuid（装载期归一）
    public uint ColorRGBA;
    public short SortOrder;
    public byte SortingLayer;
    public byte Flags;        // bit0 flipX, bit1 flipY, bit2 enabled。
                              // 注意：引擎侧新增组件默认启用（RenderComponents.h），
                              // 但 C# default(SpriteRenderer) 是零值 = 禁用——
                              // SetComponent 整写前须置 Flags = 0x4（否则不渲染）。
    public ulong SpriteGuid;  // 资产 GUID（0 = 存量未回填；脚本侧通常只读）
}

[StructLayout(LayoutKind.Sequential)]
public struct Animator2D : IComponent     // 28B（M6a 批①：尾加换段队列 16→28 与 C++ 同步）
{
    public uint ClipId;
    public float Time;
    public float Speed;
    public byte Loop;          // T3b-2：LoopMode（0=Once/1=Loop/2=PingPong；见 Lemon.Anim.LoopMode）
    public byte PlayOnStart;
    public ushort CurFrame;
    public uint NextClipId;    // 换段队列目标（0 = 无；Lemon.Anim 写）
    public float FadeRemain;   // <0 = Queue（收尾/回绕点切）；>0 = CrossFade 倒计时
    public ushort NextLoop;    // 切换时写入 Loop
    public byte Ended;         // T3d 批③：段末边沿（非 loop 段收尾 = 1；0→1 发事件）
    internal byte _pad2b;      // C++ _pad 衬齐（28B 不变）
}

[StructLayout(LayoutKind.Sequential)]
public struct ParticleEmitterRef : IComponent // 8B
{
    public uint EmitterId;
    public byte Playing;
}

[StructLayout(LayoutKind.Sequential)]
public struct SortingOverride : IComponent // 2B
{
    public short Order;
}

// ---- Behavior（注册 id 9..20）----
[StructLayout(LayoutKind.Sequential)]
public struct Health : IComponent         // 16B（M5 批⓪ 增 IFrameWindow）
{
    public float Max, Cur, IFrames, IFrameWindow; // iFrames 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Mover : IComponent          // 4B
{
    public float Speed;
}

[StructLayout(LayoutKind.Sequential)]
public struct Patrol : IComponent         // 24B
{
    public Vec2 A, B;
    public float PauseTime;
    public byte HeadingToB;
}

[StructLayout(LayoutKind.Sequential)]
public struct Chase : IComponent          // 24B
{
    public float Speed, AggroRange, KeepRange;
    public uint TargetTeam;
    public EntityHandle Target; // 运行时缓存（kFieldRuntime）
}

[StructLayout(LayoutKind.Sequential)]
public struct Flee : IComponent           // 8B
{
    public float Speed, Range;
}

[StructLayout(LayoutKind.Sequential)]
public struct Shooter : IComponent        // 32B
{
    public uint ProjectileId;
    public float Interval, Range;
    public uint TargetTeam;
    public float Cooldown;      // 运行时
    public EntityHandle Target; // 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Projectile : IComponent     // 48B（M5 批⓪：hitRadius/knockback + 命中记忆）
{
    public float Speed, Lifetime, Damage, Age; // age 运行时
    public byte Pierce, Homing;
    public ushort Hits;                         // 运行时
    public float HitRadius, Knockback;
    public byte HitHead;                        // 运行时
    internal byte _pad2a, _pad2b, _pad2c;       // C++ _pad2[3] 衬齐
    public uint HitMemory0, HitMemory1, HitMemory2, HitMemory3; // 运行时（C++ hitMemory[4]）
}

[StructLayout(LayoutKind.Sequential)]
public struct Spawner : IComponent        // 28B
{
    public uint PrefabId;
    public float Interval;
    public ushort Burst;
    public float Range;
    public uint MaxAlive;
    public uint SpawnTeam;
    public float Cooldown;   // 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Hazard : IComponent         // 16B（M5 批⓪ 增 Radius）
{
    public float Dps, TickInterval, TickPhase, Radius; // tickPhase 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Collectible : IComponent    // 24B（M5 批①：value/magnetSpeed/state/target）
{
    public byte Kind;          // 0 gem / 1 coin / 2 heart
    public byte State;         // 运行时：0 地面 / 1 磁吸中
    internal byte _pad0, _pad1; // C++ _pad[2] 衬齐
    public float MagnetRadius; // 磁吸触程（与收集者 Stats.pickupRadius 取大）
    public float MagnetSpeed;  // 磁吸飞行速度（px/s）
    public float Value;        // gem→XP / coin→gold / heart→治疗量
    public EntityHandle Target; // 运行时：磁吸目标（收集者 = 持 XpProgress 实体）
}

[StructLayout(LayoutKind.Sequential)]
public struct Trigger2D : IComponent      // 12B
{
    public uint TriggerId;
    public byte Once;
    public byte Inside;        // 运行时
    public byte Fired;         // 运行时
    public float Radius;
}

[StructLayout(LayoutKind.Sequential)]
public struct Knockback : IComponent      // 12B
{
    public Vec2 Impulse;
    public float Decay;
}

// ---- Gameplay（注册 id 21..26）----
[StructLayout(LayoutKind.Sequential)]
public struct Stats : IComponent          // 28B
{
    public float MoveSpeed, Attack, Defense, CritRate, CritDmg, PickupRadius, Luck;
}

/// <summary>状态效果元素（镜像 lemon::StatusInst，12B；数组段元素）。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct StatusInst     // 12B
{
    public ushort Id, Stacks;
    public float Remain;
    public uint Source;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct StatusEffects : IComponent // 52B
{
    public const int Capacity = 4;
    internal fixed byte _active[Capacity * 12]; // C++ StatusInst active[4]（48B，不透明镜像）
    public byte Count;
    internal byte _pad0, _pad1, _pad2;          // C++ _pad[3]：衬齐 sizeof=52（否则 C# 对齐 1 只到 49）

    /// <summary>按索引读写状态槽（越界由调用方约束；布局由校验兜底）。</summary>
    public unsafe ref StatusInst Slot(int i)
    {
        fixed (byte* p = _active)
            return ref ((StatusInst*)p)[i];
    }
}

/// <summary>物品元素（镜像 lemon::ItemStack，8B；数组段元素）。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct ItemStack      // 8B
{
    public uint ItemId;
    public ushort Count;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Inventory : IComponent // 136B
{
    public const int Capacity = 16;
    internal fixed byte _items[Capacity * 8]; // C++ ItemStack items[16]（128B，不透明镜像）
    public byte Count;
    public uint Gold;

    public unsafe ref ItemStack Slot(int i)
    {
        fixed (byte* p = _items)
            return ref ((ItemStack*)p)[i];
    }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Equipment : IComponent // 20B
{
    public uint WeaponId, ArmorId;
    public fixed uint RelicIds[3]; // 定长标量数组段
}

[StructLayout(LayoutKind.Sequential)]
public struct XpProgress : IComponent     // 16B
{
    public float Xp, XpToNext;
    public uint Level;
    internal uint _pad;     // C++ _pad：衬齐 sizeof=16
}

[StructLayout(LayoutKind.Sequential)]
public struct IncrementalState : IComponent // 24B（M6+ 占位）
{
    public double Rate, Multiplier, Cached;
}

// ---- M5 批②：导演波次表（BehaviorComponents.h 三件套镜像；id 27 表尾）----
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WaveEntry // 16B
{
    public uint PrefabId;
    public ushort Count;
    fixed byte _pad[2];       // C++ _pad（零化）
    public float Interval, Range;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WaveDef // 76B
{
    public float StartTime, RampMult;
    public byte EntryCount;
    fixed byte _pad[3];
    internal fixed byte _entries[4 * 16]; // C++ WaveEntry entries[4]（64B，不透明镜像）

    // ---- M6a 批② T2：波次表数据化写入口（布局冻结不动，纯加方法；拷贝语义
    // 避开 fixed 作用域——GetEntry 取副本改字段再 SetEntry 写回）----
    public WaveEntry GetEntry(int i) { fixed (byte* p = _entries) { return ((WaveEntry*)p)[i]; } }
    public void SetEntry(int i, in WaveEntry e) { fixed (byte* p = _entries) { ((WaveEntry*)p)[i] = e; } }
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WaveDirector : IComponent // 1260B
{
    public uint SpawnTeam;
    public int CapAlive;
    public byte WaveCount;
    fixed byte _pad[3];
    internal fixed byte _waves[16 * 76]; // C++ WaveDef waves[16]（1216B，不透明镜像）
    public float Time;                   // 运行时：局内时刻
    internal fixed float _cd[4];         // 运行时：waveCooldown[4]
    internal fixed ushort _spawned[4];   // 运行时：waveSpawned[4]
    public byte WaveIndex;               // 运行时：已生效波数
    fixed byte _pad2[3];

    /// <summary>M6a 批② T2：波次读写（拷贝语义；i 越界由调用方自理——引擎消费
    /// 侧按 WaveCount 钳制）。表数据化载入用法示例见 demo/svr-test（waves.tab）。</summary>
    public WaveDef GetWave(int i) { fixed (byte* p = _waves) { return ((WaveDef*)p)[i]; } }
    public void SetWave(int i, in WaveDef w) { fixed (byte* p = _waves) { ((WaveDef*)p)[i] = w; } }
}

// ---- T3d 批①/②（ADR-013）：id 28/29（登记表尾追加，与 ComponentCatalog 同步）----

[StructLayout(LayoutKind.Sequential)]
public unsafe struct AnimGraph : IComponent   // 24B
{
    public ulong ControllerGuid; // .controller 资产 GUID（0 = 无图，纯集绑定）
    public ulong SetGuid;        // .override 集资产 GUID（按名解析作用域）
    public byte Inited;          // 图初始化边沿（运行时；首 tick 播种参数/进 entry）
    internal fixed byte _pad[7];
}

[StructLayout(LayoutKind.Sequential)]
public struct AnimParams : IComponent  // 32B：参数黑板 8 槽（float/bool/trigger 统一
{                                      // f32；槽位 = 所绑 controller 参数表定序）
    public float P0, P1, P2, P3, P4, P5, P6, P7;
}

// ---- M6b 批③d 前置（id 30；登记表尾追加，与 ComponentCatalog 同步）----

[StructLayout(LayoutKind.Sequential)]
public struct UIDocument : IComponent  // 16B（u64+u8+u8+u16，自然对齐 sizeof=16——
{                                      // 2026-09-29 审核修正，原 8B 误写）
    public ulong SourceAssetGuid; // .rml 资产 GUID（0 = 未挂；guid 真源）
    public byte ShowOnStart;      // 进 Play 即显。注意：C++ 默认 1，C# default = 0——
                                  // SetComponent 整写前须显式 ShowOnStart = 1（否则
                                  // 装载但隐藏；SpriteRenderer.Flags 同款镜像默认值坑）
    public byte Modal;            // 模态标记初值（运行时 UI.Show(doc, true) 可覆写）
    internal ushort _reserved;    // C++ reserved 衬齐（尾加纪律）
}

// ---- M6c 批②（id 31；登记表尾追加，与 ComponentCatalog 同步）----

[StructLayout(LayoutKind.Sequential)]
public struct AudioSource : IComponent  // 24B（u64+f32×3+u16+u8+u8 自然对齐；字段序
{                                       // = ADR-015 M3 勘误后口径，见 AudioComponents.h）
    public ulong ClipGuid;   // 音频资产 GUID（0 = 无片静默）
    public float Volume;
    public float RefDist;    // 全增益半径（线性衰减起点）
    public float MaxDist;    // 衰减到 0 半径
    public ushort Flags;     // bit0 循环 bit1 进 Play 自动起播。注意：C++ 默认
                             // PlayOnStart(0x2)，C# default = 0——整写须显式置位
                             //（UIDocument.ShowOnStart 同款镜像默认值坑）
    public byte Group;       // 混音组 0 Bgm/1 Sfx/2 Ui
    internal byte _pad;      // C++ pad_ 衬齐（尾加纪律）
}

// ---- 事件包镜像（Events.h：48B 固定布局，桥侧 blittable）----
public enum GameEvent : ushort
{
    Spawn = 0, Hit, Death, TriggerEnter, TriggerExit, WaveStart, LevelUp, Pickup,
    TimerFire, Custom, // 用户自定义区起点（Custom + 用户资产注册 id）
    AnimFrame,    // T3d 批③：帧事件（user = 事件 id；userArg = clipId；payload[0] = 帧号）
    AnimFinished, // T3d 批③：非 loop 段播完（userArg = clipId；补 IsPlaying 判不了播完的缺口）
    TweenFinished, // A 档补间（2026-09-28）：Once 完成恰一次（src = 实体；userArg = tween 句柄）
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct EventPacket // 48B
{
    public GameEvent Type;
    public ushort User;
    public EntityHandle Src, Dst;
    public fixed float Payload[4];
    public ulong UserArg;
}
