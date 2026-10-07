using System;
using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>vs-survivor 模板玩家（M5 批④）：8 向移动 + HUD 四要素 + 升级三选一
/// （固定序，零 RNG）+ 环绕刃自愈 + 队友英雄跟班（AllyBehaviour：跟随/飞剑/拖尾）
/// + 击杀掉宝石 + 死亡结算/复活（死亡对话框点击；批④后修④）+ 空格冲刺残影
/// （Input.Attack bit4；编辑器已映射 Space，玩家自动索敌开火故该位无既有语义）
/// + 受击表现（M6a 批①）：Hit 订阅——怪受击 = monster-hit 段 Play+Queue 回
/// 行走 + 伤害飘字 + 世界血条；玩家受击 = hero-hit 段同款 + 受伤黄字 + 绿条
/// 常显（存档键 svr.* = 无 GUI 跑法的机器可读出口，RedVsBlue rvb.* 同款；
/// 批③d-2 落账：svr.* 属局内诊断快照语义——保留 Slot 档随局失效，不迁档）。
/// 单类挂玩家实体（多脚本 scripts[] 属 M5 余项，见 M5.md §21.2 D4 注）。
/// 批② T4 数值表化（ADR-012）：升级池/武器参数读 Assets/tables/upgrades.tab +
/// weapons.tab（列头即列契约；缺表 = 空池 + warn，游戏永不因表缺炸 Play）；
/// XP 曲线读 balance.tab 写 Lemon.Balance；散射武器 = 表行 + prefab + 本类
/// Spawn 订阅补发（引擎零改动——验收② 演示）。
/// 批③d-2（M6b 档1 流程）：本实体迁 Prefabs/Player.prefab（MainMenu.scene 单
/// 场景 = GameFlow 开局重挂）；死亡策略游戏侧——首死 RtUi 复活对话 / 二死
/// GameFlow.ShowResults 结算；飘字/血条接 GameMain.Settings 开关门控。</summary>
public sealed class PlayerBehaviour : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    // 数值表 GUID（Assets/tables/；批② T4——右键「复制 GUID」粘贴处同 WaveTableLoader）
    private const string kWeaponsTable = "7e57100000100002";
    private const string kUpgradesTable = "7e57100000100003";
    private const string kBalanceTable = "7e57100000100004";

    // M6a 批① 受击段 clip（Assets/hero-hit.clip / monster-hit.clip；GUID 与模板
    // 同号——Anim.ClipId = GUID 低 32 位自算，Main.scene 玩家/Mob.prefab 的
    // clipId 536870913/536870914 就是 kHeroWalk/kMobWalk）
    private static readonly uint kHeroWalk = Anim.ClipId("5bd31a7c20000001");
    private static readonly uint kHeroHit = Anim.ClipId("5bd31a7c20000003");
    private static readonly uint kMobWalk = Anim.ClipId("5bd31a7c20000002");
    private static readonly uint kMobHit = Anim.ClipId("5bd31a7c20000004");

    // M6c 竖切批：音效资产 GUID（Assets/Audio/；meta 预写固定号——模板锚点同款
    // 纪律。BGM 常量在 GameFlow kBgm）
    private const string kSfxHit = "6a6d100000000002";     // 怪受击（Body Hit Hard）
    private const string kSfxKill = "6a6d100000000003";    // 击杀（Fire Ball Hit）
    private const string kSfxPickup = "6a6d100000000004";  // 拾取宝石（SE Trade）
    private const string kSfxLevelUp = "6a6d100000000005"; // 升级（Holy Word）
    private const string kSfxWave = "6a6d100000000006";    // 波次横幅（SE Confirm）
    private const string kSfxDash = "6a6d100000000007";    // 冲刺（Sword Swoosh）
    private const string kSfxShoot = "6a6d100000000008";   // 主弹发射（Arrow Shoot）

    // 飘字/血条色（Fx 通道是 RGBA 序——Ui.Set 的 ABGR 惯例色不能直接搬）
    private const uint kFxTextMob = 0xFF5060F0u;    // 怪受伤害字：暖红
    private const uint kFxTextPlayer = 0xFFF0F060u; // 玩家受伤字：警示黄
    private const uint kFxBarMob = 0xFF30B0F0u;     // 怪显伤条：红
    private const uint kFxBarPlayer = 0xFF60D060u;  // 玩家常显条：绿

    // M7c 批① 表现升级：贴图血条皮肤（bar_bg/bar_fg 64×10；宽 26px → 高 26×10/64≈4）
    // + 15% 暴击率（纯表现层展示——伤害结算在引擎 ShooterSystem，演示 Crit 样式）
    private static readonly FxBarSkin kMobBarSkin =
        new("7e57000000000101", "7e57000000000102", 0xFFE8E8E8u, 4f);
    private const double kCritChance = 0.15;
    private static readonly System.Random kCritRng = new();

    private const float kArenaHalf = 1000f; // 软竞技场边界（脚本层钳制）
    private const uint kColorHp = 0xFF30B0F0u;   // 血条红（ABGR）
    private const uint kColorXp = 0xFF30D8F0u;   // 经验金
    private const uint kColorTime = 0xFFF0F0F0u; // 计时白
    private const uint kColorKill = 0xFF4098F0u; // 击杀橙
    private const uint kColorWave = 0xFF60E0A0u; // 波次绿

    // ---- 冲刺 + 残影（空格 = Input.Attack bit4；编辑器已映射 Space，模板无既有攻击语义）----
    private const float kDashDist = 160f;       // 冲刺距离 px（方向触发时刻锁定）
    private const float kDashTime = 0.16f;      // 冲刺时长 s（≈1000px/s = 基础移速 ×10）
    private const float kDashCooldown = 0.5f;   // 冲刺冷却 s
    private const float kGhostInterval = 0.03f; // 残影生成间隔 s（≈每 2 帧一枚）
    private const float kGhostLife = 0.28f;     // 残影寿命 s（渐隐 + 收缩）
    private const float kGhostShrink = 0.14f;   // 残影寿命末收缩比（1 → 0.86）
    private const int kMaxGhosts = 16;          // 残影实体上限（预算护栏；峰值 ≈ 寿命/间隔）
    private const byte kGhostAlpha0 = 0xA0;     // 残影初始 alpha（ColorRGBA 高位，0..255 单位）
    private const byte kSrFlipX = 0x01;         // SpriteRenderer.flags（与 RenderComponents.h 同步）
    private const byte kSrFlipY = 0x02;
    private const byte kSrEnabled = 0x04;       // C# 整写零值 = 禁用，SetComponent 前必须显式带上
    private const short kPlayerOrder = 1;       // 玩家桶内层序（残影 0 垫身后；负值被 uint16 强转会排最顶）

    // ---- 批② T4 表载缓存（Start 一次载入；Play 中改表下一局生效——快照语义）----
    private sealed class WeaponRow
    {
        public string Id = "", Prefab = "";
        public float Interval, Speed, Pierce, Count, Radius, Angle;
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
    // 散射参数（weapons.scatter 行；count≤1 = 无散射语义，OnSpawn 直通）
    private string _scatterPrefab = "7e57100000100005";
    private uint _scatterPrefabLow;   // Shooter.ProjectileId 口径（低 32 位）
    private float _scatterCount = 1f, _scatterAngle = 30f, _scatterSpeed = 300f;
    private int _scatterShots;        // 补发次数（svr.scatter 诊断出口——bench 断言用）

    private float _runTime;
    private int _kills;
    private int _best;
    private bool _dead;
    private bool _reviveUsed; // 批③d-2：每局一次复活（死亡策略游戏侧；prefab 重挂自然复位）
    private int _pendingLevels; // LevelUp 事件累计的待选次数
    private bool _cardsShown;
    private int _pickRotation;  // 三选一轮换序（确定性）
    private int _bladeCount = 2;
    private readonly List<ulong> _blades = new();
    private float _bladeAngle;
    private int _swordExtra; // 飞剑齐射追加数（升级"飞剑 +1"；AllyBehaviour.ExtraSwords 同步）
    private ulong _ally; // 队友英雄实体（EnsureAlly 自愈；热重载经 StateBag 迁移）

    private float _dashLeft;                      // 剩余冲刺距离（>0 = 冲刺中）
    private Vec2 _dashDir = new(1f, 0f);          // 冲刺方向（触发时刻锁定）
    private float _dashCd;                        // 冲刺冷却剩余 s
    private Vec2 _facing = new(1f, 0f);           // 最近一次非零移动轴（无输入时冲刺方向）
    private bool _attackPrev;                     // attack 位上一帧态（Input 只有按住态，无沿检测）
    private float _ghostAcc;                      // 残影节拍累加器
    private readonly List<ulong> _ghosts = new(); // 残影实体句柄（与 _ghostAge 同序）
    private readonly List<float> _ghostAge = new();

    // 受击表现计数（诊断出口——存档键 svr.*，无 GUI 跑法断言用；诊断态不入
    // StateBag，热重载归零可接受）
    private int _mobHits, _playerHits, _fxTexts, _fxBars;

    public PlayerBehaviour()
    {
        // Subscribe 助手（M15）：实例销毁自动退订（裸 Events.Subscribe 只增不删，
        // 死亡→复活重挂会逐局累积订阅）
        Subscribe(GameEvent.LevelUp, m => {
            if (m.Src.Id != gameObject.Entity.Id) return;
            ++_pendingLevels;
            Audio.PlayOneShot(kSfxLevelUp, 0.7f); // M6c 竖切批：升级音
        });
        // M6c 竖切批：拾取音（宝石吸附即响；fire-and-forget，无状态）
        Subscribe(GameEvent.Pickup, _ => Audio.PlayOneShot(kSfxPickup, 0.5f));
        Subscribe(GameEvent.Death, OnDeath);
        // M6a 批① 受击表现（模板 PlayerCombat.OnHit 同款链路）：Hit 事件在低频
        // 命中率场景安全（本场景量级 ~20/s；万怪级压测请走 Battle.scene 的
        // bench C++ 直写口径，勿在 C# 逐命中订阅）
        Subscribe(GameEvent.Hit, OnHit);
        // 批② T4 散射（验收② 演示）：ShooterSystem 主弹 Spawn 事件（src=弹，dst=射手）
        // → 当前散射弹种时沿主弹方向 ±angle/2 补 count-1 枚（C# Instantiate 不触发
        // Spawn 事件 = 无递归；引擎零改动的纯脚本扇形）
        Subscribe(GameEvent.Spawn, OnSpawn);
        Subscribe(GameEvent.WaveStart, m => {
            Ui.Set("wave", $"—— 第 {(int)m.P0 + 1} 波 ——", -1f, kColorWave);
            Audio.PlayOneShot(kSfxWave, 0.6f); // M6c 竖切批：波次横幅音
        });
    }

    /// <summary>受击表现（弹道 Hit payload [0]=伤害 [1][2]=位置；Hazard Hit 只带
    /// [0]=伤害——位置从受害者 Transform 补位，故统一走 Transform）。</summary>
    private void OnHit(GameEventMsg m)
    {
        var victim = GameObject.From(m.Dst);
        if (!victim.Alive || !victim.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) { // 怪受击：受击段立即打断、播完自动回行走 + 飘字 + 显伤条
            Audio.PlayOneShot(kSfxHit, 0.45f); // M6c 竖切批：受击音（高命中率小音量）
            Anim.Play(victim, kMobHit, false);
            Anim.Queue(victim, kMobWalk);
            // 批③d-2：飘字/血条 = 设置开关门控（GameFlow 设置屏，Settings 档持久）
            if (GameMain.Settings.FxText &&
                victim.TryGetComponent<Transform2D>(out var tf)) {
                // M7c 批①：暴击 = Crit 糖（中文「暴击 N」黄字 Pop 弹跳 + 随机散布；
                // 表现层演示——伤害数值结算在引擎侧不动）。锚点 = mob 精灵头顶
                //（世界 Y 向下 = 负偏移）。轮④ 基线修正后字形整体上移
                // 2×(bearingY-h/2)×字号 ≈ 24×字号 px，锚点同步下压补偿：
                // crit 字号 1.3 → +31；常规字号 1 → +24
                if (kCritRng.NextDouble() < kCritChance)
                    Fx.Crit($"暴击 {m.P0:0}", new Vec2(tf.Pos.X - 4f, tf.Pos.Y - 63f));
                else
                    Fx.Text(m.P0, new Vec2(tf.Pos.X - 4f, tf.Pos.Y - 56f), kFxTextMob);
                ++_fxTexts;
            }
            if (GameMain.Settings.FxBar &&
                victim.TryGetComponent<Health>(out var hp)) {
                // M7c 批①：贴图血条 + 延迟白条（掉血时残条停在旧血量线性收敛）
                Fx.Bar(victim, hp.Cur / hp.Max, kFxBarMob, 26f, kMobBarSkin);
                ++_fxBars;
            }
            ++_mobHits;
            Save.SetString("svr.mobhit", _mobHits.ToString());
        } else if (m.Dst.Id == gameObject.Entity.Id) { // 玩家受击（Hazard 接触）
            Anim.Play(gameObject, kHeroHit, false);
            Anim.Queue(gameObject, kHeroWalk);
            var tf = gameObject.GetComponent<Transform2D>();
            var hp = gameObject.GetComponent<Health>();
            if (GameMain.Settings.FxText) {
                Fx.Text(m.P0, new Vec2(tf.Pos.X - 4f, tf.Pos.Y - 204f), kFxTextPlayer);
                ++_fxTexts; // 玩家精灵 player01 337×346——头顶 ≈ -173（Y 向下），行底贴头顶血条上方；轮④ 基线补偿 +24
            }
            if (GameMain.Settings.FxBar) {
                Fx.Bar(gameObject, hp.Cur / hp.Max, kFxBarPlayer, 32f); // 每击续命 → 常显
                ++_fxBars;
            }
            ++_playerHits;
            Save.SetString("svr.playerhit", _playerHits.ToString());
        }
        Save.SetString("svr.fxtext", _fxTexts.ToString());
        Save.SetString("svr.fxbar", _fxBars.ToString()); // Bar 调用数（含帧刷新复写）
    }

    private void OnDeath(GameEventMsg m)
    {
        var src = GameObject.From(m.Src);
        if (!src.Alive || !src.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) {
            ++_kills;
            Audio.PlayOneShot(kSfxKill, 0.6f); // M6c 竖切批：击杀音
            if (src.TryGetComponent<Transform2D>(out var tf)) // 两阶段销毁：当帧可读
                Instantiate.Prefab(kGemPrefab, new Vec2(tf.Pos.X, tf.Pos.Y));
        } else if (m.Src.Id == gameObject.Entity.Id) {
            Die();
        }
    }

    /// <summary>散射补发（批② T4 验收② 演示）：主弹（Shooter 索敌自动发射）的
    /// Spawn 事件 → 沿主弹方向 ±angle/2 均布补 count-1 枚。速度用表 scatter.speed
    /// 覆写（表值 &gt;0 才生效，否则弹体 Projectile.speed）；补弹走 C#
    /// Instantiate（编辑器资产钩子 → Play World），不触发 Spawn 事件 = 无递归。</summary>
    private void OnSpawn(GameEventMsg m)
    {
        if (m.Dst.Id != gameObject.Entity.Id) return;
        Audio.PlayOneShot(kSfxShoot, 0.3f); // M6c 竖切批：主弹发射音（Shooter 间隔节流）
        if (_scatterCount <= 1f) return;
        var sh = gameObject.GetComponent<Shooter>();
        if (sh.ProjectileId != _scatterPrefabLow) return; // 只在散射弹种激活期
        var main = GameObject.From(m.Src);
        if (!main.Alive || !main.TryGetComponent<Velocity>(out var mv)) return;
        float baseAng = System.MathF.Atan2(mv.V.Y, mv.V.X);
        var tf = gameObject.GetComponent<Transform2D>();
        int n = (int)_scatterCount;
        float step = _scatterAngle / (n - 1); // angle = 全张角（度）：count=3/30° → ±15°
        for (int i = 1; i < n; ++i) {
            float a = baseAng - _scatterAngle / 2f + step * i;
            Vec2 dir = new(System.MathF.Cos(a), System.MathF.Sin(a));
            var g = Instantiate.Prefab(_scatterPrefab,
                                       new Vec2(tf.Pos.X + dir.X * 12f,
                                                tf.Pos.Y + dir.Y * 12f));
            if (!g.Alive) continue;
            var v = g.GetComponent<Velocity>();
            v.V = dir * _scatterSpeed;
            g.SetComponent(v);
        }
        ++_scatterShots; // 诊断出口（svr.* 存档键惯例——无 GUI 跑法的断言口径）
        Save.SetString("svr.scatter", _scatterShots.ToString());
    }

    // ---- 批② T4 表载（ADR-012 D1 全字符串格；坏行跳过 + warn、缺表保底——
    // 数值与表化前硬编码一致 = 行为等价变换；模板 PlayerCombat 同款结构）----

    private void LoadTables()
    {
        if (Table.Has(kWeaponsTable)) {
            int cId = ColOf(kWeaponsTable, "id"), cGuid = ColOf(kWeaponsTable, "prefabGuid"),
                cInt = ColOf(kWeaponsTable, "interval"), cSpd = ColOf(kWeaponsTable, "speed"),
                cCnt = ColOf(kWeaponsTable, "count"), cRad = ColOf(kWeaponsTable, "radius"),
                cAng = ColOf(kWeaponsTable, "angle"), cPrc = ColOf(kWeaponsTable, "pierce");
            for (int r = 1; r < Table.Rows(kWeaponsTable); ++r) {
                string? id = At(kWeaponsTable, r, cId);
                if (string.IsNullOrEmpty(id)) { WarnBadRow(kWeaponsTable, r); continue; }
                _weapons.Add(new WeaponRow {
                    Id = id!, Prefab = At(kWeaponsTable, r, cGuid) ?? "",
                    Interval = F(kWeaponsTable, r, cInt), Speed = F(kWeaponsTable, r, cSpd),
                    Pierce = F(kWeaponsTable, r, cPrc), Count = F(kWeaponsTable, r, cCnt),
                    Radius = F(kWeaponsTable, r, cRad), Angle = F(kWeaponsTable, r, cAng),
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
            if (ById(_weapons, "scatter") is { } sc) { // 散射参数行（验收② 演示武器）
                if (sc.Prefab.Length == 16) {
                    _scatterPrefab = sc.Prefab;
                    _scatterPrefabLow = GuidLow32(sc.Prefab);
                }
                if (sc.Count > 1f) _scatterCount = sc.Count;
                if (sc.Angle > 0f) _scatterAngle = sc.Angle;
                if (sc.Speed > 0f) _scatterSpeed = sc.Speed;
            }
        } else {
            Console.Error.WriteLine("[lemon][warn] PlayerBehaviour：weapons.tab 缺失"
                                    + "——环绕刃用保底参数（90px / 2.2rad/s），换弹种/散射无效");
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
            Console.Error.WriteLine("[lemon][warn] PlayerBehaviour：upgrades.tab 缺失"
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
        => Console.Error.WriteLine($"[lemon][warn] PlayerBehaviour：{table} 第 {row} 行坏——跳过");
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

    protected override void Start()
    {
        LoadTables(); // 批② T4：升级池/武器参数/XP 曲线（缺表保底，永不炸 Play）
        _best = int.TryParse(Save.GetString("vs.best", Save.Chan.Meta), out var b) ? b : 0; // 上一局纪录（跨局归 meta 档，与模板 PlayerCombat 同键同档）
        var sr = gameObject.GetComponent<SpriteRenderer>();
        sr.SortOrder = kPlayerOrder; // 残影(order 0)垫身后；只在同图集桶内生效（见 SpawnGhost）
        gameObject.SetComponent(sr);
        EnsureAlly(); // 队友英雄进场（自愈点：Update 每帧对账，丢失即补）
    }

    protected override void Update()
    {
        bool attack = Input.Attack; // 空格（InputState bit4）
        if (_dead) {
            // 死亡对话框：点击/数字键 1 → CardPick()==0 复活（消费式回读，与升级
            // 卡片同通道；批④后修④——R 键路径废弃，交互不依赖键盘焦点路由）
            if (Ui.CardPick() == 0) Revive();
            _attackPrev = attack; // 死亡期间按住不放 → 复活后不误触发冲刺
            return;
        }
        _runTime += Time.DeltaTime;

        var tf = gameObject.GetComponent<Transform2D>();
        var stats = gameObject.GetComponent<Stats>();
        Vec2 axis = Input.Axis;
        float dt = Time.DeltaTime;
        if (axis.X != 0f || axis.Y != 0f) _facing = Normalize(axis);

        // ---- 冲刺：边沿触发 + 冷却（Input 无沿检测，自做 _attackPrev）----
        _dashCd = System.Math.Max(0f, _dashCd - dt);
        if (attack && !_attackPrev && _dashCd <= 0f && _dashLeft <= 0f) {
            _dashDir = _facing;      // 方向锁定在触发时刻（无输入 = 最近移动朝向）
            _dashLeft = kDashDist;
            _dashCd = kDashCooldown;
            _ghostAcc = kGhostInterval; // 当帧即出第一枚残影
            Audio.PlayOneShot(kSfxDash, 0.5f); // M6c 竖切批：冲刺破空音
        }
        _attackPrev = attack;

        // ---- 位移：冲刺优先（忽略轴输入）；距离预算积分 → 任何帧率总位移恰为 160px ----
        float dashStep = System.MathF.Min(kDashDist / kDashTime * dt, _dashLeft);
        Vec2 step = _dashLeft > 0f ? _dashDir * dashStep : axis * (stats.MoveSpeed * dt);
        if (_dashLeft > 0f) _dashLeft -= dashStep;
        tf.Pos = new Vec2(
            System.Math.Clamp(tf.Pos.X + step.X, -kArenaHalf, kArenaHalf),
            System.Math.Clamp(tf.Pos.Y + step.Y, -kArenaHalf, kArenaHalf));
        gameObject.SetComponent(tf);

        UpdateHud();
        EnsureAlly();
        UpdateBlades(tf);
        UpdateGhosts(tf, dt);
        UpdateCards();
    }

    /// <summary>队友英雄：贴身跟班，挂 AllyBehaviour（跟随 + Shooter 索敌发飞剑 + 拖尾）。
    /// 与环绕刃同款自愈语义：实体丢失（首次/热重载清场/异常）即原位补挂。</summary>
    private void EnsureAlly()
    {
        AllyBehaviour.Player = gameObject.Entity; // 跟随锚点（热重载后新实例重设静态）
        AllyBehaviour.ExtraSwords = _swordExtra;  // 齐射数同步（升级面唯一写入口）
        if (_ally != 0 && GameObject.From(new EntityHandle { Id = _ally }).Alive) return;
        var tf = gameObject.GetComponent<Transform2D>();
        var g = Instantiate.Spawn<AllyBehaviour>(
            Assets.SpriteOf(AllyBehaviour.kHeroSheet),
            new Vec2(tf.Pos.X + 42f, tf.Pos.Y - 30f));
        _ally = g.Alive ? g.Entity.Id : 0;
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

    /// <summary>残影：复刻玩家当前动画帧（AnimatorSystem 先于 CSharpBatchSystem 跑，此处
    /// 读到的 spriteId 就是本 tick 帧）+ 中立队 2（TeamTable 0-2 Neutral、1-2 Ghost：
    /// 怪不追/不分离/不命中）+ 桶内 order 0 垫玩家（kPlayerOrder）身后。贴图本色保留，
    /// 只淡 alpha；翻面位从玩家继承。玩家贴图与残影同页同桶，故 order 生效——其余实体
    /// （怪/宝石/刃/光点）各在不同 PNG 的桶内，跨桶由 hash 定序，不受影响。</summary>
    private void SpawnGhost(Transform2D playerTf)
    {
        var src = gameObject.GetComponent<SpriteRenderer>();
        var g = Instantiate.Spawn(src.SpriteId, playerTf.Pos);
        if (!g.Alive) return;

        var m = g.GetComponent<Meta>();
        m.Team = 2; // 中立：不入敌对目标板，也不占刷怪配额（capAlive 按 spawnTeam 计）
        g.SetComponent(m);

        var sr = g.GetComponent<SpriteRenderer>();
        sr.ColorRGBA = (src.ColorRGBA & 0x00FFFFFFu) | ((uint)kGhostAlpha0 << 24);
        sr.SortOrder = 0;
        sr.Flags = (byte)(kSrEnabled | (src.Flags & (kSrFlipX | kSrFlipY)));
        g.SetComponent(sr);

        var tf = g.GetComponent<Transform2D>();
        tf.Rot = playerTf.Rot;
        tf.Scale = playerTf.Scale;
        g.SetComponent(tf);

        _ghosts.Add(g.Entity.Id);
        _ghostAge.Add(0f);
    }

    /// <summary>残影节拍 + 老化（渐隐/收缩/销毁）。与 AllyBehaviour.UpdatePuffs 同构
    /// （倒序删双清单）：残影无 Projectile 组件，ProjectileLifetimeSystem 不管它，
    /// 寿命必须脚本自管；冻结（dt=0，升级卡片）时老化自然停摆。</summary>
    private void UpdateGhosts(Transform2D playerTf, float dt)
    {
        _ghostAcc += dt;
        if (_ghostAcc >= kGhostInterval) {
            if (_dashLeft > 0f) {
                _ghostAcc = 0f;
                if (_ghosts.Count < kMaxGhosts) SpawnGhost(playerTf);
            } else {
                _ghostAcc = kGhostInterval; // 非冲刺期蓄满待发（下次冲刺当帧即出第一枚）
            }
        }

        float baseScale = playerTf.Scale.X; // 玩家缩放（升到 1 之外也不跳变）
        for (int i = _ghosts.Count - 1; i >= 0; --i) {
            float age = _ghostAge[i] + dt;
            var g = GameObject.From(new EntityHandle { Id = _ghosts[i] });
            if (!g.Alive || age >= kGhostLife) {
                if (g.Alive) g.Destroy(); // 两阶段销毁（帧首提交）
                _ghosts.RemoveAt(i);
                _ghostAge.RemoveAt(i);
                continue;
            }
            _ghostAge[i] = age;
            float inv = 1f - age / kGhostLife;
            var sr = g.GetComponent<SpriteRenderer>();
            sr.ColorRGBA = (sr.ColorRGBA & 0x00FFFFFFu) | ((uint)(kGhostAlpha0 * inv) << 24);
            g.SetComponent(sr);
            var tf = g.GetComponent<Transform2D>();
            float s = baseScale * (1f - kGhostShrink * (1f - inv));
            tf.Scale = new Vec2(s, s);
            g.SetComponent(tf);
        }
    }

    /// <summary>清扫全部残影：死亡结算 / 热重载换域。残影实体跨域存活但清单入不了
    /// StateBag（无 List 类型），不清扫 = 永远挂场的幽灵精灵（冻结期老化停走）。</summary>
    private void ClearGhosts()
    {
        for (int i = 0; i < _ghosts.Count; ++i) {
            var g = GameObject.From(new EntityHandle { Id = _ghosts[i] });
            if (g.Alive) g.Destroy();
        }
        _ghosts.Clear();
        _ghostAge.Clear();
    }

    private static Vec2 Normalize(Vec2 v)
    {
        float l = System.MathF.Sqrt(v.X * v.X + v.Y * v.Y);
        return l > 0f ? new Vec2(v.X / l, v.Y / l) : v;
    }

    private void UpdateCards()
    {
        if (_cardsShown) {
            int pick = Ui.CardPick();
            if (pick < 0) return;
            int n = _pickRotation - 1; // ShowCards 时已自增
            ApplyOption((n + pick * 2) % _upgrades.Count);
            --_pendingLevels;
            _cardsShown = false;
            Ui.HideCards();
            if (_pendingLevels <= 0) Time.Scale = 1f; // 选完恢复（多级连选继续冻结）
            return;
        }
        if (_pendingLevels > 0 && _upgrades.Count > 0) { // 空池不弹也不冻结（缺表语义）
            Time.Scale = 0f; // 卡片期间冻结（RNG 不消耗，批① D5 语义）
            int n = _pickRotation++;
            int m = _upgrades.Count;
            Ui.ShowCards("升级！三选一", _upgrades[n % m].Label,
                         _upgrades[(n + 2) % m].Label, _upgrades[(n + 4) % m].Label);
            _cardsShown = true;
        }
    }

    /// 升级应用（批② T4：switch → upgrades.tab kind 派发；数值/弹种全表读，
    /// 语义与原硬编码逐项等价）。kind：0 移速 / 1 磁力 / 2 射速 / 3 换弹种
    /// （value = weapons 行 id；散射行激活 Spawn 补发）/ 4 生命上限 / 5 环绕+1 /
    /// 6 飞剑+1。
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
        case 3: { // 换弹种（value = weapons 行 id → ProjectileId；行带 interval 则同写）
            var w = ById(_weapons, up.Value);
            var sh = gameObject.GetComponent<Shooter>();
            if (w != null && w.Prefab.Length == 16) {
                sh.ProjectileId = GuidLow32(w.Prefab);
                if (w.Interval > 0f) sh.Interval = w.Interval; // 散射 0.4s 慢射速补偿多弹
                gameObject.SetComponent(sh);
            } else {
                Console.Error.WriteLine("[lemon][warn] PlayerBehaviour：换弹种 '"
                                        + up.Value + "' 无 weapons 行/prefab guid——保底不改");
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
        case 6: // 飞剑 +1（齐射追加；AllyBehaviour 认领时刻克隆，扇形弧线展开）
            if (_swordExtra < AllyBehaviour.kMaxExtraSwords) ++_swordExtra;
            break;
        }
    }

    private void Die()
    {
        if (_dead) return; // 多源 Death（弹道/区域）只结算一次
        _dead = true;
        _dashLeft = 0f; // 冲刺中断
        ClearGhosts();  // 死亡对话框冻结（Time.Scale=0）会让残影老化停走 → 先清场
        _cardsShown = false; // 弃置在途升级卡（_pendingLevels 保留，复活后重弹）
        Time.Scale = 0f;
        int score = _kills * 10 + (int)_runTime;
        bool newBest = score > _best;
        if (newBest) {
            _best = score;
            Save.SetString("vs.best", score.ToString(), Save.Chan.Meta);
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径）
        }
        string title = newBest ? $"★ 新纪录 {score} 分！"
                               : $"本局 {score} 分（最高 {_best}）";
        // 批③d-2：死亡策略归游戏侧——首死 RtUi 复活对话（既有 UX 保留）/二死
        // 结算屏（GameFlow 原语；重开 = 清场 + prefab 重挂，实例字段含 _reviveUsed
        // 随新实例自然归零）
        if (!_reviveUsed) {
            _reviveUsed = true;
            Ui.Set("over", title, -1f, 0xFF5080FFu);
            Ui.ShowDialog(title, "复活");
        } else {
            int sec = (int)_runTime;
            GameFlow.ShowResults(newBest ? $"★ 新纪录 {score} 分！" : "本局结束",
                                 score.ToString(), $"{sec / 60:D2}:{sec % 60:D2}",
                                 _kills.ToString(), _best.ToString());
        }
    }

    private void Revive()
    {
        _dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        Ui.HideCards();
        Ui.Clear("over");
    }

    // 热重载状态迁移（数值面；刃实体经 UpdateBlades 自愈重建轨道）。冲刺状态
    // （_dashLeft/_dashCd/_dashDir/_facing）不迁移：换域即作废，靠输入重新起手。
    // 残影清单入不了包（StateBag 只有基元 + Vec2），只能连同实体一并清扫。
    protected override void OnHotReloadOut(StateBag bag)
    {
        ClearGhosts();
        bag.Set("time", _runTime);
        bag.Set("kills", _kills);
        bag.Set("best", _best);
        bag.Set("dead", _dead);
        bag.Set("pending", _pendingLevels);
        bag.Set("rotation", _pickRotation);
        bag.Set("blades", _bladeCount);
        bag.Set("swordx", _swordExtra);
        bag.Set("ally", _ally); // 实体本体跨域存活，只迁句柄（防自愈重刷 = 双队友）
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
        if (bag.TryGet("swordx", out int sx)) _swordExtra = sx;
        if (bag.TryGet("ally", out ulong a)) _ally = a;
    }
}
