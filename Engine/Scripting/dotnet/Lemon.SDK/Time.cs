// Lemon.SDK — 帧时间（M5 清障①；04 §3.2 Update = 固定步长语义）
// 数值来源：C++ 步进循环的 dt，域线程 TickBody 首行 Advance（零跨界调用、零分配——
// 档① Update 与档② ForEach 同帧读到同值：Advance → Start/Update → 批量 → LateUpdate）。
// 归零时机 = 新程序集装载（DomainManager.LoadScript）与编辑器进 Play
// （lemon_time_reset ← EditorContext::EnterPlay）——Time 属于"一局"，不属程序集域。
// timeScale（M5 批① D5）：World::Step 内缩放（dt = fixedDt × scale），本类型只经
// native 表读写——DeltaTime 拿到的已是缩放后值；=0 冻结暂停（Unity 同款语义）。
namespace Lemon;

public static class Time
{
    /// <summary>当前步长（秒）。固定步长语义（默认 60Hz = 1/60；测试/回放可为其他值）。
    /// 已含 timeScale 缩放（C++ Step 缩放后传入）。</summary>
    public static float DeltaTime { get; private set; }

    private static double s_time; // double 累计（长局零漂移），对外转 float
    /// <summary>自本局开始的累计时间（秒）。≙ Unity Time.time（C# 禁止成员与类型同名，
    /// 故取 .NET Stopwatch.Elapsed 心智命名）。按缩放 dt 累计（暂停不走表）。</summary>
    public static float Elapsed => (float)s_time;

    /// <summary>自本局开始的固定步数（帧号）。不受 timeScale 影响（暂停照走）。</summary>
    public static int FrameCount { get; private set; }

    /// <summary>时间缩放（01 §2：导演慢动作/暂停，作用于 C++ 模拟步进）。
    /// clamp [0,8]（C++ 侧）；旧宿主未注册表项时写丢弃、读恒 1。</summary>
    public static float Scale
    {
        get => Native.TimeScale();
        set => Native.SetTimeScale(value);
    }

    internal static void Advance(float dt)
    {
        DeltaTime = dt;
        s_time += dt;
        ++FrameCount;
    }

    internal static void Reset()
    {
        DeltaTime = 0f;
        s_time = 0.0;
        FrameCount = 0;
    }
}
