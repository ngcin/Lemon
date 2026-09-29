using System;
using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>战斗（M6a 批⓪ T4 拆分）：击杀/宝石掉落 + 环绕刃自愈 + 升级三选一
///（固定序轮换，零 RNG）+ 死亡结算/复活（死亡对话框点击）+ 受击表现（批①：
/// Anim 受击段 Play+Queue 回行走 + Fx 飘字/世界血条）。一局共享态写
/// GameMain.Run（HUD 读）。
/// 批③d-1：三选一/死亡对话框迁 .rml 文档（GameMain.ShowCardsDoc——UI.Click
/// 事件消费式回读，数字键通道退役）。
/// 批② T4 数值表化（ADR-012）：升级池/武器参数读 Assets/tables/upgrades.tab +
/// weapons.tab（列头即列契约；缺表 = 空池 + warn——三选一不弹 = 与"无升级"
/// 语义一致，模板永不因表缺炸 Play）；XP 曲线系数读 balance.tab 写
/// Lemon.Balance（World 级，缺省 = 引擎默认 1.25）。</summary>
public sealed class PlayerCombat : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    // 数值表 GUID（Assets/tables/；生成期固定，PlayerCombat 读）
    private const string kWeaponsTable = "7e57200000100001";
    private const string kUpgradesTable = "7e57200000100002";
    private const string kBalanceTable = "7e57200000100003";

    // 批①受击段 clip（Anim.ClipId = GUID 低 32 位自算；Assets/monster-hit.anim）
    private static readonly uint kMobWalk = Anim.ClipId("5bd31a7c20000002");
    private static readonly uint kMobHit = Anim.ClipId("5bd31a7c20000004");

    // ---- 批② T4 表载缓存（Start 一次载入；Play 中改表下一局生效——快照语义）----
    private sealed class WeaponRow
    {
        public string Id = "", Prefab = "";
        public float Interval, Speed, Count, Radius;
    }
    private sealed class UpgradeRow
    {
        public string Id = "", Label = "", Value = "";
        public int Kind;
    }
    private readonly List<WeaponRow> _weapons = new();   // weapons.tab 行缓存
    private readonly List<UpgradeRow> _upgrades = new(); // 升级池（空 = 三选一不弹）
    private string _bladePrefab = "7e57100000000006"; // Blade.prefab（blade.prefabGuid）
    private float _bladeSpeed = 2.2f; // 环绕角速 rad/s（blade.speed；缺表 = 原硬编码值）
    private float _bladeRadius = 90f; // 环绕轨道半径 px（blade.radius）

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
        // 批①受击表现：怪受击 = 受击段（Play+Queue 播完回行走）+ 伤害飘字 + 世界
        // 血条；玩家受击 = 世界血条刷新（常显——每击续命，HUD 文字条仍是权威）
        Subscribe(GameEvent.Hit, OnHit);
    }

    private void OnHit(GameEventMsg m)
    {
        var victim = GameObject.From(m.Dst);
        if (!victim.Alive || !victim.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) { // 怪受击
            Anim.Play(victim, kMobHit, false); // 受击段立即打断
            Anim.Queue(victim, kMobWalk);      // 播完（0.1667s）自动回行走
            if (victim.TryGetComponent<Transform2D>(out var tf))
                Fx.Text(m.P0, new Vec2(tf.Pos.X - 4f, tf.Pos.Y - 10f), 0xFF5060F0u); // 暖红（RGBA）
            if (victim.TryGetComponent<Health>(out var hp))
                Fx.Bar(victim, hp.Cur / hp.Max, 0xFF30B0F0u, 24f);
        } else if (m.Dst.Id == gameObject.Entity.Id) { // 玩家受击
            if (gameObject.TryGetComponent<Health>(out var hp))
                Fx.Bar(gameObject, hp.Cur / hp.Max, 0xFF60D060u, 32f);
        }
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
        LoadTables();
        GameMain.Run.Best =
            int.TryParse(Save.GetString("vs.best", Save.Chan.Meta), out var b) ? b : 0; // 上一局纪录（跨局归 meta 档）
        // 批③d-1：跨局归位对齐——EnterPlay 大扫除已 Hide 上局 stale 卡片文档，
        // 静态标志此处同步清（待选槽清空防上局残事件复活）
        GameMain.CardsShown = false;
        GameMain.CardPickPending = null;
    }

    // ---- 批② T4 表载（ADR-012 D1 全字符串格；坏行跳过 + warn、缺表保底——
    // 数值与生成器写表前硬编码一致 = 行为等价变换）----

    private void LoadTables()
    {
        if (Table.Has(kWeaponsTable)) {
            int cId = ColOf(kWeaponsTable, "id"), cGuid = ColOf(kWeaponsTable, "prefabGuid"),
                cInt = ColOf(kWeaponsTable, "interval"), cSpd = ColOf(kWeaponsTable, "speed"),
                cCnt = ColOf(kWeaponsTable, "count"), cRad = ColOf(kWeaponsTable, "radius");
            for (int r = 1; r < Table.Rows(kWeaponsTable); ++r) {
                string? id = At(kWeaponsTable, r, cId);
                if (string.IsNullOrEmpty(id)) { WarnBadRow(kWeaponsTable, r); continue; }
                _weapons.Add(new WeaponRow {
                    Id = id!, Prefab = At(kWeaponsTable, r, cGuid) ?? "",
                    Interval = F(kWeaponsTable, r, cInt), Speed = F(kWeaponsTable, r, cSpd),
                    Count = F(kWeaponsTable, r, cCnt), Radius = F(kWeaponsTable, r, cRad),
                });
            }
            if (ById(_weapons, "blade") is { } blade) { // 环绕参数行
                if (blade.Prefab.Length == 16) _bladePrefab = blade.Prefab;
                if (blade.Speed > 0f) _bladeSpeed = blade.Speed;
                if (blade.Radius > 0f) _bladeRadius = blade.Radius;
                if (blade.Count > 0f) _bladeCount = (int)blade.Count;
            }
            if (ById(_weapons, "shoot") is { Interval: > 0f } shoot) { // 初始射速写 Shooter
                var sh = gameObject.GetComponent<Shooter>();
                sh.Interval = shoot.Interval;
                gameObject.SetComponent(sh);
            }
        } else {
            Console.Error.WriteLine("[lemon][warn] PlayerCombat：weapons.tab 缺失"
                                    + "——环绕刃用保底参数（90px / 2.2rad/s），换弹种无效");
        }
        if (Table.Has(kUpgradesTable)) {
            int cId = ColOf(kUpgradesTable, "id"), cLabel = ColOf(kUpgradesTable, "label"),
                cKind = ColOf(kUpgradesTable, "kind"), cVal = ColOf(kUpgradesTable, "value");
            for (int r = 1; r < Table.Rows(kUpgradesTable); ++r) {
                string? label = At(kUpgradesTable, r, cLabel);
                if (string.IsNullOrEmpty(label) || cKind < 0) {
                    WarnBadRow(kUpgradesTable, r);
                    continue;
                }
                _upgrades.Add(new UpgradeRow {
                    Id = At(kUpgradesTable, r, cId) ?? "", Label = label!,
                    Kind = Table.Int(kUpgradesTable, r, cKind),
                    Value = At(kUpgradesTable, r, cVal) ?? "",
                });
            }
        } else {
            Console.Error.WriteLine("[lemon][warn] PlayerCombat：upgrades.tab 缺失"
                                    + "——升级池为空（三选一不弹 = 与\"无升级\"语义一致）");
        }
        if (Table.Has(kBalanceTable)) { // XP 曲线（缺表/缺行/坏值 = 引擎默认 1.25，静默）
            int cId = ColOf(kBalanceTable, "id"), cVal = ColOf(kBalanceTable, "value");
            if (cId >= 0 && cVal >= 0)
                for (int r = 1; r < Table.Rows(kBalanceTable); ++r)
                    if (At(kBalanceTable, r, cId) == "xpCurveK") {
                        float v = Table.Float(kBalanceTable, r, cVal);
                        if (v > 0f) Balance.XpCurveK = v;
                        break;
                    }
        }
    }

    private static int ColOf(string table, string name) // 列头名 → 索引（-1 = 无此列）
    {
        for (int c = 0; c < Table.Cols(table); ++c)
            if (Table.Str(table, 0, c) == name) return c;
        return -1;
    }
    private static string? At(string table, int row, int col)
        => col >= 0 ? Table.Str(table, row, col) : null;
    private static float F(string table, int row, int col)
    {
        if (col < 0) return 0f;
        string? s = Table.Str(table, row, col);
        if (string.IsNullOrEmpty(s)) return 0f; // 空格 = 未配置（宽表留空常态），免 warn
        return Table.Float(table, row, col);
    }
    private static void WarnBadRow(string table, int row)
        => Console.Error.WriteLine($"[lemon][warn] PlayerCombat：{table} 第 {row} 行坏——跳过");
    private static WeaponRow? ById(List<WeaponRow> list, string id)
    {
        foreach (var w in list)
            if (w.Id == id) return w;
        return null;
    }
    /// 倍率容错：非正值/坏格式 = 保底原值（表值写坏不把数值清零）。
    private static float MulVal(string s, float fallback)
        => float.TryParse(s, System.Globalization.CultureInfo.InvariantCulture,
                          out var v) && v > 0f ? v : fallback;
    private static float AddVal(string s, float fallback)
        => float.TryParse(s, System.Globalization.CultureInfo.InvariantCulture,
                          out var v) && v != 0f ? v : fallback;
    /// 16 位 GUID hex → 低 32 位（引擎 prefabId 口径；WaveTableLoader 同款）。
    private static uint GuidLow32(string hex)
        => hex.Length == 16 ? (uint)Convert.ToUInt64(hex, 16) : 0u;

    protected override void Update()
    {
        if (GameMain.Run.Dead) {
            // 死亡对话框：点击"复活"卡 → UI.Click 事件（key = "cards/ok"）复活。
            // 消费式回读（读后即清）与升级卡片同通道；在途升级选择一并丢弃
            // （Die 已弃置在途卡）；数字键通道已退役（批③d-1 设计定案 5）
            string? pending = GameMain.CardPickPending;
            GameMain.CardPickPending = null;
            if (pending == "cards/ok") Revive();
            return;
        }
        GameMain.Run.Time += Time.DeltaTime;

        UpdateBlades(gameObject.GetComponent<Transform2D>());
        UpdateCards();
    }

    private void UpdateBlades(Transform2D playerTf)
    {
        _bladeAngle += _bladeSpeed * Time.DeltaTime;
        while (_blades.Count < _bladeCount) { // 自愈：热重装/丢失即补（挂玩家当前位置）
            var g = Instantiate.Prefab(_bladePrefab,
                                       new Vec2(playerTf.Pos.X, playerTf.Pos.Y));
            _blades.Add(g.Entity.Id);
        }
        _blades.RemoveAll(id => !GameObject.From(new EntityHandle { Id = id }).Alive);
        for (int i = 0; i < _blades.Count; ++i) {
            var b = GameObject.From(new EntityHandle { Id = _blades[i] });
            if (!b.TryGetComponent<Transform2D>(out var bt)) continue;
            float a = _bladeAngle + i * (6.2831853f / _blades.Count);
            bt.Pos = new Vec2(playerTf.Pos.X + _bladeRadius * System.MathF.Cos(a),
                              playerTf.Pos.Y + _bladeRadius * System.MathF.Sin(a));
            bt.Rot = a;
            b.SetComponent(bt);
        }
    }

    private void UpdateCards()
    {
        if (_cardsShown) {
            string? pick = GameMain.CardPickPending; // "cards/<升级 id>"（UI.Click）
            GameMain.CardPickPending = null;         // 消费式：同一选择只报一次
            if (pick == null) return;
            if (pick.StartsWith("cards/")) pick = pick.Substring("cards/".Length);
            int idx = _upgrades.FindIndex(u => u.Id == pick); // 条目 key = 升级行 id
            if (idx < 0) return; // 非本池 key（对话框在途等）——忽略，不误吞升级轮次
            ApplyOption(idx);
            --_pendingLevels;
            _cardsShown = false;
            GameMain.HideCardsDoc();
            if (_pendingLevels <= 0) Time.Scale = 1f; // 选完恢复（多级连选继续冻结）
            return;
        }
        if (_pendingLevels > 0 && _upgrades.Count > 0) { // 空池不弹也不冻结（缺表语义）
            Time.Scale = 0f; // 卡片期间冻结（RNG 不消耗，批① D5 语义）
            int n = _pickRotation++;
            int m = _upgrades.Count;
            GameMain.ShowCardsDoc("升级！三选一", new List<UiItem> {
                new() { Key = _upgrades[n % m].Id,       Fields = { ["label"] = _upgrades[n % m].Label } },
                new() { Key = _upgrades[(n + 2) % m].Id, Fields = { ["label"] = _upgrades[(n + 2) % m].Label } },
                new() { Key = _upgrades[(n + 4) % m].Id, Fields = { ["label"] = _upgrades[(n + 4) % m].Label } },
            });
            _cardsShown = true;
        }
    }

    /// 升级应用（批② T4：switch → upgrades.tab kind 派发；数值/弹种全表读，
    /// 语义与原硬编码逐项等价）。kind：0 移速 / 1 磁力 / 2 射速 / 3 换弹种
    /// （value = weapons 行 id）/ 4 生命上限 / 5 环绕+1。
    private void ApplyOption(int o)
    {
        if (o < 0 || o >= _upgrades.Count) return;
        var up = _upgrades[o];
        switch (up.Kind) {
        case 0: { // 移速（value = 倍率）
            var st = gameObject.GetComponent<Stats>();
            st.MoveSpeed *= MulVal(up.Value, 1.10f);
            gameObject.SetComponent(st);
            break;
        }
        case 1: { // 磁力（value = 倍率）
            var st = gameObject.GetComponent<Stats>();
            st.PickupRadius *= MulVal(up.Value, 1.25f);
            gameObject.SetComponent(st);
            break;
        }
        case 2: { // 射速（value = 倍率，Interval ×）
            var sh = gameObject.GetComponent<Shooter>();
            sh.Interval = System.Math.Max(0.05f, sh.Interval * MulVal(up.Value, 0.85f));
            gameObject.SetComponent(sh);
            break;
        }
        case 3: { // 换弹种（value = weapons 行 id → ProjectileId）
            var w = ById(_weapons, up.Value);
            var sh = gameObject.GetComponent<Shooter>();
            if (w != null && w.Prefab.Length == 16) {
                sh.ProjectileId = GuidLow32(w.Prefab);
                gameObject.SetComponent(sh);
            } else {
                Console.Error.WriteLine("[lemon][warn] PlayerCombat：换弹种 '" + up.Value
                                        + "' 无 weapons 行/prefab guid——保底不改");
            }
            break;
        }
        case 4: { // 生命上限（value = 点数）
            var hp = gameObject.GetComponent<Health>();
            float add = AddVal(up.Value, 25f);
            hp.Max += add;
            hp.Cur += add;
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
            Save.SetString("vs.best", score.ToString(), Save.Chan.Meta);
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径；全档）
        }
        string title = newBest ? $"★ 新纪录 {score} 分！"
                               : $"本局 {score} 分（最高 {GameMain.Run.Best}）";
        // 批③d-1：死亡对话框 = 卡片文档单条形态（key "ok" → "cards/ok" 事件回传）
        GameMain.ShowCardsDoc(title, new List<UiItem> {
            new() { Key = "ok", Fields = { ["label"] = "复活" } },
        });
    }

    private void Revive()
    {
        GameMain.Run.Dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        GameMain.HideCardsDoc();
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
