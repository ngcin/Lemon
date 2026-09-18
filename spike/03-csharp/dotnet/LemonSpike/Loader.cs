using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace LemonSpike;

/// <summary>Collectible ALC：热重载的载体（Luma ScriptLoadContext 模式的最小版）。</summary>
internal sealed class ScriptAlc : AssemblyLoadContext
{
    public ScriptAlc() : base("LemonScriptAlc", isCollectible: true) { }
    protected override Assembly? Load(AssemblyName name) => null;  // 全部依赖走默认 ALC
}

/// <summary>C ABI 导出（C++ 经 load_assembly_and_get_function_pointer 逐个取址）。</summary>
internal static unsafe class Exports
{
    private static ScriptAlc? _alc;
    private static WeakReference? _alcWeak;   // 官方推荐的卸载判定：WeakReference.IsAlive
    private static Assembly? _asm;
    private static object? _rotator;
    private static MethodInfo? _onCreate, _onUpdate;

    // 批量入口：托管委托缓存。
    // 注意①：不能用 MethodHandle.GetFunctionPointer() —— 会对可回收程序集的方法永久 pin，
    //           阻止 ALC 卸载（spike 实测教训）。委托引用置空后方法可正常回收。
    // 注意②：委托参数用 IntPtr —— 跨程序集委托绑定要求类型全等，结构体指针不满足。
    private delegate double TickFn(IntPtr data, int count, float t);
    private static TickFn? _tickFn;

    private struct Instance  // 仅用于本地 fnptr 签名，布局与 Script.Instance 一致
    {
        public float PosX, PosY, Rot, Scale;
        public uint Color;
    }

    [UnmanagedCallersOnly(EntryPoint = "lemon_bootstrap")]
    public static int Bootstrap() => 0x1E0F;  // "lemon" 标记，C++ 侧校验

    [UnmanagedCallersOnly(EntryPoint = "lemon_load")]
    public static int Load(byte* pathUtf8)
    {
        try
        {
            string path = System.Text.Encoding.UTF8.GetString(pathUtf8, StrLen(pathUtf8));
            // 在专用托管线程上执行加载：UnmanagedCallersOnly 帧的原生线程上下文
            // 会 pin 住当次调用内创建的 Collectible ALC（spike 实测；托管线程隔离后可正常卸载）
            int ok = 0;
            var t = new Thread(() => ok = LoadCore(Path.GetFullPath(path)));
            t.IsBackground = true;
            t.Start();
            t.Join();
            return ok;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"[lemon-cs] load failed: {e}");
            return 0;
        }
    }

    private static int LoadCore(string fullPath)
    {
        try
        {
            _alc = new ScriptAlc();
            _alcWeak = new WeakReference(_alc);
            _asm = _alc.LoadFromAssemblyPath(fullPath);

            // 批量入口：反射拿 MethodInfo → 缓存托管委托（避免 fnptr pin 可回收方法）
            var tick = _asm.GetType("Script.LemonScript")!.GetMethod("Tick", BindingFlags.Public | BindingFlags.Static)!;
            _tickFn = tick.CreateDelegate<TickFn>();

            // 生命周期入口：实例化 + Invoke 调用（真实引擎 M3 中脚本组件继承 SDK 基类，
            // 强类型调度无反射；此处测反射调用的上限作为最差情况）
            var rotType = _asm.GetType("Script.Rotator")!;
            _rotator = Activator.CreateInstance(rotType);
            _onCreate = rotType.GetMethod("OnCreate")!;
            _onUpdate = rotType.GetMethod("OnUpdate")!;
            _onCreate.Invoke(_rotator, null);
            return 1;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"[lemon-cs] load failed: {e}");
            return 0;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "lemon_unload")]
    public static int Unload()
    {
        _tickFn = null;
        _onCreate = _onUpdate = null;
        _rotator = null;
        _asm = null;
        _alc!.Unload();
        for (int i = 0; i < 30; i++)   // 标准卸载等待循环：GC ×2 + WeakReference 检查
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            if (!_alcWeak!.IsAlive) { _alc = null; return 1; }
            Thread.Sleep(10);
        }
        Console.Error.WriteLine("[lemon-cs] ALC unload TIMEOUT（有引用泄漏，热重载不可用）");
        return 0;
    }

    [UnmanagedCallersOnly(EntryPoint = "lemon_script_tick")]
    public static unsafe double ScriptTick(void* data, int count, float t)
    {
        if (_tickFn == null) return double.NaN;
        return _tickFn((IntPtr)data, count, t);
    }

    // 生命周期微基准：n 个 Rotator × ticks 次 OnUpdate（反射 Invoke），返回平均每次调用微秒数
    [UnmanagedCallersOnly(EntryPoint = "lemon_lifecycle_demo")]
    public static double LifecycleDemo(int n, int ticks)
    {
        var asm2 = _alc!.LoadFromAssemblyPath(_asm!.Location);   // 同 ALC 内同一类型
        var rotType = asm2.GetType("Script.Rotator")!;
        var create = rotType.GetMethod("OnCreate")!;
        var update = rotType.GetMethod("OnUpdate")!;
        var objs = new object[n];
        for (int i = 0; i < n; i++)
        {
            objs[i] = Activator.CreateInstance(rotType)!;
            create.Invoke(objs[i], null);
        }
        var args = new object[] { 1f / 60f };
        long sw = System.Diagnostics.Stopwatch.GetTimestamp();
        double sink = 0;
        for (int k = 0; k < ticks; k++)
            for (int i = 0; i < n; i++)
                sink += (float)update.Invoke(objs[i], args)!;
        double us = (System.Diagnostics.Stopwatch.GetTimestamp() - sw) * 1e6
                    / System.Diagnostics.Stopwatch.Frequency / ((double)n * ticks);
        return sink == 12345.678 ? 0 : us;  // 引用 sink 防 DCE；返回 µs/call
    }

    // 卸载隔离自检：一次托管调用内完成 load→unload（不返回 native），
    // 用于定位"跨原生边界的流程 pin 了 ALC"还是"程序集本身不可回收"
    [UnmanagedCallersOnly(EntryPoint = "lemon_unload_selftest")]
    public static int UnloadSelfTest(byte* pathUtf8)
    {
        string path = System.Text.Encoding.UTF8.GetString(pathUtf8, StrLen(pathUtf8));
        var alc = new ScriptAlc();
        var weak = new WeakReference(alc);
        var asm = alc.LoadFromAssemblyPath(Path.GetFullPath(path));
        asm.GetType("Script.Rotator");
        var rot = Activator.CreateInstance(asm.GetType("Script.Rotator")!);
        asm.GetType("Script.Rotator")!.GetMethod("OnCreate")!.Invoke(rot, null);
        alc.Unload();
        asm = null; rot = null;
        for (int i = 0; i < 100; i++)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            if (!weak.IsAlive) return 1;
            Thread.Sleep(5);
        }
        return 0;
    }

    private static int StrLen(byte* p)
    {
        int n = 0;
        while (p[n] != 0) ++n;
        return n;
    }
}
