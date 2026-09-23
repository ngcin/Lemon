// Lemon.SDK — 热重载状态迁移包（04 §6 协议；M4.md §3.7）
// 跨域存活：StateBag 定义于 Lemon.SDK（常驻 ALC），旧域 OnHotReloadOut 写入、
// 新域 OnHotReloadIn 读取，字典本体在换装间由 Behaviours 静态持有。
// 值白名单（04 §6 回填口径）：基元值类型（int/uint/long/ulong/float/double/bool/byte）
// 与 Lemon.Vec2；string 键。托管对象引用不可迁移（会 pin 旧域）——类型不匹配/缺失
// 默认丢弃（TryGet 返回 false），不抛异常。
using System;
using System.Collections.Generic;

namespace Lemon;

/// <summary>热重载状态迁移包（key = 字段名语义；仅值类型可入包）。</summary>
public sealed class StateBag
{
    private readonly Dictionary<string, object> _vals = new();

    public int Count => _vals.Count;

    public void Set<T>(string key, in T value) where T : struct
    {
        // 白名单外（含嵌套引用字段的 struct）不入包——装箱值自身会 pin 旧域类型
        if (!IsMigratable(typeof(T))) return;
        _vals[key] = value;
    }

    public bool TryGet<T>(string key, out T value) where T : struct
    {
        value = default;
        if (!_vals.TryGetValue(key, out var raw) || raw is not T) return false; // 类型不匹配 = 丢弃
        value = (T)raw;
        return true;
    }

    private static bool IsMigratable(Type t)
    {
        if (t.IsPrimitive || t.IsEnum || t == typeof(float) || t == typeof(double) ||
            t == typeof(decimal)) return true;
        return t == typeof(Vec2); // SDK 值类型（常驻 ALC 身份，安全）
    }
}
