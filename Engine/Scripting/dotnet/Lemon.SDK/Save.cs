// Lemon.SDK — 游戏存档（M5 批④ D1；06 §10 形状裁剪版）---------------------------
// World 级内存 KV（key → bytes），C# 写读；落盘经宿主 IO 钩子（编辑器 = 项目
// .lemon/saves/game.sav；打包运行时 M8）。进 Play 自动载入上一局数据、
// ExitPlay 自动兜底落盘（编辑器语义）；Save.Flush() = 显式立即落盘（幂等）。
// M5 单档单文件；slot_N/settings/meta 三类分档推 M6（06 §10 注记修订，
// 模板用 key 前缀区分语义，如 "meta.best" / "run.kills"）。
// 值格式 = 原始字节（SetString/GetString 为 UTF8 便利层；复杂结构由游戏侧
// 自行序列化——M5 不捆绑 msgpack）。
// 仅域线程 tick 期间有效（与 Native 表同窗口约定）。不入模拟状态哈希。
namespace Lemon;

using System;
using System.Text;

public static class Save
{
    /// <summary>写/覆盖一条（key ≤ 255 字节）。</summary>
    public static void Set(string key, ReadOnlySpan<byte> bytes)
        => Native.SaveSet(key, bytes);

    /// <summary>读一条（无此键 = null）。返回副本，改它不影响通道。</summary>
    public static byte[]? Get(string key)
        => Native.SaveGet(key);

    public static bool HasKey(string key)
        => Native.SaveGetLen(key) >= 0;

    /// <summary>UTF8 字符串便利层（模板数值快照的常用形态）。</summary>
    public static void SetString(string key, string value)
        => Native.SaveSet(key, value != null ? Encoding.UTF8.GetBytes(value) : ReadOnlySpan<byte>.Empty);

    /// <summary>UTF8 字符串便利层（无此键 = null）。</summary>
    public static string? GetString(string key)
    {
        byte[]? b = Native.SaveGet(key);
        return b != null ? Encoding.UTF8.GetString(b) : null;
    }

    /// <summary>显式立即落盘（幂等；未注入 IO 钩子的宿主 = 红字一次后 no-op）。
    /// ExitPlay 兜底落盘仍在——不调用也不丢局。</summary>
    public static void Flush()
        => Native.SaveFlushCall();
}
