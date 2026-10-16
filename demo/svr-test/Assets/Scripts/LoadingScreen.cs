// 加载屏（M7c 批⑨；批⑧ 模板样例的 svr-test 消费版——形态见 Templates/vs-survivor/
// Game/LoadingScreen.cs 头注：code-mounted 文档（UI.Show 通道 B 现载 →
// origin=CSharp 跨场幸存）+ DDOL 驱动实体轮询 AsyncSceneLoad.progress 完成自毁。
// svr-test 适配点：文档路径 = 全 relPath（通道 B 以 relPath 解析）。
// 契约提醒（04 分册响亮规则）：progress 是纯呈现量（分帧预算依赖、跨机器不确定）
// ——玩法分支只许挂 sceneLoaded/isDone/completed，禁挂 progress 分支。
// 大场景后初始化（sceneLoaded 后分帧铺 spawn）：见 04 分册与模板样例头注——
// svr-test 场景小（自含 ≤3 实体，批⑨ D2），本游戏暂不需要。
using Lemon;

/// <summary>加载屏入口（静态门面；驱动实体自管理自清理）。</summary>
public static class LoadingScreen
{
    public const string Doc = "Assets/UI/loading.rml";

    // Begin → 驱动 Awake 的交接槽（AttachScript 次帧生效，驱动 Awake 从此取 op）
    internal static AsyncSceneLoad Pending;
    internal static bool HoldingGate;
    internal static float GameProgress; // 游戏自报段（0..1；门控形态的开门条件）

    private static Lemon.GameObject s_driver; // 现任驱动（Begin 代收上一任——批⑧ F3）

    static LoadingScreen()
        // 跨局复位（批⑨ 真人走查补）：s_driver 静态随域存活，旧局句柄跨 World
        // id 复用会误判 Alive → 二次 Play 加载屏死驱。每局清（hook 通道见 Events）
        => Lemon.Events.PlayResetHook += () => {
               s_driver = default;
               Pending = default;
               HoldingGate = false;
               GameProgress = 0f;
           };

    /// <summary>发起异步换场 + 挂加载屏（svr-test 场景小 = 直通形态，不门控）。
    /// 返回 op 供调用方轮询/订阅 completed。op 无效（场景不可解析/Additive 红字）
    /// = 不挂屏原样返回。重复 Begin = 新请求取代（引擎单槽）+ 旧驱动代收。</summary>
    public static AsyncSceneLoad Begin(string scene)
    {
        var op = SceneManager.LoadSceneAsync(scene);
        if (!op.isValid) return op;
        if (s_driver.Alive) s_driver.Destroy(); // 上一驱动代收（其 op 已被取代）
        op.allowSceneActivation = true;
        Pending = op;
        HoldingGate = false;
        GameProgress = 0f;
        UI.Show(Doc, modal: true);
        s_driver = Instantiate.Spawn<LoadingScreenDriver>(0, new Vec2(0, 0));
        return op;
    }

    /// <summary>游戏段进度自报（0..1，钳制）。直通形态只参与显示权重（引擎段
    /// ×0.8 + 自报 ×0.2）；门控形态见模板样例（holdGate + 开门条件）。</summary>
    public static void ReportGameProgress(float f)
    {
        GameProgress = f < 0f ? 0f : f > 1f ? 1f : f;
    }
}

/// <summary>加载屏驱动（Begin 生成；Awake 自标 DDOL，完成自毁——零残留）。</summary>
public sealed class LoadingScreenDriver : LemonBehaviour
{
    private const float kEngineWeight = 0.8f, kGameWeight = 0.2f;
    private AsyncSceneLoad _op;

    protected override void Awake()
    {
        _op = LoadingScreen.Pending;
        // 跨场幸存：驱动挂在当前 active 场景组里，不标 DDOL 会在激活帧被清场
        LemonBehaviour.DontDestroyOnLoad(gameObject);
    }

    protected override void Update()
    {
        if (!_op.isValid) return; // Begin 交接失败（防御）
        // 权重混合（ADR D3：引擎段 × 权重 + 自报游戏段 × 权重）
        float total = kEngineWeight * _op.progress + kGameWeight * LoadingScreen.GameProgress;
        UI.SetAttr(LoadingScreen.Doc, "load-bar", "value", ((int)(total * 100f)).ToString());
        UI.SetText(LoadingScreen.Doc, "load-hint", "装载中…");
        if (_op.isDone) {
            UI.Hide(LoadingScreen.Doc);
            LoadingScreen.Pending = default;
            gameObject.Destroy(); // 驱动自清（DDOL 实体不随场清——必须显式）
        }
    }
}
