using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>vs-survivor 模板玩家（M5 批④）：8 向移动 + HUD 四要素 + 升级三选一
/// （固定序轮换，零 RNG）+ 环绕刃自愈 + 击杀掉宝石 + 死亡结算/复活（R 键）。
/// 单类挂玩家实体（多脚本 scripts[] 属 M5 余项，见 M5.md §21.2 D4 注）。</summary>
public sealed class PlayerBehaviour : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    private const string kBladePrefab = "7e57100000000006";
    private const uint kPiercePrefabLow = 0x00000004; // PierceBullet.prefab 低 32 位

    private const float kArenaHalf = 1000f; // 软竞技场边界（脚本层钳制）
    private const uint kColorHp = 0xFF30B0F0u;   // 血条红（ABGR）
    private const uint kColorXp = 0xFF30D8F0u;   // 经验金
    private const uint kColorTime = 0xFFF0F0F0u; // 计时白
    private const uint kColorKill = 0xFF4098F0u; // 击杀橙
    private const uint kColorWave = 0xFF60E0A0u; // 波次绿

    private static readonly string[] kOptions = {
        "移速 +10%", "磁力 +25%", "射速 +15%", "穿透弹", "生命上限 +25", "环绕之刃 +1",
    };

    private float _runTime;
    private int _kills;
    private int _best;
    private bool _dead;
    private int _pendingLevels; // LevelUp 事件累计的待选次数
    private bool _cardsShown;
    private int _pickRotation;  // 三选一轮换序（确定性）
    private int _bladeCount = 2;
    private readonly List<ulong> _blades = new();
    private float _bladeAngle;

    public PlayerBehaviour()
    {
        // Subscribe 助手（M15）：实例销毁自动退订（裸 Events.Subscribe 只增不删，
        // 死亡→复活重挂会逐局累积订阅）
        Subscribe(GameEvent.LevelUp, m => {
            if (m.Src.Id == gameObject.Entity.Id) ++_pendingLevels;
        });
        Subscribe(GameEvent.Death, OnDeath);
        Subscribe(GameEvent.WaveStart, m =>
            Ui.Set("wave", $"—— 第 {(int)m.P0 + 1} 波 ——", -1f, kColorWave));
    }

    private void OnDeath(GameEventMsg m)
    {
        var src = GameObject.From(m.Src);
        if (!src.Alive || !src.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) {
            ++_kills;
            if (src.TryGetComponent<Transform2D>(out var tf)) // 两阶段销毁：当帧可读
                Instantiate.Prefab(kGemPrefab, new Vec2(tf.Pos.X, tf.Pos.Y));
        } else if (m.Src.Id == gameObject.Entity.Id) {
            Die();
        }
    }

    protected override void Start()
    {
        _best = int.TryParse(Save.GetString("vs.best"), out var b) ? b : 0; // 上一局纪录
    }

    protected override void Update()
    {
        if (_dead) {
            if (Input.Confirm) Revive(); // R 键复活（继续厮杀，纪录不清）
            else return;
        }
        _runTime += Time.DeltaTime;

        var tf = gameObject.GetComponent<Transform2D>();
        var stats = gameObject.GetComponent<Stats>();
        Vec2 axis = Input.Axis;
        tf.Pos = new Vec2(
            System.Math.Clamp(tf.Pos.X + axis.X * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf),
            System.Math.Clamp(tf.Pos.Y + axis.Y * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf));
        gameObject.SetComponent(tf);

        UpdateHud();
        UpdateBlades(tf);
        UpdateCards();
    }

    private void UpdateHud()
    {
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        Ui.Set("hp", $"HP {(int)hp.Cur}/{(int)hp.Max}",
               hp.Max > 0f ? hp.Cur / hp.Max : 0f, kColorHp);
        Ui.Set("xp", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}",
               xp.XpToNext > 0f ? xp.Xp / xp.XpToNext : 0f, kColorXp);
        int t = (int)_runTime;
        Ui.Set("time", $"{t / 60}:{t % 60:00}", -1f, kColorTime);
        Ui.Set("kills", $"击杀 {_kills}", -1f, kColorKill);
        Ui.Set("best", $"最高纪录 {_best}", -1f);
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
        _dead = true;
        Time.Scale = 0f;
        int score = _kills * 10 + (int)_runTime;
        bool newBest = score > _best;
        if (newBest) {
            _best = score;
            Save.SetString("vs.best", score.ToString());
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径）
        }
        Ui.Set("over", newBest ? $"★ 新纪录 {score} 分！按 R 复活"
                               : $"本局 {score} 分（最高 {_best}）  按 R 复活",
               -1f, 0xFF5080FFu);
    }

    private void Revive()
    {
        _dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        Ui.Clear("over");
    }

    // 热重载状态迁移（数值面；刃实体经 UpdateBlades 自愈重建轨道）
    protected override void OnHotReloadOut(StateBag bag)
    {
        bag.Set("time", _runTime);
        bag.Set("kills", _kills);
        bag.Set("best", _best);
        bag.Set("dead", _dead);
        bag.Set("pending", _pendingLevels);
        bag.Set("rotation", _pickRotation);
        bag.Set("blades", _bladeCount);
    }

    protected override void OnHotReloadIn(StateBag bag)
    {
        if (bag.TryGet("time", out float t)) _runTime = t;
        if (bag.TryGet("kills", out int k)) _kills = k;
        if (bag.TryGet("best", out int b)) _best = b;
        if (bag.TryGet("dead", out bool d)) _dead = d;
        if (bag.TryGet("pending", out int p)) _pendingLevels = p;
        if (bag.TryGet("rotation", out int r)) _pickRotation = r;
        if (bag.TryGet("blades", out int n)) _bladeCount = n;
    }
}
