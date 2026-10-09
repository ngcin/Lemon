// Lemon.SDK — SceneManager 门面（M7c 批⑦；ADR-017 C# API 面 / D1–D8）
// 命名逐项对齐 Unity（D8：flat Lemon.SceneManager，不建 SceneManagement 子命名空间）。
// 时序契约（换场帧协议 ⑤，D3 同步直推）：LoadScene 当帧照常跑完 → 下一帧 Essential
// 换场窗口内 sceneUnloaded → sceneLoaded（新场 Awake/OnEnable 后、同帧 Start/Update
// 前）→ activeSceneChanged——三事件都在换场帧内、早于新场脚本首帧 Update。
// v1 口径（ADR-017）：Single 唯一装载；Additive = 红字未实现（D2 预留）；
// sceneCount = isLoaded 档案数（DDOL 不建模伪场景——Unity 差异）；SetActiveScene
// 仅当前 active 合法；无 buildIndex（D4 砍单）。
// 批⑧ LoadSceneAsync（本文件）：分帧状态机（Parse→Build(暂存)→Assets→Gate→激活）
// ——加载期间旧场照常 tick（要冻结自己 Time.Scale=0）；progress = 纯呈现量（预算
// 依赖、跨机器不确定）——**玩法逻辑禁挂 progress 分支**，只许挂 sceneLoaded/
// isDone/completed（ADR-017 D3 响亮规则）；单在途（新请求 WARN 取代——被取代
// op 已取消，completed 不推）。
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Lemon;

/// <summary>装载模式（Unity 对齐）。v1 仅 Single；Additive = 红字未实现（ADR-017 D2 预留）。</summary>
public enum LoadSceneMode
{
    Single = 0,
    Additive = 1,
}

/// <summary>场景档案只读视图（Unity Scene 同构；无 buildIndex——D4）。
/// 快照语义：字段取查询时刻值（Unity Scene 结构同为快照）。default = 无效场景
/// （isValid == false）。name/path 档案级不可变——SDK 侧按句柄记忆化防逐帧分配。</summary>
public readonly struct Scene
{
    public readonly int Handle;
    public readonly string name;
    public readonly string path;
    public readonly bool isLoaded;
    public readonly int rootCount;

    public bool isValid => Handle != 0;

    internal Scene(int handle, string name, string path, bool isLoaded, int rootCount)
    {
        Handle = handle;
        this.name = name;
        this.path = path;
        this.isLoaded = isLoaded;
        this.rootCount = rootCount;
    }
}

/// <summary>异步装载句柄（Unity AsyncOperation 同构；M7c 批⑧）。
/// progress：0..1；allowSceneActivation=false 时封顶 0.9（Unity 同款），激活跳 1.0。
/// **progress 为纯呈现量**（分帧预算依赖、跨机器不确定）——玩法分支只许挂
/// sceneLoaded/isDone/completed（ADR-017 D3 响亮规则）。
/// completed：订阅状态住 SceneManager 静态注册表（readonly struct 值拷贝下事件
/// 可用的唯一形态——订阅即达，拷贝共享）；完成时恰推一次；订阅已终态 op = 立即
/// 同步触发。await：域线程同步续跑（激活 Essential 窗口内，无线程池跳 Hop）。
/// default（opId=0）= 无效 op（Additive 红字返回值）：progress 恒 0 / isDone 恒
/// false / completed 不触发。</summary>
public readonly struct AsyncSceneLoad
{
    public readonly uint OpId;

    internal AsyncSceneLoad(uint opId) { OpId = opId; }

    public bool isValid => OpId != 0;

    public float progress {
        get { Native.SceneAsyncQuery(OpId, out float p, out _); return p; }
    }

    public bool isDone {
        get { Native.SceneAsyncQuery(OpId, out _, out bool d); return d; }
    }

    /// <summary>激活门（默认 true）。false = 预备段完成后停 0.9 等门（世界照常
    /// tick）；置 true 后下一 Essential 激活。终态/未知 op 的 set = 丢弃（引擎侧
    /// 0 返回）；get = 本侧镜像（default true）。</summary>
    public bool allowSceneActivation {
        get => SceneManager.AllowMirrorOf(OpId);
        set {
            SceneManager.MirrorAllow(OpId, value);
            Native.SceneAsyncSetActivation(OpId, value);
        }
    }

    public event Action<AsyncSceneLoad>? completed {
        add => SceneManager.SubscribeCompleted(OpId, value, invokeIfDone: true);
        remove => SceneManager.UnsubscribeCompleted(OpId, value);
    }

    public AsyncSceneLoadAwaiter GetAwaiter() => new AsyncSceneLoadAwaiter(OpId);
}

/// <summary>自定义 awaiter（域线程同步续跑——completed 推送窗口内联执行，与
/// Unity SynchronizationContext 语义不同但时序更紧：await 恢复点 = 激活 Essential
/// 收口处，先于新场脚本同帧 Update）。GetResult 无返回值；失败终态不抛（世界未
/// 变、红字已响——批⑧ 失败契约）。</summary>
public readonly struct AsyncSceneLoadAwaiter :
    System.Runtime.CompilerServices.ICriticalNotifyCompletion
{
    private readonly uint opId;
    internal AsyncSceneLoadAwaiter(uint opId) { this.opId = opId; }

    public bool IsCompleted {
        get { Native.SceneAsyncQuery(opId, out _, out bool d); return d; }
    }

    public void OnCompleted(Action continuation) => UnsafeOnCompleted(continuation);

    public void UnsafeOnCompleted(Action continuation) =>
        SceneManager.SubscribeCompletedOnce(opId, continuation);

    public void GetResult() { }
}

/// <summary>场景管理门面（Unity SceneManager 手感；ADR-017 §C# API 面）。</summary>
public static class SceneManager
{
    public static event Action<Scene, LoadSceneMode>? sceneLoaded;
    public static event Action<Scene>? sceneUnloaded;
    public static event Action<Scene, Scene>? activeSceneChanged;

    /// <summary>档案 name/path 记忆化（档案级不可变 + 句柄单调不复用；PlayReset
    /// 清空——编辑器重进 Play = 新 playWorld 句柄从 1 重发，不清 = 跨局撞车）。</summary>
    private static readonly Dictionary<uint, (string name, string path)> s_names = new();

    // ---- 批⑧ async 订阅/镜像注册表（AsyncSceneLoad struct 值拷贝下的集中态）----
    private static readonly Dictionary<uint, Action<AsyncSceneLoad>?> s_completed = new();
    private static readonly Dictionary<uint, Action> s_awaitOnce = new();
    private static readonly Dictionary<uint, bool> s_allowMirror = new();
    /// <summary>当前在途 op（review F1：引擎单槽 last-wins 取代在途 op 时其注册表项
    /// 永不回调——发行新请求时代收清退，防闭包/镜像跨局前累积）。</summary>
    private static uint s_inFlight;

    // ---- 装载 ---------------------------------------------------------------

    /// <summary>同步换场（Unity 语义：当帧照常跑完，下一帧装载——vtable 侧入队
    /// SceneSwitcher，Essential #18 执行）。寻址：项目相对路径 > 唯一文件名 stem >
    /// 红字响亮失败（D4；解析在宿主 SceneSourceHooks——未注册 = 红字拒）。
    /// 在途 async op 存在时：单槽统一（批⑧ D3=A）——同步显式请求赢，async 被取消
    /// （completed 不推，引擎 WARN 交底）。</summary>
    public static void LoadScene(string nameOrPath) => LoadScene(nameOrPath, LoadSceneMode.Single);

    public static void LoadScene(string nameOrPath, LoadSceneMode mode)
    {
        if (mode != LoadSceneMode.Single) {
            Console.Error.WriteLine(
                "[lemon][error] SceneManager.LoadScene：LoadSceneMode.Additive 未实现" +
                "（ADR-017 D2 预留）");
            return;
        }
        if (Native.SceneLoadRequest(nameOrPath, (byte)mode) != 1 && !string.IsNullOrEmpty(nameOrPath))
            Console.Error.WriteLine(
                "[lemon][error] SceneManager.LoadScene：换场请求未被受理（场景不可解析或宿主未注册场景源）'"
                + nameOrPath + "'");
    }

    /// <summary>异步换场（M7c 批⑧；ADR-017 D3 分帧状态机）：加载期间旧场照常
    /// tick。v1 单在途——新请求（同步或异步）WARN 取代在途者，被取代 op 已取消
    /// （completed 不推）。同档同宿主下小场景一帧直抵激活（与 LoadScene 同帧效）。
    /// Additive = 红字 + 无效 op（isValid=false）。</summary>
    public static AsyncSceneLoad LoadSceneAsync(string nameOrPath,
                                                LoadSceneMode mode = LoadSceneMode.Single)
    {
        if (mode != LoadSceneMode.Single) {
            Console.Error.WriteLine(
                "[lemon][error] SceneManager.LoadSceneAsync：LoadSceneMode.Additive 未实现" +
                "（ADR-017 D2 预留）");
            return default;
        }
        var op = new AsyncSceneLoad(Native.SceneLoadAsyncRequest(nameOrPath, (byte)mode));
        if (op.isValid) {
            RetireInFlight(); // 上一在途 op 已被引擎取代（单槽）——注册表代收清退
            s_inFlight = op.OpId;
        } else if (!string.IsNullOrEmpty(nameOrPath)) {
            Console.Error.WriteLine(
                "[lemon][error] SceneManager.LoadSceneAsync：换场请求未被受理（场景不可解析或宿主未注册场景源）'"
                + nameOrPath + "'");
        }
        return op;
    }

    // ---- 查询 ---------------------------------------------------------------

    public static int sceneCount => (int)Native.SceneCount();

    public static Scene GetActiveScene() => ByHandle(Native.ActiveSceneHandle());

    /// <summary>按装载序取场景（index ∈ [0, sceneCount)；越界 = 无效 Scene）。</summary>
    public static Scene GetSceneAt(int index)
    {
        if (index < 0 || !Native.SceneInfoAt((uint)index, out SceneInfoC info))
            return default;
        return FromInfo(in info);
    }

    public static Scene GetSceneByName(string name)
    {
        for (int i = 0; i < sceneCount; i++) {
            Scene s = GetSceneAt(i);
            if (s.isValid && s.name == name) return s;
        }
        return default; // 未装载/不存在 = 无效（Unity 同款：不返回 unloaded 档案）
    }

    public static Scene GetSceneByPath(string path)
    {
        for (int i = 0; i < sceneCount; i++) {
            Scene s = GetSceneAt(i);
            if (s.isValid && s.path == path) return s;
        }
        return default;
    }

    /// <summary>v1：仅当前活动场景合法（Single 唯一装载）——他值 = 引擎 WARN + 拒绝；
    /// Additive 落地后放开（Instantiate 落点随 active，ADR-017）。</summary>
    public static void SetActiveScene(Scene scene)
    {
        if (!scene.isValid) return;
        Native.SetActiveScene((uint)scene.Handle);
    }

    internal static Scene ByHandle(uint handle)
    {
        if (handle == 0 || !Native.SceneInfoByHandle(handle, out SceneInfoC info))
            return default;
        return FromInfo(in info);
    }

    private static Scene FromInfo(in SceneInfoC info)
    {
        if (!s_names.TryGetValue(info.Handle, out var np)) {
            np = (NameOf(in info), PathOf(in info));
            s_names[info.Handle] = np;
        }
        return new Scene((int)info.Handle, np.name, np.path, info.IsLoaded != 0,
                         (int)info.RootCount);
    }

    private static unsafe string NameOf(in SceneInfoC info)
    {
        fixed (byte* p = info.Name) return FixedUtf8(p, 64);
    }

    private static unsafe string PathOf(in SceneInfoC info)
    {
        fixed (byte* p = info.Path) return FixedUtf8(p, 256);
    }

    private static unsafe string FixedUtf8(byte* src, int cap)
    {
        int len = 0;
        while (len < cap && src[len] != 0) len++;
        return len == 0 ? "" : System.Text.Encoding.UTF8.GetString(src, len);
    }

    // ---- 事件桥（Lemon.Entry lemon_scene_event 同步直推；批⑦ D3）------------

    internal static unsafe void OnNativeSceneEvent(byte kind, uint oldHandle, uint newHandle,
                                                   byte mode)
    {
        // 异常已拦（保进程；Exports.cs 侧还有第二道 try）。注意：隔离粒度 = 整个
        // dispatch 而非逐订阅者——单订阅者异常会中断同事件后续订阅者（与 Unity
        // 行为一致；换场低频，不做 GetInvocationList 逐订阅者隔离的分配面）
        try {
            switch (kind) {
                case 0: // sceneUnloaded：载荷 = old（推送时旧档案尚未翻 isLoaded）
                    sceneUnloaded?.Invoke(ByHandle(oldHandle));
                    break;
                case 1: // sceneLoaded：载荷 = new + mode
                    sceneLoaded?.Invoke(ByHandle(newHandle), (LoadSceneMode)mode);
                    break;
                case 2: // activeSceneChanged：载荷 = old → new
                    activeSceneChanged?.Invoke(ByHandle(oldHandle), ByHandle(newHandle));
                    break;
                case 3: // AsyncSceneLoad.completed（批⑧）：载荷复用 = oldHandle 侧带
                    // opId、newHandle = 新场景句柄（0 = 失败终态——世界不动）
                    FireCompleted(oldHandle);
                    break;
            }
        } catch (Exception e) {
            Console.Error.WriteLine("[lemon][error] scene event handler " + kind + ": " +
                                    e.Message);
        }
    }

    // ---- 批⑧ async 注册表（AsyncSceneLoad 事件/门镜像/await 续跑的集中态）------

    internal static bool AllowMirrorOf(uint opId) =>
        opId != 0 && s_allowMirror.TryGetValue(opId, out bool v) ? v : true;

    internal static void MirrorAllow(uint opId, bool value)
    {
        if (opId != 0) s_allowMirror[opId] = value;
    }

    internal static void SubscribeCompleted(uint opId, Action<AsyncSceneLoad>? handler,
                                            bool invokeIfDone)
    {
        if (opId == 0 || handler == null) return;
        if (invokeIfDone && Native.SceneAsyncQuery(opId, out _, out bool done) && done) {
            handler(new AsyncSceneLoad(opId)); // 已终态 = 立即同步触发（Unity 同款）
            return;
        }
        s_completed.TryGetValue(opId, out var cur);
        s_completed[opId] = cur + handler;
    }

    internal static void UnsubscribeCompleted(uint opId, Action<AsyncSceneLoad>? handler)
    {
        if (opId == 0 || handler == null) return;
        if (!s_completed.TryGetValue(opId, out var cur)) return;
        cur -= handler;
        if (cur == null) s_completed.Remove(opId);
        else s_completed[opId] = cur;
    }

    internal static void SubscribeCompletedOnce(uint opId, Action? continuation)
    {
        if (opId == 0 || continuation == null) return;
        if (Native.SceneAsyncQuery(opId, out _, out bool done) && done) {
            continuation(); // await 时已终态 = 同步直跑（编译器模式外零等待）
            return;
        }
        s_awaitOnce[opId] = continuation; // await 恒单续跑——后订覆盖先订
    }

    /// <summary>kind3 收口（恰一次）：先 await 续跑（可 await 链继续 await 下一个
    /// 换场），再 completed 订阅族；终态注册表随推随清（重推免疫）。</summary>
    private static void FireCompleted(uint opId)
    {
        if (opId == 0) return;
        if (s_awaitOnce.Remove(opId, out var once)) once?.Invoke();
        if (s_completed.Remove(opId, out var handlers)) handlers?.Invoke(new AsyncSceneLoad(opId));
        s_allowMirror.Remove(opId);
        if (s_inFlight == opId) s_inFlight = 0;
    }

    /// <summary>清退被取代 op 的注册表项（正常终态路径 FireCompleted 已自清——
    /// 此处只兜取消路径：completed 永不推 = 订阅闭包/镜像永不释放）。</summary>
    private static void RetireInFlight()
    {
        if (s_inFlight == 0) return;
        s_completed.Remove(s_inFlight);
        s_awaitOnce.Remove(s_inFlight);
        s_allowMirror.Remove(s_inFlight);
        s_inFlight = 0;
    }

    /// <summary>进 Play 域复位（lemon_play_reset 调）：只清句柄记忆化与 async 注册
    /// 表（新 playWorld 句柄/opId 重发）；订阅表保留——静态订阅跨局存活
    ///（Events.PlayReset 同款口径）。</summary>
    internal static void PlayReset()
    {
        s_names.Clear();
        s_completed.Clear();
        s_awaitOnce.Clear();
        s_allowMirror.Clear();
        s_inFlight = 0;
    }
}
