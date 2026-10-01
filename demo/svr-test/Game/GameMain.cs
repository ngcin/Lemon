using Lemon;

public static class GameMain
{
    /// <summary>设置态（批③d-2 D3：Settings 档持久化 version=1 + fx.text/fx.bar；
    /// M6c 批④ 音量四路 vol.*。GameFlow 载入/写回，PlayerBehaviour.OnHit 消费门控）。</summary>
    public static class Settings
    {
        public static bool FxText = true; // 伤害飘字
        public static bool FxBar = true;  // 世界血条
        // M6c 批④：音量四路（0..1；默认 0.8 = 滑条 value 80）
        public static float MasterVol = 0.8f;
        public static float BgmVol = 0.8f;
        public static float SfxVol = 0.8f;
        public static float UiVol = 0.8f;
    }

    // M6c 批④：UI 组按钮音（Assets/Audio/ui-click.ogg = Kenney CC0 click3；
    // 全体 Click 统一打点，暂停中仍可响 = Ui 组不挂起语义的消费实证）
    private const string kSfxUi = "6a6d100000000009";

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
        // M6c 批④：音量滑条值落定（key = 滑条 id；非 vol-* 键忽略，不吞 UiEcho 面）
        if (e.Kind == (byte)Lemon.UiEventKind.Change) {
            GameFlow.OnVolumeChange(e.KeyStr, e.PayloadStr);
            return;
        }
        if (e.Kind == (byte)Lemon.UiEventKind.Click) {
            Audio.PlayOneShot(kSfxUi, 0.5f, AudioGroup.Ui); // 批④：UI 组按钮音
            GameFlow.HandleUiEvent(e);
        }
        // 其余事件（UiEcho 的 Submit 等）由 UiEcho 自身订阅面消费
    }
}
