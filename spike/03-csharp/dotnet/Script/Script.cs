using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Script;

// 与 C++ 侧 Instance 完全一致的 24B 布局（spike/03-csharp/main.cpp）
// [StructLayout(LayoutKind.Sequential)] 对 blittable 基元字段默认顺序布局即可
public struct Instance
{
    public float PosX, PosY;
    public float Rot;
    public float Scale;
    public uint Color;
}

/// <summary>模拟"用户脚本"程序集：热重载的真正目标。</summary>
public static class LemonScript
{
    // 批量系统（设计文档 04 §2.2 档② 的雏形）：一次调用处理全部实例，
    // 与 spike/02 的 CPU 更新做同一份数学（环绕轨道），供 C# vs C++ 对比。
    // 参数用 IntPtr：跨程序集委托绑定要求类型全等，裸指针结构体类型无法满足。
    // 注意：不能加 [UnmanagedCallersOnly]（该类方法禁止被托管代码调用；本方法经托管委托分发）
    public static unsafe double Tick(IntPtr data, int count, float t)
    {
        var inst = (Instance*)data;
        const float Cx = 640f, Cy = 360f;
        double checksum = 0;
        for (int i = 0; i < count; i++)
        {
            float a = (i % 360) * 0.017453293f + t;
            inst[i].PosX = Cx + MathF.Cos(a) * (8f + (i % 500));
            inst[i].PosY = Cy + MathF.Sin(a) * (8f + (i % 500)) * 0.62f;
            inst[i].Rot = a * 2f;
            checksum += inst[i].PosX;
        }
        return checksum;
    }
}

/// <summary>模拟脚本组件（档① 的雏形）：反射实例化 + 生命周期调用。</summary>
public class Rotator
{
    private float _angle;
    private int _id;

    public void OnCreate() { _id = nextId++; _angle = _id * 0.01f; }

    public float OnUpdate(float dt)
    {
        _angle += dt * 1.5f;
        return MathF.Sin(_angle);
    }

    private static int nextId;
}
