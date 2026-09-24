// Lemon.SDK — 结构变更命令缓冲（04 §3 / M3-6）
// 纪律：脚本的结构变更（建/删实体、增删组件、挂脚本）只入命令缓冲，帧首 Essential
// 统一应用（与两阶段销毁同拍）——当帧脚本读旧结构，跨帧生效（04 §3.2 不对齐清单）。
// 占位实体：Create 返回的句柄高位置 1，仅在本帧命令流内有效（后续命令引用它会被
// 解析为真实实体）；跨帧引用请走查询（下一帧新实体对系统可见）。
using System;
using System.Collections.Generic;
using Lemon.Interop;

namespace Lemon;

public enum SceneOpType : byte
{
    Create = 0,
    Destroy = 1,
    AddComponent = 2,
    RemoveComponent = 3,
    AttachScript = 4, // compId = 脚本类型 id（Behaviours 注册序）
    DetachScript = 5, // M6a 批⓪ T3：compId = 脚本类型 id（卸单槽：OnDestroy+退订+槽移除）
}

[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct SceneOp
{
    public SceneOpType Type;
    public byte CompId;
    public ushort Reserved;
    public EntityHandle Entity; // Create 命令：占位句柄（发出方不关心回读）

    public static ulong NextPlaceholder()
    {
        unchecked { return 0x8000_0000_0000_0000ul | (ulong)++s_placeholderCounter; }
    }
    private static ulong s_placeholderCounter;
}

public static unsafe class SceneOps
{
    private static readonly List<SceneOp> s_pending = new();
    private static readonly object s_lock = new();

    /// <summary>新建实体；返回占位句柄（仅本帧命令流内引用有效）。</summary>
    public static EntityHandle Create()
    {
        var ph = new EntityHandle { Id = SceneOp.NextPlaceholder() };
        lock (s_lock) s_pending.Add(new SceneOp { Type = SceneOpType.Create, Entity = ph });
        return ph;
    }

    public static void Destroy(EntityHandle e)
    {
        lock (s_lock) s_pending.Add(new SceneOp { Type = SceneOpType.Destroy, Entity = e });
    }

    public static void AddComponent<T>(EntityHandle e) where T : unmanaged
    {
        lock (s_lock)
            s_pending.Add(new SceneOp { Type = SceneOpType.AddComponent,
                                        CompId = ComponentTable.Id<T>(), Entity = e });
    }

    public static void RemoveComponent<T>(EntityHandle e) where T : unmanaged
    {
        lock (s_lock)
            s_pending.Add(new SceneOp { Type = SceneOpType.RemoveComponent,
                                        CompId = ComponentTable.Id<T>(), Entity = e });
    }

    /// <summary>挂脚本组件（档①）；typeId = Behaviours 注册序。</summary>
    public static void AttachScript(EntityHandle e, int typeId)
    {
        lock (s_lock)
            s_pending.Add(new SceneOp { Type = SceneOpType.AttachScript,
                                        CompId = (byte)typeId, Entity = e });
    }

    /// <summary>卸单槽脚本（M6a 批⓪ T3：GameObject.RemoveComponent 脚本分路）；
    /// typeId = Behaviours 注册序。未挂 = 引擎侧幂等 no-op。</summary>
    public static void DetachScript(EntityHandle e, int typeId)
    {
        lock (s_lock)
            s_pending.Add(new SceneOp { Type = SceneOpType.DetachScript,
                                        CompId = (byte)typeId, Entity = e });
    }

    /// <summary>同帧待决查询（GameObject.AddComponent 脚本分路幂等 get-or-add：
    /// 命令帧首才应用，实例未挂但命令在队 = 已在加，勿重复入队）。</summary>
    internal static bool HasPendingAttach(EntityHandle e, int typeId)
    {
        lock (s_lock)
            for (int i = 0; i < s_pending.Count; i++)
                if (s_pending[i].Type == SceneOpType.AttachScript &&
                    s_pending[i].CompId == (byte)typeId &&
                    s_pending[i].Entity.Id == e.Id)
                    return true;
        return false;
    }

    // ---- Lemon.Entry / 宿主侧 ----
    internal static int PullPending(SceneOp* dst, int cap)
    {
        lock (s_lock) {
            int n = Math.Min(s_pending.Count, cap);
            for (int i = 0; i < n; i++) dst[i] = s_pending[i];
            if (n == s_pending.Count) s_pending.Clear();
            else s_pending.RemoveRange(0, n);
            return n;
        }
    }

    internal static void SubmitRaw(byte type, byte compId, ulong e)
    {
        lock (s_lock)
            s_pending.Add(new SceneOp { Type = (SceneOpType)type, CompId = compId,
                                        Entity = new EntityHandle { Id = e } });
    }

    internal static void Reset() { lock (s_lock) s_pending.Clear(); }
}
