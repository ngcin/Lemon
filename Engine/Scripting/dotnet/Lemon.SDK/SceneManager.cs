// Lemon.SDK — SceneManager 门面（M7c 批⑦；ADR-017 C# API 面 / D1–D8）
// 命名逐项对齐 Unity（D8：flat Lemon.SceneManager，不建 SceneManagement 子命名空间）。
// 时序契约（换场帧协议 ⑤，D3 同步直推）：LoadScene 当帧照常跑完 → 下一帧 Essential
// 换场窗口内 sceneUnloaded → sceneLoaded（新场 Awake/OnEnable 后、同帧 Start/Update
// 前）→ activeSceneChanged——三事件都在换场帧内、早于新场脚本首帧 Update。
// v1 口径（ADR-017）：Single 唯一装载；Additive = 红字未实现（D2 预留）；
// sceneCount = isLoaded 档案数（DDOL 不建模伪场景——Unity 差异）；SetActiveScene
// 仅当前 active 合法；无 buildIndex（D4 砍单）。LoadSceneAsync 归批⑧（不在本文件）。
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

/// <summary>场景管理门面（Unity SceneManager 手感；ADR-017 §C# API 面）。</summary>
public static class SceneManager
{
    public static event Action<Scene, LoadSceneMode>? sceneLoaded;
    public static event Action<Scene>? sceneUnloaded;
    public static event Action<Scene, Scene>? activeSceneChanged;

    /// <summary>档案 name/path 记忆化（档案级不可变 + 句柄单调不复用；PlayReset
    /// 清空——编辑器重进 Play = 新 playWorld 句柄从 1 重发，不清 = 跨局撞车）。</summary>
    private static readonly Dictionary<uint, (string name, string path)> s_names = new();

    // ---- 装载 ---------------------------------------------------------------

    /// <summary>同步换场（Unity 语义：当帧照常跑完，下一帧装载——vtable 侧入队
    /// SceneSwitcher，Essential #18 执行）。寻址：项目相对路径 > 唯一文件名 stem >
    /// 红字响亮失败（D4；解析在宿主 SceneSourceHooks——未注册 = 红字拒）。</summary>
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
            }
        } catch (Exception e) {
            Console.Error.WriteLine("[lemon][error] scene event handler " + kind + ": " +
                                    e.Message);
        }
    }

    /// <summary>进 Play 域复位（lemon_play_reset 调）：只清句柄记忆化（新 playWorld
    /// 句柄重发）；订阅表保留——静态订阅跨局存活（Events.PlayReset 同款口径）。</summary>
    internal static void PlayReset() => s_names.Clear();
}
