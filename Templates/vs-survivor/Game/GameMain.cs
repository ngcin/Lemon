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
    }

    /// <summary>卡片屏文档名（M6b 批③d-1：升级三选一/死亡对话框同一 .rml 文档，
    /// 单条形态 = 对话框）。UI 资产建后不挪不改名（relPath 寻址约定）。</summary>
    internal const string CardsDoc = "Assets/UI/cards.rml";

    // ---- 卡片屏文档态（静态：Configure 订阅不持实例；PlayerCombat 写/消费）----
    internal static string? CardPickPending; // 待选条目 key（"cards/<id>"；读后即清 = 消费式）
    internal static string CardTitle = "";
    internal static List<UiItem>? CardItems; // 最近一次条目集（热重载重灌用）
    internal static bool CardsShown;

    public static void Configure()
    {
        // 注册序 = 跨类型 Update 执行序（04 §3.2）：移动 → 战斗 → HUD
        Lemon.Behaviours.Register<PlayerMovement>();
        Lemon.Behaviours.Register<PlayerCombat>();
        Lemon.Behaviours.Register<PlayerHud>();
        // 批③d-1：UI 事件静态订阅（Configure 每域一次，跨局存活——③c 先例）。
        // Click(pick) → 待选 key（PlayerCombat.Update 消费式读取）；DocumentReloaded
        // → shown 态卡片重灌（M2 契约——隐藏态不重放，防凭空亮屏）
        Lemon.UI.Events.Subscribe(OnUiEvent);
    }

    private static void OnUiEvent(Lemon.UiEvent e)
    {
        if (e.DocStr != CardsDoc) return;
        if (e.Kind == (byte)Lemon.UiEventKind.Click && e.EvStr == "pick")
            CardPickPending = e.KeyStr;
        else if (e.Kind == (byte)Lemon.UiEventKind.DocumentReloaded && CardsShown)
            ReplayCards();
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
