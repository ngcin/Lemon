// Lemon.SDK — native 函数表（04 §1：Bootstrap 期 C++ 注册给 C# 的低频语法糖通道）
// 高频数据访问走 Chunk（零跨界）；本表只服务档① 的低频组件读写。
// 线程约定：仅在域线程 tick 期间调用（C++ 侧活动 World/Scene 上下文仅此时有效）。
using System;
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
    public delegate* unmanaged<byte*, void*, uint, int> SaveSet;               // M5 批④：Lemon.Save.Set → World.Saves
    public delegate* unmanaged<byte*, int> SaveGetLen;                         // M5 批④：-1 = 无此键
    public delegate* unmanaged<byte*, void*, uint, int> SaveGet;               // M5 批④：返回拷贝数（-2 = cap 不足）
    public delegate* unmanaged<void> SaveFlush;                                // M5 批④：ScriptIoHooks 落盘
    public delegate* unmanaged<byte*, void> RtUiClear;                         // M5 批④：Lemon.Ui.Clear → RtUi 删单行
    public delegate* unmanaged<byte*, byte*, float, uint, void> RtUiSetEx;     // M5 批④：Ui.Set 着色版（0 = 默认）
    public delegate* unmanaged<int, byte*, byte*, byte*, byte*, void> UiCards; // M5 批④：三选一卡片显隐/内容
    public delegate* unmanaged<int> UiCardPick;                                // M5 批④：消费式：返回后置 -1
    public delegate* unmanaged<byte*, float, float, uint, void> FxPopup;       // M6a 批①：飘字 → World.Fx
    public delegate* unmanaged<ulong, float, uint, float, void> FxBar;         // M6a 批①：世界血条 → World.Fx
    public delegate* unmanaged<float, float, int, int, void> SetSeparation;    // 调参下放批：Lemon.Physics.Separation（<0 = 保持）
    public delegate* unmanaged<byte*, int> TableRows;                          // M6a 批②：行数（含列头行）；-1 = 无表/空宿主
    public delegate* unmanaged<byte*, int> TableCols;                          // M6a 批②：列数；-1 = 无表/空宿主
    public delegate* unmanaged<byte*, int, int, byte*, uint, int> TableCell;   // M6a 批②：-1 越界/无表 -2 cap 不足；返回拷贝数
    public delegate* unmanaged<ulong, byte*, long> ClipByName;                 // M6a 批② T3c：集内按名 → clipId；-1 = 失败（T3d 起优先 AnimGraph 绑定集）
    public delegate* unmanaged<ulong, byte*, int> AnimParamSlot;               // T3d 批②：实体所绑 controller 参数名 → 槽位 0..7；-1 = 失败
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
        CopyUtf8(key, k, 15);
        CopyUtf8(text, t, 47);
        Api.RtUiSet(k, t, frac);
    }

    // ---- M5 批④（存档 + HUD 完整版；旧宿主未注册时安全降级）----

    internal static unsafe void UiClear(string key)
    {
        if (Api.RtUiClear == null || key == null) return;
        byte* k = stackalloc byte[16];
        CopyUtf8(key, k, 15);
        Api.RtUiClear(k);
    }

    internal static unsafe void UiSetColored(string key, string text, float frac, uint color)
    {
        if (Api.RtUiSetEx == null || key == null || text == null) return;
        byte* k = stackalloc byte[16];
        byte* t = stackalloc byte[48];
        CopyUtf8(key, k, 15);
        CopyUtf8(text, t, 47);
        Api.RtUiSetEx(k, t, frac, color);
    }

    internal static unsafe void UiCards(bool show, string title, string a, string b, string c)
    {
        if (Api.UiCards == null) return;
        byte* pTitle = stackalloc byte[48];
        byte* pa = stackalloc byte[48];
        byte* pb = stackalloc byte[48];
        byte* pc = stackalloc byte[48];
        CopyUtf8(title, pTitle, 47);
        CopyUtf8(a, pa, 47);
        CopyUtf8(b, pb, 47);
        CopyUtf8(c, pc, 47);
        Api.UiCards(show ? 1 : 0, pTitle, pa, pb, pc);
    }

    internal static int UiCardPick() => Api.UiCardPick != null ? Api.UiCardPick() : -1;

    // ---- M6a 批①（世界空间表现通道；旧宿主未注册时安全降级丢弃）----

    internal static unsafe void FxPopup(string text, float x, float y, uint color)
    {
        if (Api.FxPopup == null || text == null) return;
        byte* t = stackalloc byte[16];
        CopyUtf8(text, t, 15); // 与通道 char[16] 同口径（中文约 5 字截断）
        Api.FxPopup(t, x, y, color);
    }

    internal static void FxBar(ulong entity, float frac, uint color, float width)
    {
        if (Api.FxBar != null) Api.FxBar(entity, frac, color, width);
    }

    // ---- 调参下放批（分离力参数场景侧覆盖；旧宿主未注册时安全降级丢弃）----

    internal static void SetSeparationParams(float radius, float strength, int maxNeighbors, int densityCap)
    {
        if (Api.SetSeparation != null) Api.SetSeparation(radius, strength, maxNeighbors, densityCap);
    }

    /// string → UTF-8 NUL 结尾（M5 批④：中文 HUD/卡片文本；此前逐 char 截字节
    /// 只对 ASCII 正确）。ASCII 快路径零分配；非 ASCII 走 UTF8.GetBytes（低频 UI
    /// 调用可容忍小分配）。截断回退到多字节边界（不切出半个字符）。
    private static unsafe void CopyUtf8(string? s, byte* dst, int maxBytes)
    {
        if (s == null || maxBytes <= 0) { dst[0] = 0; return; }
        bool ascii = true;
        for (int i = 0; i < s.Length; i++)
            if (s[i] >= 0x80) { ascii = false; break; }
        if (ascii) {
            int n = System.Math.Min(s.Length, maxBytes);
            for (int i = 0; i < n; i++) dst[i] = (byte)s[i];
            dst[n] = 0;
            return;
        }
        byte[] b = System.Text.Encoding.UTF8.GetBytes(s);
        int m = System.Math.Min(b.Length, maxBytes);
        if (m < b.Length) { // 仅真截断时回退到多字节边界。批④后修④：原无条件
            // 回退会吃掉 CJK 结尾完整串的最后一字，且停在孤立前导字节——
            // "复活"→"复"；ASCII 结尾串（"磁力 +25%"）不触发故潜伏至今。
            while (m > 0 && (b[m - 1] & 0xC0) == 0x80) m--; // 尾部 10xxxxxx 连续段
            if (m > 0 && (b[m - 1] & 0x80) != 0) m--;       // 孤立前导字节一并去
        }
        for (int i = 0; i < m; i++) dst[i] = b[i];
        dst[m] = 0;
    }

    internal static unsafe int SaveSet(string key, ReadOnlySpan<byte> bytes)
    {
        if (Api.SaveSet == null || key == null) return -1;
        byte* k = stackalloc byte[256];
        CopyUtf8(key, k, 255);
        fixed (byte* p = bytes)
            return Api.SaveSet(k, p, (uint)bytes.Length);
    }

    internal static unsafe int SaveGetLen(string key)
    {
        if (Api.SaveGetLen == null || key == null) return -1;
        byte* k = stackalloc byte[256];
        CopyUtf8(key, k, 255);
        return Api.SaveGetLen(k);
    }

    internal static unsafe byte[]? SaveGet(string key)
    {
        int len = SaveGetLen(key);
        if (len < 0) return null;
        var buf = new byte[len];
        if (len == 0) return buf;
        byte* k = stackalloc byte[256];
        CopyUtf8(key, k, 255);
        fixed (byte* p = buf)
            Api.SaveGet(k, p, (uint)len);
        return buf;
    }

    internal static void SaveFlushCall()
    {
        if (Api.SaveFlush != null) Api.SaveFlush();
    }

    // ---- M6a 批②（配置表：Lemon.Table → World.Tables；旧宿主未注册 = -1 降级）----

    internal static unsafe int TableRows(string guidHex)
    {
        if (Api.TableRows == null || guidHex == null) return -1;
        byte* g = stackalloc byte[64];
        CopyUtf8(guidHex, g, 63);
        return Api.TableRows(g);
    }

    internal static unsafe int TableCols(string guidHex)
    {
        if (Api.TableCols == null || guidHex == null) return -1;
        byte* g = stackalloc byte[64];
        CopyUtf8(guidHex, g, 63);
        return Api.TableCols(g);
    }

    /// <summary>取格 UTF-8 串；null = 无表/越界。格上限 128 码点（≤512B），
    /// 640B 栈缓冲恒足够（-2 只可能是非法宿主表，按 null 降级）。</summary>
    internal static unsafe string? TableCell(string guidHex, int row, int col)
    {
        if (Api.TableCell == null || guidHex == null) return null;
        byte* g = stackalloc byte[64];
        CopyUtf8(guidHex, g, 63);
        byte* buf = stackalloc byte[640];
        int n = Api.TableCell(g, row, col, buf, 640);
        return n < 0 ? null : System.Text.Encoding.UTF8.GetString(buf, n);
    }

    // ---- M6a 批② T3c（动画集按名；旧宿主未注册 = -1 降级）----

    internal static unsafe long ClipByName(ulong entity, string name)
    {
        if (Api.ClipByName == null || name == null) return -1;
        byte* p = stackalloc byte[64];
        CopyUtf8(name, p, 63);
        return Api.ClipByName(entity, p);
    }

    // ---- T3d 批②（参数槽解析；旧宿主未注册 = -1 降级）----

    internal static unsafe int AnimParamSlot(ulong entity, string name)
    {
        if (Api.AnimParamSlot == null || name == null) return -1;
        byte* p = stackalloc byte[64];
        CopyUtf8(name, p, 63);
        return Api.AnimParamSlot(entity, p);
    }
}
