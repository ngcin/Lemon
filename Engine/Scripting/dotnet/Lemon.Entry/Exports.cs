// Lemon.Entry — native→managed 边界（04 §1 Bootstrap 形态 + M3-1 布局护栏导出）
// 纪律（M0 Go-NoGo 教训 6/7）：
//   * 导出全部 [UnmanagedCallersOnly]，宿主按托管方法名解析（非 EntryPoint 名）；
//   * 批量入口经托管委托缓存分发，禁 MethodHandle.GetFunctionPointer（pin ALC）；
//   * 跨程序集委托/导出签名参数一律 IntPtr/基元类型（blittable 全等）。
// 注意：net10.0 中 UnmanagedCallersOnlyAttribute 在 System.Runtime.InteropServices
// （System.Runtime ref 里没有此类型——M3 实测坑）。
using System;
using System.Runtime.InteropServices;
using Lemon.Interop;

namespace Lemon.Entry;

internal static unsafe class Exports
{
    private const int Magic = 0x1E0F; // 宿主侧哨兵（spike-03 同款约定）

    [UnmanagedCallersOnly]
    public static int Bootstrap() => Magic;

    /// <summary>诊断：Scripting 注册表状态（count + 程序集身份 + ALC）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_diag_scripting(byte* outInfo, int cap)
    {
        string s = $"count={Lemon.Scripting.SystemCount} " +
            $"asm={typeof(Lemon.Scripting).Assembly.GetHashCode():x8} " +
            $"alc={System.Runtime.Loader.AssemblyLoadContext.GetLoadContext(typeof(Lemon.Scripting).Assembly)!.Name}\n";
        for (int i = 0; i < s.Length && i < cap; i++) outInfo[i] = (byte)s[i];
        if (s.Length < cap) outInfo[s.Length] = 0;
        return Lemon.Scripting.SystemCount;
    }

    /// <summary>诊断：进程内 Lemon.SDK 副本数（>1 = 类型身份分裂）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_diag_sdk_copies(byte* outInfo, int cap)
    {
        var sb = new System.Text.StringBuilder();
        int n = 0;
        foreach (var a in AppDomain.CurrentDomain.GetAssemblies()) {
            if (a.GetName().Name != "Lemon.SDK") continue;
            ++n;
            sb.Append(a.Location).Append(" @ ").Append(System.Runtime.Loader.AssemblyLoadContext
                           .GetLoadContext(a)!.Name).Append('\n');
        }
        var s = sb.ToString();
        for (int i = 0; i < s.Length && i < cap; i++) outInfo[i] = (byte)s[i];
        if (s.Length < cap) outInfo[s.Length] = 0;
        return n;
    }

    // ---- M3-2b DomainManager（域线程统一执行；ADR-010 D1）------------------------
    // 纪律（M4.6 实测闪退教训）：UnmanagedCallersOnly 导出里未捕获的托管异常 =
    // coreclr 直接 abort 整个进程。所有可能有业务异常的导出一律 try/catch 转
    // 0 返回值 + stderr（编辑器调用侧已有失败日志/红字路径）。

    /// <summary>加载用户脚本程序集（可回收 ALC，域线程执行）；1=成功。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_dm_load(byte* pathUtf8)
    {
        try {
            int len = 0;
            while (pathUtf8[len] != 0) len++;
            return DomainManager.LoadScript(System.Text.Encoding.UTF8.GetString(pathUtf8, len)) ? 1 : 0;
        } catch (Exception e) {
            Console.Error.WriteLine("[lemon] dm_load 异常（已拦，保进程）：" + e.Message);
            return 0;
        }
    }

    /// <summary>卸载脚本域并确认回收（WeakReference + GC 轮询）；1=回收成功 0=超时（pin 活着）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_dm_unload()
    {
        try { return DomainManager.UnloadScript() ? 1 : 0; }
        catch (Exception e) {
            Console.Error.WriteLine("[lemon] dm_unload 异常（已拦，保进程）：" + e.Message);
            return 0;
        }
    }

    /// <summary>M4.5 热重载换装（A 线整域重建）：StateBag 捕获 → 旧域尽力卸载 → 新域装载。
    /// 返回 1 = 新域可用；*leakCount = 累计泄漏换装数；*lastCollected = 本次旧域是否回收。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int lemon_dm_reload(byte* pathUtf8, int* leakCount, int* lastCollected)
    {
        try {
            int len = 0;
            while (pathUtf8[len] != 0) len++;
            bool ok = DomainManager.ReloadScript(System.Text.Encoding.UTF8.GetString(pathUtf8, len));
            if (leakCount != null) *leakCount = DomainManager.LeakCount;
            if (lastCollected != null) *lastCollected = DomainManager.LastCollected ? 1 : 0;
            return ok ? 1 : 0;
        } catch (Exception e) {
            Console.Error.WriteLine("[lemon] dm_reload 异常（已拦，保进程）：" + e.Message);
            if (leakCount != null) *leakCount = DomainManager.LeakCount;
            if (lastCollected != null) *lastCollected = 0;
            return 0;
        }
    }

    /// <summary>换装次数 / 累计泄漏次数（Profiler 常驻显示；M4.md §3.7）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_hr_reloads() => DomainManager.ReloadCount;

    [UnmanagedCallersOnly]
    public static int lemon_hr_leaks() => DomainManager.LeakCount;

    /// <summary>帧执行（域线程）；未加载返回 NaN 哨兵。</summary>
    [UnmanagedCallersOnly]
    public static double lemon_dm_tick(float dt) => DomainManager.Tick(dt);

    /// <summary>Time 归零（编辑器进 Play = 新的一局；M5 清障①）。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_time_reset() => Lemon.Time.Reset();

    /// <summary>进 Play 域复位（M5 批④后修）：清 behaviour 实例（含实例级订阅退订）
    /// + 事件待发/计数；静态订阅保留（Configure 期注册，跨局存活）。
    /// 编辑器 EnterPlay 在 lemon_time_reset 之后、装配新实例之前调用；bench/回放
    /// 路径不经过（金档零扰动）。旧 Entry 程序集无本导出 = 宿主判空安全 no-op。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_play_reset()
    {
        Lemon.Behaviours.ClearInstances();
        Lemon.Events.PlayReset();
        Lemon.UI.PlayReset(); // 批③c：UI 待发/计数随局清（订阅表保留，跨局存活）
    }

    // ---- M3-3 档② 批量系统 ------------------------------------------------------

    /// <summary>已注册批量系统数（load 后由宿主拉取注册表）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_batch_count() => Batch.SystemCount();

    /// <summary>系统 i 的查询组件 id 表（拷入调用方缓冲）；返回拷贝数。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_batch_query(int systemIndex, byte* dst, int cap)
        => Batch.CopyQuery(systemIndex, dst, cap);

    /// <summary>批量帧执行（域线程）：frames/count 由 C++ 管线线程构造。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_batch_tick(BatchSystemFrame* frames, int count)
        => DomainManager.PostBatch(() => Batch.Tick(frames, count));

    // ---- M3-4 事件队列桥 --------------------------------------------------------

    /// <summary>C++ → C# 批量派发（域线程；#15 两段零拷贝转发）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void lemon_events_dispatch(Lemon.Interop.EventPacket* pkts, int n)
        => DomainManager.PostBatchEvents(pkts, n); // 池化（GC 纪律）

    /// <summary>拉取脚本 pending 事件（#15 头部；拷入调用方缓冲，返回条数）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int lemon_events_pull(Lemon.Interop.EventPacket* dst, int cap)
        => Lemon.Events.PullPending(dst, cap);

    /// <summary>诊断：某事件类型累计派发数（测试用）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_events_received(int type) => Lemon.Events.ReceivedCount((GameEvent)type);

    // ---- M3-5/6 档① 脚本组件 + 结构命令缓冲 -------------------------------------

    /// <summary>C++ native 函数表注册（低频语法糖通道；Initialize 期调用）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void lemon_api_register(NativeApi* api) => Lemon.Native.Register(api);

    /// <summary>帧执行（域线程）：Start/Update → 档② 批量 → LateUpdate（一帧固定序）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void lemon_scripts_tick(BatchSystemFrame* frames, int count, float dt)
        => DomainManager.PostBatchTick(frames, count, dt); // 池化（GC 纪律）

    /// <summary>挂载脚本实例（帧首结构命令应用时调用；Awake/OnEnable 同步跑）。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_scripts_attach(int typeId, Lemon.Interop.EntityHandle e)
        => DomainManager.PostBatch(() => Lemon.Behaviours.Attach(typeId, e));

    /// <summary>实体销毁通知（Destroy 命令应用时调用；OnDestroy + 托管实例移除）。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_scripts_destroy(Lemon.Interop.EntityHandle e)
        => DomainManager.PostBatch(() => Lemon.Behaviours.Detach(e));

    /// <summary>单类型卸载（M6a 批⓪ T3：op5 DetachScript 应用时调用）。
    /// 只卸 (typeId, 实体) 一槽实例：OnDestroy + 实例级订阅退订；未挂 = 幂等 no-op。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_scripts_detach(int typeId, Lemon.Interop.EntityHandle e)
        => DomainManager.PostBatch(() => Lemon.Behaviours.DetachOne(typeId, e));

    [UnmanagedCallersOnly]
    public static int lemon_behaviours_types() => Lemon.Behaviours.TypeCount;

    /// <summary>注册脚本类型名表（M4.4 编辑器装配通路）：'\n' 分隔写入 dst，'\0' 结尾。
    /// 返回类型数；cap 不足返回 -1（调用方换大缓冲重试）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int lemon_behaviours_list(byte* dst, int cap)
    {
        var names = Lemon.Behaviours.RegisteredNames;
        int total = 0;
        foreach (var n in names) total += n.Length + 1; // 名 + '\n'
        if (total >= cap) return -1;
        int p = 0;
        foreach (var n in names) {
            for (int i = 0; i < n.Length; i++) dst[p++] = (byte)n[i];
            dst[p++] = (byte)'\n';
        }
        dst[total > 0 ? total - 1 : 0] = 0; // 末 '\n' 换成 '\0'（空表 = 首字节 \0）
        return names.Length;
    }

    [UnmanagedCallersOnly]
    public static int lemon_behaviours_attached() => Lemon.Behaviours.AttachedCount;

    /// <summary>托管累计分配字节数（验收：示例脚本每帧分配 = 0；04 §5 GC 纪律）。</summary>
    [UnmanagedCallersOnly]
    public static ulong lemon_gc_allocated() => (ulong)System.GC.GetTotalAllocatedBytes(false);

    /// <summary>拉取脚本结构命令（帧首 Essential 应用）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int lemon_ops_pull(Lemon.SceneOp* dst, int cap)
        => Lemon.SceneOps.PullPending(dst, cap);

    /// <summary>直投结构命令（测试/工具通道；脚本侧用 SceneOps.*）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void lemon_ops_submit(byte type, byte compId, ulong e)
        => Lemon.SceneOps.SubmitRaw(type, compId, e);

    // ---- M6a 批③c（ADR-014 M2/M3）：UI ops 拉取 + UiEvent 派发 --------------------
    // 旧宿主（批③c 前 C++）不解析本对导出 = 零影响；新宿主缺本 Entry 构建 = 挂空安全。

    /// <summary>拉取脚本 UI ops（TickBatch 尾：staging 经 UI.Apply() 入 ready）。
    /// *arenaBytes = arena 实际字节数；返回 op 数；-1 = 超容量整批丢弃（响亮）。</summary>
    [UnmanagedCallersOnly]
    public static unsafe int lemon_ui_ops_pull(Lemon.UiOp* dst, int capOps,
                                               byte* dstArena, int capArena, int* arenaBytes)
    {
        try { return Lemon.UI.PullOps(dst, capOps, dstArena, capArena, arenaBytes); }
        catch (Exception e) { // M4.6 导出纪律（上方 52 行）：未捕获 = coreclr abort
            Console.Error.WriteLine("[lemon] ui_ops_pull 异常（已拦，保进程）：" + e.Message);
            Lemon.UI.DiscardPending(); // -1 语义 = 整批已丢弃——把契约做实
            if (arenaBytes != null) *arenaBytes = 0;
            return -1;
        }
    }

    /// <summary>UI 事件批量派发（#16 头部：引擎文档监听器 → 订阅者）。
    /// 订阅者异常由 UI.DispatchEvents 逐个隔离；此处兜底编码/框架层异常。</summary>
    [UnmanagedCallersOnly]
    public static unsafe void lemon_ui_events_dispatch(Lemon.UiEvent* src, int n)
    {
        try { Lemon.UI.DispatchEvents(src, n); }
        catch (Exception e) {
            Console.Error.WriteLine("[lemon] ui_events_dispatch 异常（已拦，保进程）：" + e.Message);
        }
    }

    // ---- M3-2b 卸载 pin 诊断探针（长期保留：ADR-010 修订——.NET runtime 升级后
    //      重跑 lemon-script-tests 即知 pin 行为是否修复）--------------------------
    [UnmanagedCallersOnly]
    public static int lemon_diag_min_cycle(byte* pathUtf8)
    {
        int len = 0;
        while (pathUtf8[len] != 0) len++;
        return DomainManager.MinCycle(System.Text.Encoding.UTF8.GetString(pathUtf8, len)) ? 1 : 0;
    }

    [UnmanagedCallersOnly]
    public static int lemon_diag_load_minimal(byte* pathUtf8)
    {
        int len = 0;
        while (pathUtf8[len] != 0) len++;
        return DomainManager.LoadMinimal(System.Text.Encoding.UTF8.GetString(pathUtf8, len)) ? 1 : 0;
    }

    /// <summary>UCO 调用线程就地 load→unload（spike UnloadSelfTest 同形态；不经域线程）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_diag_min_cycle_uco(byte* pathUtf8)
    {
        int len = 0;
        while (pathUtf8[len] != 0) len++;
        string path = System.Text.Encoding.UTF8.GetString(pathUtf8, len);
        var alc = new ScriptAlc();
        var weak = new WeakReference(alc);
        _ = alc.LoadFromAssemblyPath(path);
        alc.Unload();
        for (int i = 0; i < 30; i++) {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            if (!weak.IsAlive) return 1;
            System.Threading.Thread.Sleep(10);
        }
        return 0;
    }


    // ---- M3-1 布局一致性护栏 ---------------------------------------------------

    /// <summary>填充 C# 侧布局自报表；返回组件数（=27）；缓冲不足返回 -1。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_sdk_layout(IntPtr pComps, int compCap, IntPtr pFields, int fieldCap,
                                       IntPtr pSegs, int segCap)
    {
        var comps = LayoutTables.Comps;
        var fields = LayoutTables.Fields;
        var segs = LayoutTables.Segs;
        if (compCap < comps.Length || fieldCap < fields.Length || segCap < segs.Length) return -1;
        comps.AsSpan().CopyTo(new Span<CompLayoutRow>((void*)pComps, comps.Length));
        fields.AsSpan().CopyTo(new Span<FieldLayoutRow>((void*)pFields, fields.Length));
        segs.AsSpan().CopyTo(new Span<SegLayoutRow>((void*)pSegs, segs.Length));
        return comps.Length;
    }

    /// <summary>整块 blit 拷贝（count 个 compId 组件；尺寸取自 C# 自报表——表已被 C++ 核对）。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_blit_copy(IntPtr dst, IntPtr src, int compId, int count)
    {
        uint size = LayoutTables.Comps[compId].SizeOf;
        Buffer.MemoryCopy((void*)src, (void*)dst, (ulong)size * (ulong)count,
                          (ulong)size * (ulong)count);
    }

    /// <summary>typed roundtrip：经镜像 struct 写入约定值，C++ 侧核对落到确切字段。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_roundtrip_typed(IntPtr pTransform, IntPtr pStatus)
    {
        var t = (Transform2D*)pTransform;
        t->Pos = new Vec2(11f, 22f);
        t->Rot = 0.5f;
        t->Scale = new Vec2(3f, 4f);

        var s = (StatusEffects*)pStatus;
        s->Count = 2;
        s->Slot(1) = new StatusInst { Id = 7, Stacks = 3, Remain = 1.25f, Source = 9 };
    }

    /// <summary>RNG golden：同 (seed,stream) 抽 n 个 uint，C++ 与 lemon::Rng 比对。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_rng_fill(ulong seed, ulong stream, IntPtr outU32, int n)
    {
        var r = new Pcg32(seed, stream);
        var dst = (uint*)outU32;
        for (int i = 0; i < n; i++) dst[i] = r.Next();
    }

    /// <summary>RNG golden：Float01 序列（高 24 位位级确定）。</summary>
    [UnmanagedCallersOnly]
    public static void lemon_rng_float01(ulong seed, ulong stream, IntPtr outF, int n)
    {
        var r = new Pcg32(seed, stream);
        var dst = (float*)outF;
        for (int i = 0; i < n; i++) dst[i] = r.Float01();
    }

    /// <summary>RNG 回归（F-10）：Range 单点/逆序区间——老实现 span==1 时 zone 截 0
    /// 会永久死循环；挂钟上限内返回即双端同语义的证据。</summary>
    [UnmanagedCallersOnly]
    public static uint lemon_rng_range(uint lo, uint hi)
    {
        var r = new Pcg32(1, 1);
        return r.Range(lo, hi);
    }

    /// <summary>EventPacket 布局自报（返回 sizeof；各字段偏移写入 out 指针）。</summary>
    [UnmanagedCallersOnly]
    public static int lemon_eventpacket_layout(ushort* offType, ushort* offUser, ushort* offSrc,
                                               ushort* offDst, ushort* offPayload, ushort* offUserArg)
    {
        EventPacket t = default;
        EventPacket* p = &t;
        byte* b = (byte*)p;
        *offType = (ushort)((byte*)&p->Type - b);
        *offUser = (ushort)((byte*)&p->User - b);
        *offSrc = (ushort)((byte*)&p->Src - b);
        *offDst = (ushort)((byte*)&p->Dst - b);
        *offPayload = (ushort)((byte*)p->Payload - b);
        *offUserArg = (ushort)((byte*)&p->UserArg - b);
        return sizeof(EventPacket);
    }
}
