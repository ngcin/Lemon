// Lemon.SDK — 帧时间（M5 清障①；04 §3.2 Update = 固定步长语义）
// 数值来源：C++ 步进循环的 dt，域线程 TickBody 首行 Advance（零跨界调用、零分配——
// 档① Update 与档② ForEach 同帧读到同值：Advance → Start/Update → 批量 → LateUpdate）。
// 归零时机 = 新程序集装载（DomainManager.LoadScript）与编辑器进 Play
// （lemon_time_reset ← EditorContext::EnterPlay）——Time 属于"一局"，不属程序集域。
// timeScale（导演慢动作/暂停，01 §2）随 M5 导演系统批次接 C++ 步进，届时再加。
namespace Lemon;

public static class Time
{
    /// <summary>当前步长（秒）。固定步长语义（默认 60Hz = 1/60；测试/回放可为其他值）。</summary>
    public static float DeltaTime { get; private set; }

    private static double s_time; // double 累计（长局零漂移），对外转 float
    /// <summary>自本局开始的累计时间（秒）。≙ Unity Time.time（C# 禁止成员与类型同名，
    /// 故取 .NET Stopwatch.Elapsed 心智命名）。</summary>
    public static float Elapsed => (float)s_time;

    /// <summary>自本局开始的固定步数（帧号）。</summary>
    public static int FrameCount { get; private set; }

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
