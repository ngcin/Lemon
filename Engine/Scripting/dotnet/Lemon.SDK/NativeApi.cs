// Lemon.SDK — native 函数表（04 §1：Bootstrap 期 C++ 注册给 C# 的低频语法糖通道）
// 高频数据访问走 Chunk（零跨界）；本表只服务档① 的低频组件读写。
// 线程约定：仅在域线程 tick 期间调用（C++ 侧活动 World/Scene 上下文仅此时有效）。
using System.Runtime.InteropServices;

namespace Lemon;

/// <summary>与 C++ lemon::scripting::NativeApi 逐字节一致（两侧同步改）。</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApi
{
    public delegate* unmanaged<ulong, int> IsAlive;
    public delegate* unmanaged<ulong, byte, int> HasComponent;
    public delegate* unmanaged<ulong, byte, void*, uint, int> ReadComponent;   // 拷贝，返回字节数
    public delegate* unmanaged<ulong, byte, void*, uint, int> WriteComponent;  // 写回，返回字节数
}

internal static unsafe class Native
{
    internal static NativeApi Api;

    internal static void Register(NativeApi* api) => Api = *api;

    internal static int IsAlive(ulong e) => Api.IsAlive != null ? Api.IsAlive(e) : 0;
    internal static int Has(ulong e, byte compId) => Api.HasComponent != null ? Api.HasComponent(e, compId) : 0;

    internal static bool Read<T>(ulong e, out T comp) where T : unmanaged
    {
        comp = default;
        if (Api.ReadComponent == null) return false;
        T* buf = stackalloc T[1];
        if (Api.ReadComponent(e, ComponentTable.Id<T>(), buf, (uint)sizeof(T)) != sizeof(T))
            return false;
        comp = buf[0];
        return true;
    }

    internal static bool Write<T>(ulong e, in T comp) where T : unmanaged
    {
        if (Api.WriteComponent == null) return false;
        T* buf = stackalloc T[1];
        buf[0] = comp;
        return Api.WriteComponent(e, ComponentTable.Id<T>(), buf, (uint)sizeof(T)) == sizeof(T);
    }
}
