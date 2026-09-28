// Lemon.SDK — 游戏存档（M5 批④ D1；06 §10 形状裁剪版）---------------------------
// World 级内存 KV（key → bytes），C# 写读；落盘经宿主 IO 钩子（编辑器 = 项目
// .lemon/saves/；打包运行时 M8）。进 Play 自动载入上一局数据、ExitPlay 自动
// 兜底落盘（编辑器语义）；Save.Flush() = 显式立即落盘（幂等，全档）。
// M6a 批② T5 分档：三档三文件（Save.Chan）——Slot=slot_0.sav（局内进度，v1
// 固定单档）、Settings=settings.sav、Meta=meta.sav；方法可选参默认 Slot =
// 既有源码零改即编译。档间同键互不串；旧宿主（无 Ex 表项）chan 忽略落唯一档。
// 档内结构 = 纯 KV（引擎不解析），键约定（防后续里程碑撞僵 schema）：
//   Settings：版本化 KV——首键 "version"（整数字符串）= 键集结构版本，其余键 =
//     设置项自由增长（M6b 音量/手柄键位/画质档位直接加键），别定死成字段；
//   Meta：收集条目预留 "col.<条目id>.state" / "col.<条目id>.count"（条目 id →
//     状态/计数映射）；全局统计平键（如模板 vs.best）。
// 值格式 = 原始字节（SetString/GetString 为 UTF8 便利层；复杂结构由游戏侧
// 自行序列化——不捆绑 msgpack）。仅域线程 tick 期间有效（与 Native 表同窗口
// 约定）。不入模拟状态哈希。
namespace Lemon;

using System;
using System.Text;

public static class Save
{
    /// <summary>存档分档（M6a 批② T5）——值与引擎侧通道号一致。</summary>
    public enum Chan : byte
    {
        Slot = 0,     // 局内进度（.lemon/saves/slot_0.sav；v1 固定单档）
        Settings = 1, // 设置（跨局、非进度；版本化 KV——见本文件头注）
        Meta = 2,     // 纪录/收集（跨局；收集条目键约定见头注）
    }

    /// <summary>写/覆盖一条（key ≤ 255 字节）。</summary>
    public static void Set(string key, ReadOnlySpan<byte> bytes, Chan chan = Chan.Slot)
        => Native.SaveSet(key, bytes, (byte)chan);

    /// <summary>读一条（无此键 = null）。返回副本，改它不影响通道。</summary>
    public static byte[]? Get(string key, Chan chan = Chan.Slot)
        => Native.SaveGet(key, (byte)chan);

    public static bool HasKey(string key, Chan chan = Chan.Slot)
        => Native.SaveGetLen(key, (byte)chan) >= 0;

    /// <summary>UTF8 字符串便利层（模板数值快照的常用形态）。</summary>
    public static void SetString(string key, string value, Chan chan = Chan.Slot)
        => Native.SaveSet(key, value != null ? Encoding.UTF8.GetBytes(value) : ReadOnlySpan<byte>.Empty, (byte)chan);

    /// <summary>UTF8 字符串便利层（无此键 = null）。</summary>
    public static string? GetString(string key, Chan chan = Chan.Slot)
    {
        byte[]? b = Native.SaveGet(key, (byte)chan);
        return b != null ? Encoding.UTF8.GetString(b) : null;
    }

    /// <summary>显式立即落盘（幂等；全档——含未指定 chan 的档。未注入 IO 钩子的
    /// 宿主 = 红字一次后 no-op）。ExitPlay 兜底落盘仍在——不调用也不丢局。</summary>
    public static void Flush()
        => Native.SaveFlushCall();
}
