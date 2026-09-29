using Lemon;
using Lemon.Interop;

/// <summary>HUD（M6b 批③d-1 文档化）：Assets/UI/hud.rml 六行 + 血/经双进度条，
/// UI.SetText/SetStyle 每帧一次批量提交（UI.Apply——M2 单一口）。配色/字号/
/// 间距全在 theme.rcss token（换肤 = 改 token 不改代码）；RtUi 通道（Lemon.Ui）
/// 本屏退役——RmlUi 文档即编辑器 GameView 与 M8 打包的同一呈现者。读
/// GameMain.Run 共享态；死亡相位冻结末帧（结算标题由战斗侧写入卡片屏）。</summary>
public sealed class PlayerHud : LemonBehaviour
{
    private const string kDoc = "Assets/UI/hud.rml";

    public PlayerHud()
    {
        Subscribe(GameEvent.WaveStart, m => {
            UI.SetText(kDoc, "wave", $"—— 第 {(int)m.P0 + 1} 波 ——");
            UI.Apply();
        });
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        UI.SetText(kDoc, "hp-text", $"HP {(int)hp.Cur}/{(int)hp.Max}");
        UI.SetStyle(kDoc, "hp-fill", "width", Pct(hp.Max > 0f ? hp.Cur / hp.Max : 0f));
        UI.SetText(kDoc, "xp-text", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}");
        UI.SetStyle(kDoc, "xp-fill", "width", Pct(xp.XpToNext > 0f ? xp.Xp / xp.XpToNext : 0f));
        int t = (int)GameMain.Run.Time;
        UI.SetText(kDoc, "time", $"{t / 60}:{t % 60:00}");
        UI.SetText(kDoc, "kills", $"击杀 {GameMain.Run.Kills}");
        UI.SetText(kDoc, "best", $"最高纪录 {GameMain.Run.Best}");
        UI.Apply();
    }

    /// 进度条填充宽（"0%".."100%"——自定义格式无千分位/空格，RCSS 直接可吃）。
    private static string Pct(float frac) =>
        System.Math.Clamp(frac, 0f, 1f).ToString("0%", System.Globalization.CultureInfo.InvariantCulture);
}
