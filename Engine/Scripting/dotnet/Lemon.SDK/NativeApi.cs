// Lemon.SDK — native 函数表（04 §1：Bootstrap 期 C++ 注册给 C# 的低频语法糖通道）
// 高频数据访问走 Chunk（零跨界）；本表只服务档① 的低频组件读写。
// 线程约定：仅在域线程 tick 期间调用（C++ 侧活动 World/Scene 上下文仅此时有效）。
using System.Runtime.InteropServices;

namespace Lemon;

/// <summary>与 C++ lemon::scripting::NativeApiVtable 逐字节一致（两侧同步改；
/// M4.4 表尾追加 4 项、M5 批① 追加 2 项——旧宿主（未注册新项）时为 null，SDK 侧判空调用）。</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApi
{
    public delegate* unmanaged<ulong, int> IsAlive;
    public delegate* unmanaged<ulong, byte, int> HasComponent;
    public delegate* unmanaged<ulong, byte, void*, uint, int> ReadComponent;   // 拷贝，返回字节数
    public delegate* unmanaged<ulong, byte, void*, uint, int> WriteComponent;  // 写回，返回字节数
    public delegate* unmanaged<ulong*, float*, float*, void> GetInput;         // M4.4：InputState 快照
    public delegate* unmanaged<byte*, uint> SpriteOfGuid;                      // M4.4：资产 GUID → spriteId
    public delegate* unmanaged<uint, float, float, ulong> SpawnSprite;         // M4.4：Instantiate.Spawn
    public delegate* unmanaged<byte*, float, float, ulong> InstantiatePrefab;  // M4.4：Prefab 实例化
    public delegate* unmanaged<float> GetTimescale;                            // M5 批①：World.TimeScale
    public delegate* unmanaged<float, void> SetTimescale;                      // M5 批①：World.SetTimeScale
    public delegate* unmanaged<byte*, byte*, float, void> RtUiSet;             // M5 批①：Lemon.Ui.Set → World.RtUi
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

    // ---- M4.4 SDK 增量（宿主未注册新表项时安全降级：零输入/0/句柄 0）----

    internal static (ulong buttons, float ax, float ay) Input()
    {
        if (Api.GetInput == null) return (0, 0, 0);
        ulong b; float ax = 0, ay = 0;
        Api.GetInput(&b, &ax, &ay);
        return (b, ax, ay);
    }

    internal static uint SpriteOfGuid(string guidHex)
    {
        if (Api.SpriteOfGuid == null || guidHex == null) return 0;
        byte* p = stackalloc byte[64];
        int n = System.Math.Min(guidHex.Length, 63);
        for (int i = 0; i < n; i++) p[i] = (byte)guidHex[i];
        p[n] = 0;
        return Api.SpriteOfGuid(p);
    }

    internal static ulong Spawn(uint spriteId, float x, float y)
        => Api.SpawnSprite != null ? Api.SpawnSprite(spriteId, x, y) : 0;

    internal static ulong InstantiatePrefabGuid(string guidHex, float x, float y)
    {
        if (Api.InstantiatePrefab == null || guidHex == null) return 0;
        byte* p = stackalloc byte[64];
        int n = System.Math.Min(guidHex.Length, 63);
        for (int i = 0; i < n; i++) p[i] = (byte)guidHex[i];
        p[n] = 0;
        return Api.InstantiatePrefab(p, x, y);
    }

    // ---- M5 批①（timeScale ↔ Time.Scale；旧宿主未注册时：读 1 / 写丢弃）----

    internal static float TimeScale() => Api.GetTimescale != null ? Api.GetTimescale() : 1f;

    internal static void SetTimeScale(float s)
    {
        if (Api.SetTimescale != null) Api.SetTimescale(s);
    }

    // ---- M5 批①（RT UI ↔ Lemon.Ui.Set；旧宿主未注册时丢弃）----

    internal static unsafe void UiSet(string key, string text, float frac)
    {
        if (Api.RtUiSet == null || key == null || text == null) return;
        byte* k = stackalloc byte[16];
        byte* t = stackalloc byte[48];
        int kn = System.Math.Min(key.Length, 15);
        for (int i = 0; i < kn; i++) k[i] = (byte)key[i];
        k[kn] = 0;
        int tn = System.Math.Min(text.Length, 47);
        for (int i = 0; i < tn; i++) t[i] = (byte)text[i];
        t[tn] = 0;
        Api.RtUiSet(k, t, frac);
    }
}
