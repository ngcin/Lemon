using Lemon;

public static class GameMain
{
    /// <summary>设置态（批③d-2 D3：Settings 档持久化 version=1 + fx.text/fx.bar；
    /// GameFlow 载入/写回，PlayerBehaviour.OnHit 消费门控）。</summary>
    public static class Settings
    {
        public static bool FxText = true; // 伤害飘字
        public static bool FxBar = true;  // 世界血条
    }

    /// <summary>流程屏文档名（批③d-2；Assets/UI/ 下四屏 + theme，与用户既有
    /// main.rml（UiTest 观测面）不冲突）。UI 资产建后不挪不改名。</summary>
    internal const string MainDoc = "Assets/UI/menu.rml";
    internal const string PauseDoc = "Assets/UI/pause.rml";
    internal const string SettingsDoc = "Assets/UI/settings.rml";
    internal const string ResultsDoc = "Assets/UI/results.rml";

    public static void Configure()
    {
        // 注册序 = 跨类型 Update 执行序：流程闸最先（批③d-2 GameFlow/RunSweeper）
        Lemon.Behaviours.Register<GameFlow>();
        Lemon.Scripting.Register(new RunSweeper());
        // UI 事件单点订阅 → GameFlow 路由（流程四屏 start/resume/settings/...）
        Lemon.UI.Events.Subscribe(OnUiEvent);
        Lemon.Behaviours.Register<PlayerBehaviour>();
        Lemon.Behaviours.Register<AllyBehaviour>();
        Lemon.Behaviours.Register<RedVsBlue>();
        Lemon.Behaviours.Register<WaveTableLoader>(); // 波次表载入（M6a 批② 范例）
        Lemon.Behaviours.Register<DuelBehaviour>(); // T3d 终验：ani.scene 双怪对决（状态机全链）
        Lemon.Behaviours.Register<UiEcho>(); // UiTest.scene：UI 事件回显（③c 真人验收观测面）
        Lemon.Behaviours.Register<TweenDemo>(); // ani.scene：宝石 Tween.Scale OutBack 弹跳演示
    }

    private static void OnUiEvent(Lemon.UiEvent e)
    {
        if (e.Kind == (byte)Lemon.UiEventKind.DocumentReloaded) {
            GameFlow.OnDocReloaded(e.DocStr);
            return;
        }
        if (e.Kind == (byte)Lemon.UiEventKind.Click) GameFlow.HandleUiEvent(e);
        // 其余事件（UiEcho 的 Change/Submit 等）由 UiEcho 自身订阅面消费
    }
}
