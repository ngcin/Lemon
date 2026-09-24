using Lemon;
using Lemon.Interop;

/// <summary>HUD（M6a 批⓪ T4 拆分）：四要素行 + 波次横幅。读 GameMain.Run 共享态；
/// 死亡相位冻结末帧（"over" 结算行由战斗侧写）。</summary>
public sealed class PlayerHud : LemonBehaviour
{
    private const uint kColorHp = 0xFF30B0F0u;   // 血条红（ABGR）
    private const uint kColorXp = 0xFF30D8F0u;   // 经验金
    private const uint kColorTime = 0xFFF0F0F0u; // 计时白
    private const uint kColorKill = 0xFF4098F0u; // 击杀橙
    private const uint kColorWave = 0xFF60E0A0u; // 波次绿

    public PlayerHud()
    {
        Subscribe(GameEvent.WaveStart, m =>
            Ui.Set("wave", $"—— 第 {(int)m.P0 + 1} 波 ——", -1f, kColorWave));
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        Ui.Set("hp", $"HP {(int)hp.Cur}/{(int)hp.Max}",
               hp.Max > 0f ? hp.Cur / hp.Max : 0f, kColorHp);
        Ui.Set("xp", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}",
               xp.XpToNext > 0f ? xp.Xp / xp.XpToNext : 0f, kColorXp);
        int t = (int)GameMain.Run.Time;
        Ui.Set("time", $"{t / 60}:{t % 60:00}", -1f, kColorTime);
        Ui.Set("kills", $"击杀 {GameMain.Run.Kills}", -1f, kColorKill);
        Ui.Set("best", $"最高纪录 {GameMain.Run.Best}", -1f);
    }
}
