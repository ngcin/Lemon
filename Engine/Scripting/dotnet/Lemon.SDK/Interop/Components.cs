// Lemon.SDK — 27 组件镜像 struct（与 lemon::ecs 组件头逐字节对齐；M3-1 布局护栏锁定）。
// 纪律：
//   * 字段顺序/类型与 ComponentCatalog.cpp 登记表 1:1（改动 = 破回放，须两侧同步）；
//   * 布局一致性由 lemon-script-tests 双向校验（C# 报告表 vs C++ ComponentMeta），
//     手写镜像 + 自动校验；代码生成（Source Generator）按 04 §3 留 M4；
//   * C# fixed 缓冲只支持基元类型 → StatusEffects/Inventory 的结构数组用不透明字节块
//     镜像（总尺寸由布局校验兜底；typed 访问随 M3-3 Chunk API 提供手动偏移版本）。
using System.Runtime.InteropServices;

namespace Lemon.Interop;

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
public struct Transform2D   // 20B
{
    public Vec2 Pos;
    public float Rot;       // 弧度
    public Vec2 Scale;
}

[StructLayout(LayoutKind.Sequential)]
public struct Velocity      // 8B
{
    public Vec2 V;
}

[StructLayout(LayoutKind.Sequential)]
public struct Hierarchy     // 32B
{
    public EntityHandle Parent, FirstChild, Next, Prev;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct Meta   // 48B（M4.1 增 Guid，C++ CoreComponents.h 同步）
{
    public ulong PrefabId;
    public uint Team;
    public ushort Layer;
    public fixed byte Tag[24]; // Blob24：短标签（NUL 结尾约定同 C++）
    public ulong Guid;         // 持久实体 GUID（0 = 运行时生成实体）
}

[StructLayout(LayoutKind.Sequential)]
public struct DestroyQueueTag // 1B（空 tag 组件）
{
}

// ---- Render（注册 id 5..8）----
[StructLayout(LayoutKind.Sequential)]
public struct SpriteRenderer // 12B
{
    public uint SpriteId;
    public uint ColorRGBA;
    public short SortOrder;
    public byte SortingLayer;
    public byte Flags;        // bit0 flipX, bit1 flipY, bit2 enabled。
                              // 注意：引擎侧新增组件默认启用（RenderComponents.h），
                              // 但 C# default(SpriteRenderer) 是零值 = 禁用——
                              // SetComponent 整写前须置 Flags = 0x4（否则不渲染）。
}

[StructLayout(LayoutKind.Sequential)]
public struct Animator2D     // 16B
{
    public uint ClipId;
    public float Time;
    public float Speed;
    public byte Loop;
    public byte PlayOnStart;
    public ushort CurFrame;
}

[StructLayout(LayoutKind.Sequential)]
public struct ParticleEmitterRef // 8B
{
    public uint EmitterId;
    public byte Playing;
}

[StructLayout(LayoutKind.Sequential)]
public struct SortingOverride // 2B
{
    public short Order;
}

// ---- Behavior（注册 id 9..20）----
[StructLayout(LayoutKind.Sequential)]
public struct Health         // 16B（M5 批⓪ 增 IFrameWindow）
{
    public float Max, Cur, IFrames, IFrameWindow; // iFrames 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Mover          // 4B
{
    public float Speed;
}

[StructLayout(LayoutKind.Sequential)]
public struct Patrol         // 24B
{
    public Vec2 A, B;
    public float PauseTime;
    public byte HeadingToB;
}

[StructLayout(LayoutKind.Sequential)]
public struct Chase          // 24B
{
    public float Speed, AggroRange, KeepRange;
    public uint TargetTeam;
    public EntityHandle Target; // 运行时缓存（kFieldRuntime）
}

[StructLayout(LayoutKind.Sequential)]
public struct Flee           // 8B
{
    public float Speed, Range;
}

[StructLayout(LayoutKind.Sequential)]
public struct Shooter        // 32B
{
    public uint ProjectileId;
    public float Interval, Range;
    public uint TargetTeam;
    public float Cooldown;      // 运行时
    public EntityHandle Target; // 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Projectile     // 48B（M5 批⓪：hitRadius/knockback + 命中记忆）
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
public struct Spawner        // 28B
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
public struct Hazard         // 16B（M5 批⓪ 增 Radius）
{
    public float Dps, TickInterval, TickPhase, Radius; // tickPhase 运行时
}

[StructLayout(LayoutKind.Sequential)]
public struct Collectible    // 24B（M5 批①：value/magnetSpeed/state/target）
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
public struct Trigger2D      // 12B
{
    public uint TriggerId;
    public byte Once;
    public byte Inside;        // 运行时
    public byte Fired;         // 运行时
    public float Radius;
}

[StructLayout(LayoutKind.Sequential)]
public struct Knockback      // 12B
{
    public Vec2 Impulse;
    public float Decay;
}

// ---- Gameplay（注册 id 21..26）----
[StructLayout(LayoutKind.Sequential)]
public struct Stats          // 28B
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
public unsafe struct StatusEffects // 52B
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
public unsafe struct Inventory // 136B
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
public unsafe struct Equipment // 20B
{
    public uint WeaponId, ArmorId;
    public fixed uint RelicIds[3]; // 定长标量数组段
}

[StructLayout(LayoutKind.Sequential)]
public struct XpProgress     // 16B
{
    public float Xp, XpToNext;
    public uint Level;
    internal uint _pad;     // C++ _pad：衬齐 sizeof=16
}

[StructLayout(LayoutKind.Sequential)]
public struct IncrementalState // 24B（M6+ 占位）
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
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WaveDirector // 1260B
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
}

// ---- 事件包镜像（Events.h：48B 固定布局，桥侧 blittable）----
public enum GameEvent : ushort
{
    Spawn = 0, Hit, Death, TriggerEnter, TriggerExit, WaveStart, LevelUp, Pickup,
    TimerFire, Custom, // 用户自定义区起点（Custom + 用户资产注册 id）
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
