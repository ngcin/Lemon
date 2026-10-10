// Lemon.SDK — 配置表读取（M6a 批② T2；ADR-012 D1：.tab 全字符串格，第 0 行 = 列头）
// 表资产按 16 位 hex GUID 定位（AssetBrowser「复制 GUID」同源）。进 Play 时刻快照
// ——Play 中改 .tab 下一局生效（与 clip 同语义）。数值解释归本层：Int/Float 容错
// （空串/格式错 = 0 + warn 一次/格——反复读同一坏格不刷屏）。
using System;
using System.Collections.Generic;

namespace Lemon;

public static class Table
{
    /// <summary>表存在（无此表/宿主未注册通道 = false）。</summary>
    public static bool Has(string guidHex) => Native.TableRows(guidHex) >= 0;

    /// <summary>行数（含第 0 行列头）；-1 = 无表/空宿主。</summary>
    public static int Rows(string guidHex) => Native.TableRows(guidHex);

    /// <summary>列数；-1 = 无表/空宿主。</summary>
    public static int Cols(string guidHex) => Native.TableCols(guidHex);

    /// <summary>取格原串（UTF-8）；null = 无表/越界。数据行自 row=1 起（row=0 = 列头）。</summary>
    public static string? Str(string guidHex, int row, int col)
        => Native.TableCell(guidHex, row, col);

    /// <summary>取格 int（Invariant 解析；空/格式错 = 0 + warn 一次/格）。</summary>
    public static int Int(string guidHex, int row, int col)
    {
        string? s = Native.TableCell(guidHex, row, col);
        if (int.TryParse(s, System.Globalization.NumberStyles.Integer,
                         System.Globalization.CultureInfo.InvariantCulture, out int v))
            return v;
        WarnOnce(guidHex, row, col, s, "Int");
        return 0;
    }

    /// <summary>取格 float（Invariant 解析；空/格式错 = 0 + warn 一次/格）。</summary>
    public static float Float(string guidHex, int row, int col)
    {
        string? s = Native.TableCell(guidHex, row, col);
        if (float.TryParse(s, System.Globalization.NumberStyles.Float,
                           System.Globalization.CultureInfo.InvariantCulture, out float v))
            return v;
        WarnOnce(guidHex, row, col, s, "Float");
        return 0;
    }

    static readonly HashSet<string> s_warned = new HashSet<string>();

    /// <summary>随局/换域清告警去重表（L17：跨局陈旧键残留，与 Anim.ResetWarnTables 同批）。</summary>
    internal static void ResetWarnTables() => s_warned.Clear();

    static void WarnOnce(string guidHex, int row, int col, string? val, string kind)
    {
        if (s_warned.Add(kind + ":" + guidHex + ":" + row + ":" + col))
            Console.Error.WriteLine(
                $"[lemon][warn] Table.{kind}('{guidHex}',{row},{col}): " +
                $"非数值 '{val ?? "(null)"}' → 0");
    }
}
