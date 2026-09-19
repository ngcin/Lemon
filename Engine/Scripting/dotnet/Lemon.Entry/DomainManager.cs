// Lemon.Entry — DomainManager 常驻托管线程（ADR-010 D1：域线程统一执行）
// M0 Go-NoGo 教训 8 的已知解法：ALC 的加载/卸载/脚本执行全部收敛到本线程的命令泵，
// native→managed 的 UCO 导出只投递命令并等待完成——原生线程上下文不再触碰可回收
// ALC（spike-03 实测该形态会 pin 住 ALC 导致卸载 TIMEOUT ~350ms）。
// M3 无热重载（ADR-010 D2），但 Load/Unload 自第一天可用（卸载自检 = M3 验收项）。
using System;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime;
using System.Threading;

namespace Lemon.Entry;

/// <summary>可回收脚本域：用户程序集只经此 ALC 从路径加载。</summary>
internal sealed class ScriptAlc : AssemblyLoadContext
{
    public ScriptAlc() : base("LemonScriptAlc", isCollectible: true) { }

    /// <summary>Lemon.SDK 解析到 Entry 所在 ALC 的同一实例（类型身份必须全进程唯一：
    /// IForEachSystem/Chunk/ComponentTable 的桥契约才成立。曾因 Default ALC 预载第二份
    /// 导致 typeof 分裂 → ComponentTable KeyNotFound——M3-3 实测）。</summary>
    protected override Assembly? Load(AssemblyName name)
        => name.Name == "Lemon.SDK" ? typeof(Lemon.Vec2).Assembly : null;
}

internal static unsafe class DomainManager
{
    private sealed class Command
    {
        public Action? Run;
        public Exception? Error;
        public readonly ManualResetEventSlim Done = new(false);
    }

    public static readonly string DomainThreadName = "Lemon.Domain";

    private static readonly BlockingCollection<Command> s_queue = new(new ConcurrentQueue<Command>());
    private static readonly Thread s_thread;
    private static readonly ThreadLocal<bool> s_onDomainThread = new();

    // 域状态：生命周期（load/unload/反射装配）在 UCO 调用线程执行；仅帧执行在域线程。
    // （M3-2b 实测：本机 .NET 10.0.12 上可回收 ALC 的 Unload 只在经 UCO 转场的线程上
    //  能完成回收——纯托管域线程连"零执行的裸 load→unload"都 pin。见 M3-2b 诊断记录。）
    private static ScriptAlc? s_alc;
    private static WeakReference? s_alcWeak;
    private static Assembly? s_asm;
    private static volatile TickFn? s_tickFn;
    private static double s_tickResult = double.NaN;

    /// <summary>用户脚本帧入口签名（TestScript.Tick / M3-3 起为批量系统分发器）。</summary>
    private delegate double TickFn(float dt);

    static DomainManager()
    {
        s_thread = new Thread(Loop) {
            Name = DomainThreadName,
            IsBackground = true, // 进程退出不阻塞（CoreCLR 进程级存活，不做 runtime shutdown）
        };
        s_thread.Start();
    }

    private static void Loop()
    {
        s_onDomainThread.Value = true;
        foreach (var cmd in s_queue.GetConsumingEnumerable()) {
            try { cmd.Run?.Invoke(); }
            catch (Exception e) { cmd.Error = e; }
            cmd.Done.Set();
        }
    }

    /// <summary>投递命令并等完成（域线程串行执行 = 单线程确定性，ADR-010 D1）。</summary>
    private static void Post(Action run)
    {
        if (s_onDomainThread.Value) { run(); return; } // 域线程重入：就地执行
        var cmd = new Command { Run = run };
        s_queue.Add(cmd);
        cmd.Done.Wait();
        if (cmd.Error != null) throw cmd.Error;
    }

    // ---- 域生命周期：装配与执行在域线程；卸载在 UCO 调用线程（M3-2b 实测矩阵）---
    // 本机 .NET 10.0.12 行为（今日实测 + spike 数据合并）：
    //   * 卸载线程必须从未触碰过 ALC（反射/执行/装配均不可）——UCO 主线程只发命令时 OK；
    //   * 纯托管域线程做卸载永远 TIMEOUT（连裸 load→unload 都 pin）；
    //   * 因此：装配+执行 Post 到域线程，Unload 在 UCO 线程就地执行（发命令线程）。

    public static bool LoadScript(string assemblyPath)
    {
        bool ok = false;
        Post(() => {
            if (s_alc != null) { ok = true; return; } // 已加载（幂等）
            var alc = new ScriptAlc();
            var asm = alc.LoadFromAssemblyPath(assemblyPath);
            // 换域清注册表/事件订阅 + 装配入口约定：GameMain.Configure()（无则无脚本系统）
            Lemon.Scripting.Reset();
            Lemon.Events.Reset();
            Lemon.Behaviours.Reset();
            Lemon.SceneOps.Reset();
            asm.GetType("GameMain")?.GetMethod("Configure", BindingFlags.Public | BindingFlags.Static)
               ?.Invoke(null, null);
            var tick = asm.GetType("TestScript")?.GetMethod("Tick", BindingFlags.Public | BindingFlags.Static);
            if (tick != null)
                s_tickFn = tick.CreateDelegate<TickFn>(); // 教训 7：托管委托缓存，禁 GetFunctionPointer
            s_asm = asm;
            s_alcWeak = new WeakReference(alc);
            s_alc = alc;
            ok = true;
        });
        return ok;
    }

    /// <summary>卸载并确认回收（UCO 调用线程就地：置空域线程持有的引用后 Unload+GC 轮询）。</summary>
    public static bool UnloadScript()
    {
        // 先让域线程丢掉委托/程序集引用（后续 Tick 返回 NaN 哨兵）
        Post(() => { s_tickFn = null; s_asm = null; s_alc = null; });
        var weak = s_alcWeak;
        s_alcWeak = null;
        if (weak == null) return true;
        ((AssemblyLoadContext)weak.Target!).Unload();
        for (int i = 0; i < 30; i++) {
            // compacting 强制 GC：尝试清掉执行线程残留的陈旧引用（M3-2b 诊断）
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            GC.WaitForPendingFinalizers();
            if (!weak.IsAlive) return true;
            Thread.Sleep(10);
        }
        return false;
    }

    public static bool IsLoaded => s_alc != null;

    /// <summary>帧执行（域线程）；未加载返回 NaN 哨兵（隔离验证用）。</summary>
    public static double Tick(float dt)
    {
        Post(() => s_tickResult = s_tickFn != null ? s_tickFn(dt) : double.NaN);
        return s_tickResult;
    }

    /// <summary>批量帧执行（域线程；M3-3 档②）。异常吞掉并红字——批量 tick 不因脚本异常失败。</summary>
    internal static void PostBatch(Action run)
    {
        if (s_onDomainThread.Value) { run(); return; }
        var cmd = new Command { Run = run };
        s_queue.Add(cmd);
        cmd.Done.Wait();
        // cmd.Error 不上抛：Batch 内部已做异常隔离与禁用记账
    }

    // ---- 热路径命令池（GC 纪律 04 §5：每帧命令对象/闭包/事件句柄 = ~200B/帧分配，
    //      M3-7 bench-script 判据实测抓出。UCO 调用串行 → 单槽复用安全）----------

    // 注意声明顺序：委托字段先于命令字段（静态字段按文本序初始化——先命令后委托
    // 会把 null 塞进 Run，脚本执行整体哑火。M3-7 实测假阴性坑）
    private static readonly Action s_tickBody = TickBody;
    private static readonly Action s_evtBody = EvtBody;
    private static readonly Command s_tickCmd = new() { Run = s_tickBody };
    private static readonly Command s_evtCmd = new() { Run = s_evtBody };
    private static unsafe Lemon.Entry.BatchSystemFrame* s_tFrames;
    private static int s_tCount;
    private static float s_tDt;
    private static unsafe Lemon.Interop.EventPacket* s_ePkts;
    private static int s_eCount;

    private static void TickBody()
    {
        Lemon.Behaviours.TickStartUpdate(s_tDt);
        Lemon.Entry.Batch.Tick(s_tFrames, s_tCount);
        Lemon.Behaviours.TickLateUpdate(s_tDt);
    }

    private static void EvtBody() => Lemon.Events.DispatchPackets(s_ePkts, s_eCount);

    private static void RunPooled(Command cmd)
    {
        if (s_onDomainThread.Value) { cmd.Run!.Invoke(); return; }
        cmd.Error = null;
        cmd.Done.Reset();
        s_queue.Add(cmd);
        cmd.Done.Wait();
    }

    /// <summary>批量帧执行（池化，零分配；异常已在域线程内部隔离）。</summary>
    internal static unsafe void PostBatchTick(BatchSystemFrame* frames, int count, float dt)
    {
        s_tFrames = frames; s_tCount = count; s_tDt = dt;
        RunPooled(s_tickCmd);
    }

    /// <summary>事件段派发（池化，零分配）。</summary>
    internal static unsafe void PostBatchEvents(Lemon.Interop.EventPacket* pkts, int count)
    {
        s_ePkts = pkts; s_eCount = count;
        RunPooled(s_evtCmd);
    }

    // ---- M3-2b 卸载 pin 诊断探针（长期保留：ADR-010 修订——runtime 升级复测用）---

    /// <summary>单命令内完成 load→unload（重入就地执行）：排除跨命令状态因素。</summary>
    public static bool MinCycle(string path)
    {
        bool ok = false;
        Post(() => {
            var alc = new ScriptAlc();
            var weak = new WeakReference(alc);
            _ = alc.LoadFromAssemblyPath(path);
            alc.Unload();
            for (int i = 0; i < 30; i++) {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                if (!weak.IsAlive) { ok = true; return; }
                Thread.Sleep(10);
            }
        });
        return ok;
    }

    /// <summary>只加载不建委托不存 asm（裸 LoadFromAssemblyPath）：排除委托/反射因素。</summary>
    public static bool LoadMinimal(string path)
    {
        bool ok = false;
        Post(() => {
            if (s_alc != null) return;
            var alc = new ScriptAlc();
            s_alcWeak = new WeakReference(alc);
            s_alc = alc;
            alc.LoadFromAssemblyPath(path);
            ok = true;
        });
        return ok;
    }
}
