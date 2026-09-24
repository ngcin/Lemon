using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>战斗（M6a 批⓪ T4 拆分）：击杀/宝石掉落 + 环绕刃自愈 + 升级三选一
///（固定序轮换，零 RNG）+ 死亡结算/复活（死亡对话框点击）。一局共享态写
/// GameMain.Run（HUD 读）。</summary>
public sealed class PlayerCombat : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    private const string kBladePrefab = "7e57100000000006";
    private const uint kPiercePrefabLow = 0x00000004; // PierceBullet.prefab 低 32 位

    private static readonly string[] kOptions = {
        "移速 +10%", "磁力 +25%", "射速 +15%", "穿透弹", "生命上限 +25", "环绕之刃 +1",
    };

    private int _pendingLevels; // LevelUp 事件累计的待选次数
    private bool _cardsShown;
    private int _pickRotation;  // 三选一轮换序（确定性）
    private int _bladeCount = 2;
    private readonly List<ulong> _blades = new();
    private float _bladeAngle;

    public PlayerCombat()
    {
        // Subscribe 助手（M15）：实例销毁自动退订（裸 Events.Subscribe 只增不删，
        // 死亡→复活重挂会逐局累积订阅）
        Subscribe(GameEvent.LevelUp, m => {
            if (m.Src.Id == gameObject.Entity.Id) ++_pendingLevels;
        });
        Subscribe(GameEvent.Death, OnDeath);
    }

    private void OnDeath(GameEventMsg m)
    {
        var src = GameObject.From(m.Src);
        if (!src.Alive || !src.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) {
            ++GameMain.Run.Kills;
            if (src.TryGetComponent<Transform2D>(out var tf)) // 两阶段销毁：当帧可读
                Instantiate.Prefab(kGemPrefab, new Vec2(tf.Pos.X, tf.Pos.Y));
        } else if (m.Src.Id == gameObject.Entity.Id) {
            Die();
        }
    }

    protected override void Start()
    {
        GameMain.Run.Best =
            int.TryParse(Save.GetString("vs.best"), out var b) ? b : 0; // 上一局纪录
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) {
            // 死亡对话框：点击/数字键 1 → CardPick()==0 复活（消费式回读，与升级
            // 卡片同通道；批④后修④——R 键路径废弃，交互不依赖键盘焦点路由）
            if (Ui.CardPick() == 0) Revive();
            return;
        }
        GameMain.Run.Time += Time.DeltaTime;

        UpdateBlades(gameObject.GetComponent<Transform2D>());
        UpdateCards();
    }

    private void UpdateBlades(Transform2D playerTf)
    {
        _bladeAngle += 2.2f * Time.DeltaTime;
        while (_blades.Count < _bladeCount) { // 自愈：热重装/丢失即补（挂玩家当前位置）
            var g = Instantiate.Prefab(kBladePrefab,
                                       new Vec2(playerTf.Pos.X, playerTf.Pos.Y));
            _blades.Add(g.Entity.Id);
        }
        _blades.RemoveAll(id => !GameObject.From(new EntityHandle { Id = id }).Alive);
        for (int i = 0; i < _blades.Count; ++i) {
            var b = GameObject.From(new EntityHandle { Id = _blades[i] });
            if (!b.TryGetComponent<Transform2D>(out var bt)) continue;
            float a = _bladeAngle + i * (6.2831853f / _blades.Count);
            bt.Pos = new Vec2(playerTf.Pos.X + 90f * System.MathF.Cos(a),
                              playerTf.Pos.Y + 90f * System.MathF.Sin(a));
            bt.Rot = a;
            b.SetComponent(bt);
        }
    }

    private void UpdateCards()
    {
        if (_cardsShown) {
            int pick = Ui.CardPick();
            if (pick < 0) return;
            int n = _pickRotation - 1; // ShowCards 时已自增
            ApplyOption((n + pick * 2) % 6);
            --_pendingLevels;
            _cardsShown = false;
            Ui.HideCards();
            if (_pendingLevels <= 0) Time.Scale = 1f; // 选完恢复（多级连选继续冻结）
            return;
        }
        if (_pendingLevels > 0) {
            Time.Scale = 0f; // 卡片期间冻结（RNG 不消耗，批① D5 语义）
            int n = _pickRotation++;
            Ui.ShowCards("升级！三选一", kOptions[n % 6], kOptions[(n + 2) % 6],
                         kOptions[(n + 4) % 6]);
            _cardsShown = true;
        }
    }

    private void ApplyOption(int o)
    {
        switch (o) {
        case 0: { // 移速
            var st = gameObject.GetComponent<Stats>();
            st.MoveSpeed *= 1.10f;
            gameObject.SetComponent(st);
            break;
        }
        case 1: { // 磁力
            var st = gameObject.GetComponent<Stats>();
            st.PickupRadius *= 1.25f;
            gameObject.SetComponent(st);
            break;
        }
        case 2: { // 射速
            var sh = gameObject.GetComponent<Shooter>();
            sh.Interval = System.Math.Max(0.05f, sh.Interval * 0.85f);
            gameObject.SetComponent(sh);
            break;
        }
        case 3: { // 穿透弹（切弹种）
            var sh = gameObject.GetComponent<Shooter>();
            sh.ProjectileId = kPiercePrefabLow;
            gameObject.SetComponent(sh);
            break;
        }
        case 4: { // 生命上限
            var hp = gameObject.GetComponent<Health>();
            hp.Max += 25f;
            hp.Cur += 25f;
            gameObject.SetComponent(hp);
            break;
        }
        case 5: ++_bladeCount; break; // 环绕 +1（UpdateBlades 自愈补挂）
        }
    }

    private void Die()
    {
        if (GameMain.Run.Dead) return; // 多源 Death（弹道/区域）只结算一次
        GameMain.Run.Dead = true;
        _cardsShown = false; // 弃置在途升级卡（_pendingLevels 保留，复活后重弹）
        Time.Scale = 0f;
        int score = GameMain.Run.Kills * 10 + (int)GameMain.Run.Time;
        bool newBest = score > GameMain.Run.Best;
        if (newBest) {
            GameMain.Run.Best = score;
            Save.SetString("vs.best", score.ToString());
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径）
        }
        string title = newBest ? $"★ 新纪录 {score} 分！"
                               : $"本局 {score} 分（最高 {GameMain.Run.Best}）";
        Ui.Set("over", title, -1f, 0xFF5080FFu);
        Ui.ShowDialog(title, "复活");
    }

    private void Revive()
    {
        GameMain.Run.Dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        Ui.HideCards();
        Ui.Clear("over");
    }

    // 热重载状态迁移（数值面，含 GameMain.Run 共享态——静态随域重建必须经包走；
    // 刃实体经 UpdateBlades 自愈重建轨道）
    protected override void OnHotReloadOut(StateBag bag)
    {
        bag.Set("time", GameMain.Run.Time);
        bag.Set("kills", GameMain.Run.Kills);
        bag.Set("best", GameMain.Run.Best);
        bag.Set("dead", GameMain.Run.Dead);
        bag.Set("pending", _pendingLevels);
        bag.Set("rotation", _pickRotation);
        bag.Set("blades", _bladeCount);
    }

    protected override void OnHotReloadIn(StateBag bag)
    {
        if (bag.TryGet("time", out float t)) GameMain.Run.Time = t;
        if (bag.TryGet("kills", out int k)) GameMain.Run.Kills = k;
        if (bag.TryGet("best", out int b)) GameMain.Run.Best = b;
        if (bag.TryGet("dead", out bool d)) GameMain.Run.Dead = d;
        if (bag.TryGet("pending", out int p)) _pendingLevels = p;
        if (bag.TryGet("rotation", out int r)) _pickRotation = r;
        if (bag.TryGet("blades", out int n)) _bladeCount = n;
    }
}
