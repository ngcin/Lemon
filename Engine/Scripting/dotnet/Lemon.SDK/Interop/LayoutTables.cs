// Lemon.SDK — 布局探针表（M3-1 布局一致性护栏）
// 自报家门：C# 侧把每个镜像 struct 的 sizeof + 逐字段 offset/type/flags 用 unsafe 指针
// 实测出来（不是手写常数），C++ 测试逐项对照 ComponentRegistry——表会撒谎的前提不存在。
// 探针声明与 ComponentCatalog.cpp 登记表 1:1（顺序=注册顺序）。
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Lemon.Interop;

/// <summary>字段类型代码 = C++ FieldType 枚举序（ComponentRegistry.h；两侧同步改）。</summary>
public static class FieldTypeCode
{
    public const byte Float = 0, Double = 1, Int32 = 2, UInt32 = 3, UInt64 = 4,
        Int16 = 5, UInt16 = 6, Int8 = 7, UInt8 = 8, Bool = 9, Vec2 = 10,
        EntityRef = 11, TeamRef = 12, Blob24 = 13;

    public const byte RuntimeFlag = 1; // kFieldRuntime（序列化跳过位）
}

[StructLayout(LayoutKind.Sequential)]
public struct CompLayoutRow   // 16B（C++ 测试侧镜像同布局）
{
    public ulong NameHash;
    public uint SizeOf;
    public ushort FieldCount;
    public ushort FirstField;
}

[StructLayout(LayoutKind.Sequential)]
public struct FieldLayoutRow  // 16B（ulong 对齐 8，尾垫 4——两侧自然对齐同规则）
{
    public ulong NameHash;
    public ushort Offset;
    public byte Type;
    public byte Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct SegLayoutRow    // 24B：定长数组段（元素字段行挂在 Fields 尾部）
{
    public ulong NameHash;
    public ushort SegOffset, ElemSize, CountOffset, MaxCount, ElemFieldFirst, ElemFieldCount;
}

public static unsafe class LayoutTables
{
    public static readonly CompLayoutRow[] Comps;
    public static readonly FieldLayoutRow[] Fields;
    public static readonly SegLayoutRow[] Segs;

    /// <summary>FNV-1a 64（与引擎 StateHash 同源参数：basis/prime 一致）。</summary>
    public static ulong Fnv64(string s)
    {
        ulong h = 1469598103934665603ul;
        foreach (char ch in s) { h ^= (byte)ch; h *= 1099511628211ul; }
        return h;
    }

    private static FieldLayoutRow F(string name, byte type, byte flags, ushort offset)
        => new() { NameHash = Fnv64(name), Offset = offset, Type = type, Flags = flags };

    static LayoutTables()
    {
        var c = new List<CompLayoutRow>();
        var f = new List<FieldLayoutRow>();
        ushort First() => (ushort)f.Count;
        void Add<T>(string name, uint size, FieldLayoutRow[] rows) where T : unmanaged
        {
            ComponentTable.Bind<T>((byte)c.Count); // 镜像类型 → 组件 id（Query.With<T> 用）
            ushort first = First();
            f.AddRange(rows);
            c.Add(new CompLayoutRow { NameHash = Fnv64(name), SizeOf = size,
                                      FieldCount = (ushort)rows.Length, FirstField = first });
        }
        ushort O(void* b, void* field) => checked((ushort)((byte*)field - (byte*)b));

        // ---- Core（id 0..4；字段顺序=ComponentCatalog.cpp）----
        { Transform2D t = default; Transform2D* p = &t;
          Add<Transform2D>("Transform2D", (uint)sizeof(Transform2D), new[] {
            F("pos", FieldTypeCode.Vec2, 0, O(p, &p->Pos)),
            F("rot", FieldTypeCode.Float, 0, O(p, &p->Rot)),
            F("scale", FieldTypeCode.Vec2, 0, O(p, &p->Scale)) }); }
        { Velocity t = default; Velocity* p = &t;
          Add<Velocity>("Velocity", (uint)sizeof(Velocity), new[] {
            F("v", FieldTypeCode.Vec2, 0, O(p, &p->V)) }); }
        { Hierarchy t = default; Hierarchy* p = &t;
          Add<Hierarchy>("Hierarchy", (uint)sizeof(Hierarchy), new[] {
            F("parent", FieldTypeCode.EntityRef, 0, O(p, &p->Parent)),
            F("firstChild", FieldTypeCode.EntityRef, 0, O(p, &p->FirstChild)),
            F("next", FieldTypeCode.EntityRef, 0, O(p, &p->Next)),
            F("prev", FieldTypeCode.EntityRef, 0, O(p, &p->Prev)) }); }
        { Meta t = default; Meta* p = &t;
          Add<Meta>("Meta", (uint)sizeof(Meta), new[] {
            F("prefabId", FieldTypeCode.UInt64, 0, O(p, &p->PrefabId)),
            F("team", FieldTypeCode.TeamRef, 0, O(p, &p->Team)),
            F("layer", FieldTypeCode.UInt16, 0, O(p, &p->Layer)),
            F("tag", FieldTypeCode.Blob24, 0, O(p, p->Tag)),
            F("guid", FieldTypeCode.UInt64, 0, O(p, &p->Guid)) }); }
        Add<DestroyQueueTag>("DestroyQueueTag", (uint)sizeof(DestroyQueueTag), Array.Empty<FieldLayoutRow>());

        // ---- Render（id 5..8）----
        { SpriteRenderer t = default; SpriteRenderer* p = &t;
          Add<SpriteRenderer>("SpriteRenderer", (uint)sizeof(SpriteRenderer), new[] {
            F("spriteId", FieldTypeCode.UInt32, 0, O(p, &p->SpriteId)),
            F("colorRGBA", FieldTypeCode.UInt32, 0, O(p, &p->ColorRGBA)),
            F("sortOrder", FieldTypeCode.Int16, 0, O(p, &p->SortOrder)),
            F("sortingLayer", FieldTypeCode.UInt8, 0, O(p, &p->SortingLayer)),
            F("flags", FieldTypeCode.UInt8, 0, O(p, &p->Flags)) }); }
        { Animator2D t = default; Animator2D* p = &t;
          Add<Animator2D>("Animator2D", (uint)sizeof(Animator2D), new[] {
            F("clipId", FieldTypeCode.UInt32, 0, O(p, &p->ClipId)),
            F("time", FieldTypeCode.Float, 0, O(p, &p->Time)),
            F("speed", FieldTypeCode.Float, 0, O(p, &p->Speed)),
            F("loop", FieldTypeCode.UInt8, 0, O(p, &p->Loop)),
            F("playOnStart", FieldTypeCode.UInt8, 0, O(p, &p->PlayOnStart)),
            F("curFrame", FieldTypeCode.UInt16, 0, O(p, &p->CurFrame)) }); }
        { ParticleEmitterRef t = default; ParticleEmitterRef* p = &t;
          Add<ParticleEmitterRef>("ParticleEmitterRef", (uint)sizeof(ParticleEmitterRef), new[] {
            F("emitterId", FieldTypeCode.UInt32, 0, O(p, &p->EmitterId)),
            F("playing", FieldTypeCode.UInt8, 0, O(p, &p->Playing)) }); }
        { SortingOverride t = default; SortingOverride* p = &t;
          Add<SortingOverride>("SortingOverride", (uint)sizeof(SortingOverride), new[] {
            F("order", FieldTypeCode.Int16, 0, O(p, &p->Order)) }); }

        // ---- Behavior（id 9..20）----
        { Health t = default; Health* p = &t;
          Add<Health>("Health", (uint)sizeof(Health), new[] {
            F("max", FieldTypeCode.Float, 0, O(p, &p->Max)),
            F("cur", FieldTypeCode.Float, 0, O(p, &p->Cur)),
            F("iFrames", FieldTypeCode.Float, FieldTypeCode.RuntimeFlag, O(p, &p->IFrames)),
            F("iframeWindow", FieldTypeCode.Float, 0, O(p, &p->IFrameWindow)) }); }
        { Mover t = default; Mover* p = &t;
          Add<Mover>("Mover", (uint)sizeof(Mover), new[] {
            F("speed", FieldTypeCode.Float, 0, O(p, &p->Speed)) }); }
        { Patrol t = default; Patrol* p = &t;
          Add<Patrol>("Patrol", (uint)sizeof(Patrol), new[] {
            F("a", FieldTypeCode.Vec2, 0, O(p, &p->A)),
            F("b", FieldTypeCode.Vec2, 0, O(p, &p->B)),
            F("pauseTime", FieldTypeCode.Float, 0, O(p, &p->PauseTime)),
            F("headingToB", FieldTypeCode.UInt8, 0, O(p, &p->HeadingToB)) }); }
        { Chase t = default; Chase* p = &t;
          Add<Chase>("Chase", (uint)sizeof(Chase), new[] {
            F("speed", FieldTypeCode.Float, 0, O(p, &p->Speed)),
            F("aggroRange", FieldTypeCode.Float, 0, O(p, &p->AggroRange)),
            F("keepRange", FieldTypeCode.Float, 0, O(p, &p->KeepRange)),
            F("targetTeam", FieldTypeCode.TeamRef, 0, O(p, &p->TargetTeam)),
            F("target", FieldTypeCode.EntityRef, FieldTypeCode.RuntimeFlag, O(p, &p->Target)) }); }
        { Flee t = default; Flee* p = &t;
          Add<Flee>("Flee", (uint)sizeof(Flee), new[] {
            F("speed", FieldTypeCode.Float, 0, O(p, &p->Speed)),
            F("range", FieldTypeCode.Float, 0, O(p, &p->Range)) }); }
        { Shooter t = default; Shooter* p = &t;
          Add<Shooter>("Shooter", (uint)sizeof(Shooter), new[] {
            F("projectileId", FieldTypeCode.UInt32, 0, O(p, &p->ProjectileId)),
            F("interval", FieldTypeCode.Float, 0, O(p, &p->Interval)),
            F("range", FieldTypeCode.Float, 0, O(p, &p->Range)),
            F("targetTeam", FieldTypeCode.TeamRef, 0, O(p, &p->TargetTeam)),
            F("cooldown", FieldTypeCode.Float, FieldTypeCode.RuntimeFlag, O(p, &p->Cooldown)),
            F("target", FieldTypeCode.EntityRef, FieldTypeCode.RuntimeFlag, O(p, &p->Target)) }); }
        { Projectile t = default; Projectile* p = &t;
          Add<Projectile>("Projectile", (uint)sizeof(Projectile), new[] {
            F("speed", FieldTypeCode.Float, 0, O(p, &p->Speed)),
            F("lifetime", FieldTypeCode.Float, 0, O(p, &p->Lifetime)),
            F("damage", FieldTypeCode.Float, 0, O(p, &p->Damage)),
            F("age", FieldTypeCode.Float, FieldTypeCode.RuntimeFlag, O(p, &p->Age)),
            F("pierce", FieldTypeCode.UInt8, 0, O(p, &p->Pierce)),
            F("homing", FieldTypeCode.UInt8, 0, O(p, &p->Homing)),
            F("hits", FieldTypeCode.UInt16, FieldTypeCode.RuntimeFlag, O(p, &p->Hits)),
            F("hitRadius", FieldTypeCode.Float, 0, O(p, &p->HitRadius)),
            F("knockback", FieldTypeCode.Float, 0, O(p, &p->Knockback)),
            F("hitHead", FieldTypeCode.UInt8, FieldTypeCode.RuntimeFlag, O(p, &p->HitHead)),
            F("hitMemory0", FieldTypeCode.UInt32, FieldTypeCode.RuntimeFlag, O(p, &p->HitMemory0)),
            F("hitMemory1", FieldTypeCode.UInt32, FieldTypeCode.RuntimeFlag, O(p, &p->HitMemory1)),
            F("hitMemory2", FieldTypeCode.UInt32, FieldTypeCode.RuntimeFlag, O(p, &p->HitMemory2)),
            F("hitMemory3", FieldTypeCode.UInt32, FieldTypeCode.RuntimeFlag, O(p, &p->HitMemory3)) }); }
        { Spawner t = default; Spawner* p = &t;
          Add<Spawner>("Spawner", (uint)sizeof(Spawner), new[] {
            F("prefabId", FieldTypeCode.UInt32, 0, O(p, &p->PrefabId)),
            F("interval", FieldTypeCode.Float, 0, O(p, &p->Interval)),
            F("burst", FieldTypeCode.UInt16, 0, O(p, &p->Burst)),
            F("range", FieldTypeCode.Float, 0, O(p, &p->Range)),
            F("maxAlive", FieldTypeCode.UInt32, 0, O(p, &p->MaxAlive)),
            F("spawnTeam", FieldTypeCode.TeamRef, 0, O(p, &p->SpawnTeam)),
            F("cooldown", FieldTypeCode.Float, FieldTypeCode.RuntimeFlag, O(p, &p->Cooldown)) }); }
        { Hazard t = default; Hazard* p = &t;
          Add<Hazard>("Hazard", (uint)sizeof(Hazard), new[] {
            F("dps", FieldTypeCode.Float, 0, O(p, &p->Dps)),
            F("tickInterval", FieldTypeCode.Float, 0, O(p, &p->TickInterval)),
            F("tickPhase", FieldTypeCode.Float, FieldTypeCode.RuntimeFlag, O(p, &p->TickPhase)),
            F("radius", FieldTypeCode.Float, 0, O(p, &p->Radius)) }); }
        { Collectible t = default; Collectible* p = &t;
          Add<Collectible>("Collectible", (uint)sizeof(Collectible), new[] {
            F("kind", FieldTypeCode.UInt8, 0, O(p, &p->Kind)),
            F("state", FieldTypeCode.UInt8, FieldTypeCode.RuntimeFlag, O(p, &p->State)),
            F("magnetRadius", FieldTypeCode.Float, 0, O(p, &p->MagnetRadius)),
            F("magnetSpeed", FieldTypeCode.Float, 0, O(p, &p->MagnetSpeed)),
            F("value", FieldTypeCode.Float, 0, O(p, &p->Value)),
            F("target", FieldTypeCode.EntityRef, FieldTypeCode.RuntimeFlag, O(p, &p->Target)) }); }
        { Trigger2D t = default; Trigger2D* p = &t;
          Add<Trigger2D>("Trigger2D", (uint)sizeof(Trigger2D), new[] {
            F("triggerId", FieldTypeCode.UInt32, 0, O(p, &p->TriggerId)),
            F("once", FieldTypeCode.UInt8, 0, O(p, &p->Once)),
            F("inside", FieldTypeCode.UInt8, FieldTypeCode.RuntimeFlag, O(p, &p->Inside)),
            F("fired", FieldTypeCode.UInt8, FieldTypeCode.RuntimeFlag, O(p, &p->Fired)),
            F("radius", FieldTypeCode.Float, 0, O(p, &p->Radius)) }); }
        { Knockback t = default; Knockback* p = &t;
          Add<Knockback>("Knockback", (uint)sizeof(Knockback), new[] {
            F("impulse", FieldTypeCode.Vec2, 0, O(p, &p->Impulse)),
            F("decay", FieldTypeCode.Float, 0, O(p, &p->Decay)) }); }

        // ---- Gameplay（id 21..26）----
        { Stats t = default; Stats* p = &t;
          Add<Stats>("Stats", (uint)sizeof(Stats), new[] {
            F("moveSpeed", FieldTypeCode.Float, 0, O(p, &p->MoveSpeed)),
            F("attack", FieldTypeCode.Float, 0, O(p, &p->Attack)),
            F("defense", FieldTypeCode.Float, 0, O(p, &p->Defense)),
            F("critRate", FieldTypeCode.Float, 0, O(p, &p->CritRate)),
            F("critDmg", FieldTypeCode.Float, 0, O(p, &p->CritDmg)),
            F("pickupRadius", FieldTypeCode.Float, 0, O(p, &p->PickupRadius)),
            F("luck", FieldTypeCode.Float, 0, O(p, &p->Luck)) }); }
        { StatusEffects t = default; StatusEffects* p = &t;
          Add<StatusEffects>("StatusEffects", (uint)sizeof(StatusEffects), new[] {
            F("count", FieldTypeCode.UInt8, 0, O(p, &p->Count)) }); }
        { Inventory t = default; Inventory* p = &t;
          Add<Inventory>("Inventory", (uint)sizeof(Inventory), new[] {
            F("count", FieldTypeCode.UInt8, 0, O(p, &p->Count)),
            F("gold", FieldTypeCode.UInt32, 0, O(p, &p->Gold)) }); }
        { Equipment t = default; Equipment* p = &t;
          Add<Equipment>("Equipment", (uint)sizeof(Equipment), new[] {
            F("weaponId", FieldTypeCode.UInt32, 0, O(p, &p->WeaponId)),
            F("armorId", FieldTypeCode.UInt32, 0, O(p, &p->ArmorId)) }); }
        { XpProgress t = default; XpProgress* p = &t;
          Add<XpProgress>("XpProgress", (uint)sizeof(XpProgress), new[] {
            F("xp", FieldTypeCode.Float, 0, O(p, &p->Xp)),
            F("xpToNext", FieldTypeCode.Float, 0, O(p, &p->XpToNext)),
            F("level", FieldTypeCode.UInt32, 0, O(p, &p->Level)) }); }
        { IncrementalState t = default; IncrementalState* p = &t;
          Add<IncrementalState>("IncrementalState", (uint)sizeof(IncrementalState), new[] {
            F("rate", FieldTypeCode.Double, 0, O(p, &p->Rate)),
            F("multiplier", FieldTypeCode.Double, 0, O(p, &p->Multiplier)),
            F("cached", FieldTypeCode.Double, 0, O(p, &p->Cached)) }); }

        // ---- 数组段（元素字段行挂 Fields 尾部；对照 ArraySegMeta）----
        var segs = new List<SegLayoutRow>();
        { StatusEffects t = default; StatusEffects* p = &t;
          ushort elemFirst = (ushort)f.Count;
          { StatusInst e = default; StatusInst* q = &e;
            f.Add(F("id", FieldTypeCode.UInt16, 0, O(q, &q->Id)));
            f.Add(F("stacks", FieldTypeCode.UInt16, 0, O(q, &q->Stacks)));
            f.Add(F("remain", FieldTypeCode.Float, 0, O(q, &q->Remain)));
            f.Add(F("source", FieldTypeCode.UInt32, 0, O(q, &q->Source))); }
          segs.Add(new SegLayoutRow { NameHash = Fnv64("StatusEffects"),
              SegOffset = O(p, p->_active), ElemSize = (ushort)sizeof(StatusInst),
              CountOffset = O(p, &p->Count), MaxCount = StatusEffects.Capacity,
              ElemFieldFirst = elemFirst, ElemFieldCount = 4 }); }
        { Inventory t = default; Inventory* p = &t;
          ushort elemFirst = (ushort)f.Count;
          { ItemStack e = default; ItemStack* q = &e;
            f.Add(F("itemId", FieldTypeCode.UInt32, 0, O(q, &q->ItemId)));
            f.Add(F("count", FieldTypeCode.UInt16, 0, O(q, &q->Count))); }
          segs.Add(new SegLayoutRow { NameHash = Fnv64("Inventory"),
              SegOffset = O(p, p->_items), ElemSize = (ushort)sizeof(ItemStack),
              CountOffset = O(p, &p->Count), MaxCount = Inventory.Capacity,
              ElemFieldFirst = elemFirst, ElemFieldCount = 2 }); }
        { Equipment t = default; Equipment* p = &t;
          segs.Add(new SegLayoutRow { NameHash = Fnv64("Equipment"),
              SegOffset = O(p, p->RelicIds), ElemSize = (ushort)sizeof(uint),
              CountOffset = 0xFFFF, MaxCount = 3, ElemFieldFirst = 0, ElemFieldCount = 0 }); }

        Comps = c.ToArray();
        Fields = f.ToArray();
        Segs = segs.ToArray();
    }
}
