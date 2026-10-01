using Lemon;
using Lemon.Interop;

/// <summary>HUD（M6b 批③d-1 文档化；T8 后修② 条改原生 progress）：Assets/UI/
/// hud.rml 六行 + 血/经双进度条，UI.SetText/SetAttr 每帧一次批量提交（UI.Apply
/// ——M2 单一口）。进度条 = RmlUi 原生 &lt;progress&gt;：value/max 走属性通道（语义
/// 数据非样式，C# 不做百分比数学）；fill 引擎定位，免布局坑。配色/字号/间距全在
/// theme.rcss token（换肤 = 改 token 不改代码）；RtUi 通道（Lemon.Ui）本屏退役
/// ——RmlUi 文档即编辑器 GameView 与 M8 打包的同一呈现者。读 GameMain.Run 共享态；
/// 死亡相位冻结末帧（结算标题由战斗侧写入卡片屏）。</summary>
public sealed class PlayerHud : LemonBehaviour
{
    private const string kDoc = "Assets/UI/hud.rml";
    // M6c 批④：波次横幅音（Assets/Audio/wave.ogg）
    private const string kSfxWave = "7e57400000000006";

    public PlayerHud()
    {
        Subscribe(GameEvent.WaveStart, m => {
            UI.SetText(kDoc, "wave", $"—— 第 {(int)m.P0 + 1} 波 ——");
            Audio.PlayOneShot(kSfxWave, 0.6f); // M6c 批④：波次横幅音
            UI.Apply();
        });
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        UI.SetText(kDoc, "hp-text", $"HP {(int)hp.Cur}/{(int)hp.Max}");
        Bar(kDoc, "hp-bar", hp.Cur, hp.Max);
        UI.SetText(kDoc, "xp-text", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}");
        Bar(kDoc, "xp-bar", xp.Xp, xp.XpToNext);
        int t = (int)GameMain.Run.Time;
        UI.SetText(kDoc, "time", $"{t / 60}:{t % 60:00}");
        UI.SetText(kDoc, "kills", $"击杀 {GameMain.Run.Kills}");
        UI.SetText(kDoc, "best", $"最高纪录 {GameMain.Run.Best}");
        UI.Apply();
    }

    /// 原生 progress 驱动（value/max 属性对；max 每帧同写——升级换挡零特判）。
    /// (int) 舍入与文本行同口径（"0" 格式会四舍五入 → 两行数字不一致）。
    private static void Bar(string doc, string id, float cur, float max) {
        UI.SetAttr(doc, id, "value", ((int)cur).ToString(IC));
        UI.SetAttr(doc, id, "max", ((int)max).ToString(IC));
    }
    private static readonly System.Globalization.CultureInfo IC =
        System.Globalization.CultureInfo.InvariantCulture;
}
