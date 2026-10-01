using System.Collections.Generic;
using Lemon;

public static class GameMain
{
    /// <summary>一局共享态（M6a 批⓪ T4 三拆）：战斗写（计时/击杀/纪录/死亡），
    /// HUD/移动读。静态随 A 线换域重建、不经 StateBag 通道——迁移由 PlayerCombat
    /// 的 OnHotReloadOut/In 代收代还。</summary>
    public static class Run
    {
        public static float Time;  // 本局秒（死亡冻结）
        public static int Kills;   // 本局击杀
        public static int Best;    // 历史最高（Save "vs.best" @ Chan.Meta 持久）
        public static bool Dead;   // 死亡结算相位（三脚本共用的闸）
        public static bool ReviveUsed; // 批③d-2：复活已用（死亡策略游戏侧示例——每局一次；道具化/表驱动只动 PlayerCombat.Die 分叉）
    }

    /// <summary>设置态（批③d-2 D3：Settings 档持久化 version=1 + fx.text/fx.bar；
    /// 批④ 音量四路 vol.*。GameFlow 载入/写回，PlayerCombat.OnHit 消费门控）。
    /// 静态随域重建——GameFlow 热重载代收代还。</summary>
    public static class Settings
    {
        public static bool FxText = true; // 伤害飘字
        public static bool FxBar = true;  // 世界血条
        // M6c 批④：音量四路（0..1；默认 0.8 = 滑条 value 80。引擎应用 =
        // Audio.MasterVolume/SetGroupVolume，进 Play 装载后 + 滑条 Change 即时）
        public static float MasterVol = 0.8f;
        public static float BgmVol = 0.8f;
        public static float SfxVol = 0.8f;
        public static float UiVol = 0.8f;
    }

    /// <summary>UI 文档名（批③d-1 cards + 批③d-2 流程四屏）。UI 资产建后
    /// 不挪不改名（relPath 寻址约定）。</summary>
    internal const string CardsDoc = "Assets/UI/cards.rml";
    internal const string MainDoc = "Assets/UI/main.rml";
    internal const string PauseDoc = "Assets/UI/pause.rml";
    internal const string SettingsDoc = "Assets/UI/settings.rml";
    internal const string ResultsDoc = "Assets/UI/results.rml";

    // M6c 批④：UI 组按钮音（Assets/Audio/ui-click.ogg——全体 Click 统一打点，
    // 暂停中仍可响 = Ui 组不挂起语义的消费实证）
    private const string kSfxUi = "7e57400000000007";

    // ---- 卡片屏文档态（静态：Configure 订阅不持实例；PlayerCombat 写/消费）----
    internal static string? CardPickPending; // 待选条目 key（"cards/<id>"；读后即清 = 消费式）
    internal static string CardTitle = "";
    internal static List<UiItem>? CardItems; // 最近一次条目集（热重载重灌用）
    internal static bool CardsShown;

    public static void Configure()
    {
        // 注册序 = 跨类型 Update 执行序（04 §3.2）：流程 → 移动 → 战斗 → HUD
        //（批③d-2：GameFlow 首个——状态闸先于玩法 tick）
        Lemon.Behaviours.Register<GameFlow>();
        Lemon.Behaviours.Register<PlayerMovement>();
        Lemon.Behaviours.Register<PlayerCombat>();
        Lemon.Behaviours.Register<PlayerHud>();
        // 档② 清场批量系统（批③d-2：GameFlow.EnterRun/ReturnToMenu 消费）
        Lemon.Scripting.Register(new RunSweeper());
        // 批③d-1：UI 事件静态订阅（Configure 每域一次，跨局存活——③c 先例）。
        // Click(pick) → 待选 key（PlayerCombat.Update 消费式读取）；DocumentReloaded
        // → shown 态重灌（M2 契约——隐藏态不重放，防凭空亮屏）。批③d-2 起流程
        // 四屏事件（start/resume/settings/...）一并路由 GameFlow
        Lemon.UI.Events.Subscribe(OnUiEvent);
    }

    private static void OnUiEvent(Lemon.UiEvent e)
    {
        if (e.Kind == (byte)Lemon.UiEventKind.DocumentReloaded) {
            if (e.DocStr == CardsDoc && CardsShown) ReplayCards();
            else GameFlow.OnDocReloaded(e.DocStr); // 流程屏 shown 态重放 + 设置标签重灌
            return;
        }
        // M6c 批④：滑条值落定（payload = "%f" 值串；key = 滑条 id）→ 音量应用
        if (e.Kind == (byte)Lemon.UiEventKind.Change) {
            GameFlow.OnVolumeChange(e.KeyStr, e.PayloadStr);
            return;
        }
        if (e.Kind != (byte)Lemon.UiEventKind.Click) return;
        Audio.PlayOneShot(kSfxUi, 0.5f, AudioGroup.Ui); // 批④：UI 组按钮音全体打点
        if (e.DocStr == CardsDoc) {
            if (e.EvStr == "pick") CardPickPending = e.KeyStr;
        } else {
            GameFlow.HandleUiEvent(e); // 流程四屏（main/pause/settings/results）
        }
    }

    /// <summary>显示卡片屏（模态：模拟已 Time.Scale=0 冻结，M7 游戏侧让出；
    /// 层序 = 最近 Show 序自然压 HUD）。</summary>
    internal static void ShowCardsDoc(string title, List<UiItem> items)
    {
        CardTitle = title;
        CardItems = items;
        CardsShown = true;
        UI.Show(CardsDoc, modal: true); // Show 先行——同批后续 op 可达（③c 顺序契约）
        UI.SetText(CardsDoc, "cards-title", title);
        UI.SetItems(CardsDoc, "cards", "card", items);
        UI.Apply();
    }

    internal static void HideCardsDoc()
    {
        CardsShown = false;
        UI.Hide(CardsDoc);
        UI.Apply();
    }

    private static void ReplayCards() // 热重载重灌（title + 条目；shown 态保持）
    {
        UI.SetText(CardsDoc, "cards-title", CardTitle);
        if (CardItems != null) UI.SetItems(CardsDoc, "cards", "card", CardItems);
        UI.Apply();
    }
}
