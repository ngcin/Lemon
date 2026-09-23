// Lemon.SDK — Input 语义读取（M4.md §4-8 最小增量 / ADR-010 D4 分期）
// 读 World 当前 InputState 快照（编辑器 GameView 门控注入 / 运行时平台采样）。
// 位分配与 lemon::ecs::InputState 一致（bit0=up 1=down 2=left 3=right 4=attack）；
// 完整输入动作表/手柄 M5（03 §4）。
using System;

namespace Lemon;

/// <summary>语义键位（与 C++ InputState.buttons 位分配同步改）。</summary>
public static class InputButton
{
    public const int Up = 0;
    public const int Down = 1;
    public const int Left = 2;
    public const int Right = 3;
    public const int Attack = 4;
    public const int Confirm = 5; // M5 批④：确认/重开（编辑器映射 R 键）
}

public static class Input
{
    /// <summary>移动轴 -1..1（键盘合成/摇杆；Y 向下 = 正）。</summary>
    public static Vec2 Axis
    {
        get {
            var (_, ax, ay) = Native.Input();
            return new Vec2(ax, ay);
        }
    }

    /// <summary>语义键位按下（bit 越界恒 false；宿主未接输入 = 全 false）。</summary>
    public static bool GetButton(int bit)
    {
        if ((uint)bit >= 64) return false;
        var (buttons, _, _) = Native.Input();
        return (buttons & (1ul << bit)) != 0;
    }

    public static bool Up => GetButton(InputButton.Up);
    public static bool Down => GetButton(InputButton.Down);
    public static bool Left => GetButton(InputButton.Left);
    public static bool Right => GetButton(InputButton.Right);
    public static bool Attack => GetButton(InputButton.Attack);
    public static bool Confirm => GetButton(InputButton.Confirm);
}
