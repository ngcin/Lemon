// Lemon.SDK — 数学镜像（与 lemon::math::Vec2 位级一致；M3-1 布局护栏锁定）
namespace Lemon;

/// <summary>2D 向量（镜像 lemon::Vec2：float x,y，Y 轴向下=像素坐标）。</summary>
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct Vec2
{
    public float X, Y;

    public Vec2(float x, float y) { X = x; Y = y; }

    public static Vec2 Zero => default;
    public static Vec2 One => new(1f, 1f);

    public static Vec2 operator +(Vec2 a, Vec2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Vec2 operator -(Vec2 a, Vec2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Vec2 operator -(Vec2 v) => new(-v.X, -v.Y);
    public static Vec2 operator *(Vec2 v, float s) => new(v.X * s, v.Y * s);
    public static Vec2 operator *(float s, Vec2 v) => new(s * v.X, s * v.Y);
    public static Vec2 operator /(Vec2 v, float s) => new(v.X / s, v.Y / s);
}
