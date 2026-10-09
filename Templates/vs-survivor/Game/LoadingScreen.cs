// 加载屏样例（M7c 批⑧；ADR-017 D3"游戏侧重初始化归游戏侧"的模板侧兑现）。
// 形态（Unity 加载屏同构 + Lemon 特有面）：
//   * 文档 = code-mounted（UI.Show 通道 B 现载 → origin=CSharp 跨场幸存——换场
//     sweep 只卸 origin=Scene；无需在场景里声明 UIDocument）；
//   * 驱动 = LoadingScreenDriver（LemonBehaviour）挂在 Begin() 即时生成的实体上，
//     Awake 自标 DontDestroyOnLoad → 跨场幸存轮询进度；完成即自毁；
//   * 进度 = 引擎段 × 0.8 + 游戏自报段 × 0.2 权重混合（ADR D3 原文口径）。
// 两形态：
//   ① 直通（默认）：LoadingScreen.Begin("Grass") —— 门开，装载完成即激活；
//   ② 门控：LoadingScreen.Begin("Grass", holdGate: true) —— 引擎段停 0.9 等
//     游戏段就绪（配置/存档回读等前置完成后 ReportGameProgress(1f)）→ 驱动自动
//     开门激活。注意：sceneLoaded 在激活时才发——激活后的重初始化（铺 NPC 等）
//     请订阅 sceneLoaded 后自行分帧（每帧 N 个，防激活帧一次性 spawn 卡帧）。
// 契约提醒（04 分册响亮规则）：progress 是纯呈现量（分帧预算依赖、跨机器不确定）
// ——玩法分支只许挂 sceneLoaded/isDone/completed，禁挂 progress 分支。
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

    /// <summary>发起异步换场 + 挂加载屏。返回 op 供调用方轮询/订阅 completed。
    /// op 无效（场景不可解析/Additive 红字）= 不挂屏原样返回。重复 Begin = 新请求
    /// 取代（引擎单槽）+ 旧驱动代收（被取代 op 永不 isDone，孤儿驱动会常驻 tick）。</summary>
    public static AsyncSceneLoad Begin(string scene, bool holdGate = false)
    {
        var op = SceneManager.LoadSceneAsync(scene);
        if (!op.isValid) return op;
        if (s_driver.Alive) s_driver.Destroy(); // 上一驱动代收（其 op 已被取代）
        op.allowSceneActivation = !holdGate;
        Pending = op;
        HoldingGate = holdGate;
        GameProgress = 0f;
        UI.Show(Doc, modal: true);
        s_driver = Instantiate.Spawn<LoadingScreenDriver>(0, new Vec2(0, 0));
        return op;
    }

    /// <summary>游戏段进度自报（0..1，钳制）。门控形态：引擎段到 0.9 且自报满 1
    /// → 驱动开门（激活帧 = 下一 Essential）；直通形态只参与显示权重。</summary>
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
        UI.SetText(LoadingScreen.Doc, "load-hint",
                   LoadingScreen.HoldingGate ? "准备中…" : "装载中…");
        // 门控形态：引擎段 0.9 + 游戏段满 → 开门（激活帧 = 下一 Essential）
        if (LoadingScreen.HoldingGate && _op.progress >= 0.9f &&
            LoadingScreen.GameProgress >= 1f) {
            _op.allowSceneActivation = true;
            LoadingScreen.HoldingGate = false;
        }
        if (_op.isDone) {
            UI.Hide(LoadingScreen.Doc);
            LoadingScreen.Pending = default;
            gameObject.Destroy(); // 驱动自清（DDOL 实体不随场清——必须显式）
        }
    }
}
