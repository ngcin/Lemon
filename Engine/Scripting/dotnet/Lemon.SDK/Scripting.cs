// Lemon.SDK — 档② 批量系统 API（04 §2.2：C++ 一次遍历、按块回调；块内纯托管循环）
// 数据流（ADR-010 D1 域线程模型）：
//   C++ #14 CSharpBatchSystem（管线线程）构造块描述符 → lemon_batch_tick
//   → DomainManager 域线程按块回调 IForEachSystem.ForEach → 原地写回（零拷贝）
using System.Collections.Generic;
using Lemon.Interop;

namespace Lemon;

/// <summary>镜像 struct → 注册表组件 id（LayoutTables 顺序 = C++ 登记序，M3-1 锁定）。</summary>
public static class ComponentTable
{
    private static readonly Dictionary<System.Type, byte> s_ids = new();

    internal static void Bind<T>(byte id) where T : unmanaged => s_ids[typeof(T)] = id;

    public static byte Id<T>() where T : unmanaged => s_ids[typeof(T)];

    /// <summary>Type 键反查（M6a 批⓪ T3：GameObject 统一门面的值组件分路——
    /// 泛型约束 unmanaged 不能上提到 notnull 门面，运行时按 typeof(T) 取 id）。</summary>
    public static bool TryId(System.Type t, out byte id) => s_ids.TryGetValue(t, out id);
}

/// <summary>查询声明（With = 参与遍历的组件集；M3 全部视为可读写）。</summary>
public readonly struct Query
{
    internal readonly byte[] CompIds;
    private Query(byte[] ids) { CompIds = ids; }

    public static Query With<T1>() where T1 : unmanaged => new(new byte[] { ComponentTable.Id<T1>() });
    public static Query With<T1, T2>() where T1 : unmanaged where T2 : unmanaged
        => new(new byte[] { ComponentTable.Id<T1>(), ComponentTable.Id<T2>() });
    public static Query With<T1, T2, T3>() where T1 : unmanaged where T2 : unmanaged where T3 : unmanaged
        => new(new byte[] { ComponentTable.Id<T1>(), ComponentTable.Id<T2>(), ComponentTable.Id<T3>() });
}

/// <summary>槽位视图：this[i] 经指针数组原地读写（零拷贝）。</summary>
public unsafe ref struct CompSpan<T> where T : unmanaged
{
    private readonly void** _ptrs;
    internal CompSpan(void** ptrs) { _ptrs = ptrs; }

    public ref T this[int i] => ref *(T*)_ptrs[i];
}

/// <summary>块：一批实体（≤64）+ 各查询组件的实例指针数组。</summary>
public readonly unsafe ref struct Chunk
{
    public readonly int Length;
    public readonly EntityHandle* Entities;
    public readonly void** Comps;     // comps[slot * Stride + i] = 第 slot 个组件第 i 个实例
    public readonly byte* CompIds;    // 查询组件 id（与注册序一致）
    public readonly int CompCount;
    public readonly int Stride;

    internal Chunk(int len, EntityHandle* ents, void** comps, byte* compIds, int compCount, int stride)
    { Length = len; Entities = ents; Comps = comps; CompIds = compIds; CompCount = compCount; Stride = stride; }

    public CompSpan<T> Span<T>() where T : unmanaged
    {
        byte id = ComponentTable.Id<T>();
        for (int s = 0; s < CompCount; s++)
            if (CompIds[s] == id) return new CompSpan<T>(Comps + s * Stride);
        throw new System.InvalidOperationException($"component {typeof(T).Name} not in query");
    }
}

/// <summary>档② 批量系统（04 §2.2）。执行点 = 管线 #14；异常隔离：连续 60 帧异常自动禁用。</summary>
public interface IForEachSystem
{
    string Name { get; }
    Query Query { get; }
    void ForEach(ref readonly Chunk chunk);
}

/// <summary>脚本注册入口（用户程序集 GameMain.Configure 内调用）。</summary>
public static class Scripting
{
    static Scripting()
    {
        // 强制先跑布局表静态构造（ComponentTable.Bind）——Query.With<T> 依赖绑定完成；
        // 进程未调用 lemon_sdk_layout 时（如 bench）由此路径兜底（M3-7 实测坑）
        _ = Interop.LayoutTables.Comps.Length;
    }

    private static readonly List<IForEachSystem> s_systems = new();
    private static readonly List<byte[]> s_queries = new();

    public static void Register(IForEachSystem sys)
    {
        s_systems.Add(sys);
        s_queries.Add(sys.Query.CompIds);
    }

    /// <summary>系统随机源（ADR-010 D3：引擎子流；脚本系统占 200..255 槽位，与 C++ 系统 0..15 隔离）。</summary>
    public static Pcg32 Rng(int scriptSystemIndex, ulong worldSeed)
        => new(worldSeed, 0x4C320000ul + 200u + (ulong)scriptSystemIndex);

    // ---- Lemon.Entry 域线程侧使用 ----
    internal static int SystemCount => s_systems.Count;
    internal static IForEachSystem Get(int i) => s_systems[i];
    internal static byte[] GetQuery(int i) => s_queries[i];

    /// <summary>换域时清空（DomainManager.LoadScript 在 Configure 前调用）。</summary>
    internal static void Reset()
    {
        s_systems.Clear();
        s_queries.Clear();
    }
}
