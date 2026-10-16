using System;
using System.Collections.Generic;
using System.Diagnostics;
using Lemon;
using Lemon.Interop;

/// <summary>红蓝大作战压测场（Scenes/Battle.scene 专用）：两军对冲的性能场景。
///
/// 阵营与默认势力表（TeamTable::Default）对账：红 = team 1（队内 soft-collide
/// 自分离 → 阵型会散开），蓝 = team 3（1↔3 敌对；3↔3 互穿 → 推挤时更密集——
/// 两种密度形态同场，恰是 Hazard/分离查询的双形状压测）。弹体势力继承射手
/// （AISystem bulletTeam），双方 Shooter 对射零脚本干预。
///
/// 单位两型随机混编（确定性 LCG——续战补充兵也走同序列），**都是英雄本体**
/// （dungeon_hero_1 + hero-walk 动画，对齐游戏里的两个角色）：
///   - 玩家型（PlayerBehaviour 的战斗面）：直射弹幕——Shooter 发
///     Bullet.prefab（伤害 8/速 320/命中即毁），行军途中点射；
///   - 队友型（AllyBehaviour 的战斗面，飞剑技能）：Shooter 发
///     FlySword.prefab（穿透弹道），出膛侧旋 + 预算内追踪（AllyBehaviour
///     同款弧线，见 Spawn 认领/SteerSwords；超 kMaxSteered 只偏转不追踪——
///     弧线观感保住，C# 逐剑成本有界）；体型 0.85（跟班略小一号，同款观感）。
///
/// 兵力口径：kPerSide = 总兵力（每边 10000）；kDeployAtStart = 首波列阵
/// （1500/边浅纵深高密度——全部落在默认视野内，深纵深立阵大半屏外白站）；
/// 战线损耗近线持续补充（每 1s ≤300，总兵力耗尽为止）= 攻城式持续压力。
/// 索敌视距 = 战术级（320/340）：接战带 ≈ 视距深。
///
/// **行军（2026-09-26 五万场）**：全员 Patrol——路径点 = 出生位镜像穿中
/// （a=列阵位、b=对侧镜像位），大军持续穿越中线对流（引擎固定速 60），
/// Shooter 边走边射；无 Chase（视距索敌在五万规模是 AI 巨耗源，且行军本身
/// 就消灭了"后排站桩"）。分帧播种（800/边/帧）防单帧巨型尖刺。
/// 本脚本只做播种/补充兵/死亡计数/飞剑表现层/HUD 与存档读数——战斗链全
/// 引擎侧。读数口径：HUD perf 行 = 模拟吞吐"步/s"（Stopwatch 墙钟计，60 =
/// 逻辑满速）；渲染帧率看 F3 Profiler。存档键 rvb.red/rvb.blue/rvb.steps
/// = 无 GUI 跑法的机器可读出口。调参：改 consts 后热重载 → Stop/Play 重播。</summary>
public sealed class RedVsBlue : LemonBehaviour
{
    // 资产 GUID（与 Assets/Prefabs 内 .meta 对账，勿改）
    private const string kHeroSheet = "5bd31a7c10000001";      // dungeon_hero_1.png
    private const string kSwordPng = "7e570000000000a1";       // flysword.png
    private const string kFlySwordPrefab = "7e571000000000a1"; // FlySword.prefab
    private const uint kFlySwordLow = 0x000000A1;              // FlySword.prefab 低 32 位
    private const uint kBulletLow = 0x00000003;                // Bullet.prefab 低 32 位
    private const uint kHeroWalkClip = 536870913u;             // hero-walk.clip 低 32 位

    // 阵营（默认势力表敌对对：1↔3；见类注释）
    private const uint kRedTeam = 1, kBlueTeam = 3;
    private const uint kRedTint = 0xFF5A5AFFu;   // RGBA 组包：R255 G90 B90
    private const uint kBlueTint = 0xFFFF785Au;  // R90 G120 B255

    // 兵力与节拍（布阵几何以"默认视野装得下"为准：半宽 ~640/半高 ~360——
    // 深纵深立阵大半在屏外白站，浅纵深高密度才有"大军同屏"观感）
    private const int kPerSide = 25000;       // 总兵力/边（五万同屏口径）
    private const int kDeployAtStart = 25000;  // 首波即全部（160 行×157 列 ≈ 706px 深）
    private const int kDeployPerFrame = 800;   // 分帧播种（防单帧 5 万实体巨型尖刺）
    private const float kAllyRatio = 0.45f;   // 队友型占比（活弹 ≈ 占比×射速×弹寿命）
    private const float kSpacing = 10f;        // 阵列间距（超高密度堆叠；红队分离半径 32 会自然摊开）
    private const int kRows = 160;            // 阵高行数（160×4.5 = 720 ≈ 视野高）
    private const float kFrontGap = 60f;      // 前排距中线（两军前沿隔 120px）
    private const float kHudInterval = 0.5f;

    // 飞剑弧线（AllyBehaviour 同源常量；只做表现层——改写 Velocity，弹速守恒）
    private const float kArcSwirl = 0.7f;   // 固定侧旋（rad，奇偶交替正负）
    private const float kTurnRate = 4.0f;   // 扑靶转向角速度（rad/s）
    private const float kSteerTime = 0.6f;  // 转向预算秒（耗尽/对齐/目标亡即直飞）
    private const int kMaxSteered = 768;    // 逐剑追踪上限（C# 成本护栏；超出只偏转）

    private uint _heroSheetId, _swordSpriteId;
    private int _redAlive, _blueAlive, _redDeployed, _blueDeployed;
    private float _hudAcc, _refillAcc;
    private int _swirlToggle;
    private readonly List<SwordState> _swords = new();
    private readonly Stopwatch _wall = Stopwatch.StartNew();
    private double _wallLast;          // 上次 HUD 节拍的墙钟（步/s 分母）
    private int _stepsSinceHud;        // 上次 HUD 节拍以来的模拟步数（分子）
    private ulong _lcg = 0x9E3779B97F4A7C15ul; // 确定性混编/抖动序列（续战同源）

    private struct SwordState
    {
        public ulong Id, Target;
        public float Steer; // 剩余转向预算秒（0 = 直飞）
    }

    private static float Rand(ref ulong s)
    {
        s = s * 6364136223846793005ul + 1442695040888963407ul;
        return ((s >> 33) & 0xFFFFFF) / (float)0x1000000;
    }

    private static float Length(Vec2 v) => System.MathF.Sqrt(v.X * v.X + v.Y * v.Y);
    private static Vec2 Rotate(Vec2 v, float rad)
    {
        float c = System.MathF.Cos(rad), s = System.MathF.Sin(rad);
        return new Vec2(v.X * c - v.Y * s, v.X * s + v.Y * c);
    }
    private static float WrapPi(float a)
    {
        while (a > System.MathF.PI) a -= 2f * System.MathF.PI;
        while (a < -System.MathF.PI) a += 2f * System.MathF.PI;
        return a;
    }

    public RedVsBlue()
    {
        // 死亡计数（只认带 Health 的单位——弹体寿命到期/命中销毁不算战损）
        Subscribe(GameEvent.Death, m => {
            var g = GameObject.From(m.Src);
            if (!g.TryGetComponent<Health>(out _)) return;
            if (!g.TryGetComponent<Meta>(out var meta)) return;
            if (meta.Team == kRedTeam) --_redAlive;
            else if (meta.Team == kBlueTeam) --_blueAlive;
        });
        // 飞剑认领（AllyBehaviour 同款链路）：引擎弹出膛（Dst=飞剑型射手）即
        // 侧旋偏转 + 预算内接管追踪。事件派发在帧末（AISystem 已写朝目标初速）。
        Subscribe(GameEvent.Spawn, m => {
            var shooter = GameObject.From(m.Dst);
            if (!shooter.TryGetComponent<Shooter>(out var sh) ||
                sh.ProjectileId != kFlySwordLow) return;
            var sword = GameObject.From(m.Src);
            if (!sword.TryGetComponent<Velocity>(out var sv)) return;
            float speed = Length(sv.V);
            if (speed <= 1f) return; // 无有效轴：引擎直飞，不接管
            _swirlToggle = -_swirlToggle; // 奇偶交替侧旋（单发也带弧）
            Vec2 dir = Rotate(sv.V / speed, kArcSwirl * _swirlToggle);
            sword.SetComponent(new Velocity { V = dir * speed });
            // 兜底贴图（拖尾光点）→ 指向性飞剑（AllyBehaviour 同款一次性改）+ 放大一档
            var ssr = sword.GetComponent<SpriteRenderer>();
            if (_swordSpriteId != 0 && ssr.SpriteId != _swordSpriteId) {
                ssr.SpriteId = _swordSpriteId;
                sword.SetComponent(ssr);
            }
            if (sword.TryGetComponent<Transform2D>(out var st)) {
                st.Rot = System.MathF.Atan2(dir.Y, dir.X); // 剑头指向出膛方向
                st.Scale = new Vec2(1.2f, 1.2f);
                sword.SetComponent(st);
            }
            if (_swords.Count < kMaxSteered)
                _swords.Add(new SwordState {
                    Id = sword.Entity.Id,
                    Target = sh.Target.IsNull ? 0 : sh.Target.Id,
                    Steer = kSteerTime,
                });
        });
    }

    protected override void Awake()
    {
        _heroSheetId = Assets.SpriteOf(kHeroSheet);
        _swordSpriteId = Assets.SpriteOf(kSwordPng);
        // 分离调参（2026-09-26 调参下放批）：五万 @10px 超密场实测 ~98% 红队实体
        // 压在默认 maxNeighbors=10 截断线上（真实密度远超）——收紧到 6 = 邻居扫描
        // 近乎减半（引擎默认不动，基准场零漂移）；densityCap 同步 ≤ 截断线才可触达
        Lemon.Physics.Separation(maxNeighbors: 6, densityCap: 6);
    }

    protected override void Update()
    {
        ++_stepsSinceHud;

        // 分帧播种：每帧 kDeployPerFrame/边，直至 kDeployAtStart（行军随出随走）
        if (_redDeployed < kDeployAtStart || _blueDeployed < kDeployAtStart) {
            int wave = Math.Min(kDeployPerFrame, kDeployAtStart - _redDeployed);
            if (wave > 0) SpawnFormation(kRedTeam, -1f, wave, _redDeployed);
            wave = Math.Min(kDeployPerFrame, kDeployAtStart - _blueDeployed);
            if (wave > 0) SpawnFormation(kBlueTeam, 1f, wave, _blueDeployed);
        }

        SteerSwords(Time.DeltaTime);

        _hudAcc += Time.DeltaTime;
        if (_hudAcc >= kHudInterval) {
            _hudAcc = 0f;
            // 模拟吞吐 = 步数 / 墙钟（编辑器 vsync 下渲染可低于此值——渲染帧率看 F3）
            double wallNow = _wall.Elapsed.TotalSeconds;
            float stepsPerSec = _wallLast > 0
                ? (float)(_stepsSinceHud / (wallNow - _wallLast)) : 60f;
            _wallLast = wallNow;
            _stepsSinceHud = 0;

            int total = Math.Max(1, _redAlive + _blueAlive);
            Ui.Set("battle", $"红 {_redAlive}  蓝 {_blueAlive}", (float)_redAlive / total);
            Ui.Set("perf", $"模拟 {stepsPerSec:F0}步/s  单位 {total}  飞剑 {_swords.Count}");
            Save.SetString("rvb.red", _redAlive.ToString());
            Save.SetString("rvb.blue", _blueAlive.ToString());
            Save.SetString("rvb.steps", stepsPerSec.ToString("F1"));
            Save.Flush();
        }

        // 五万场 = 单波无预备队（kPerSide == kDeployAtStart）；阵亡即减员，
        // 想要持续战改 kPerSide > kDeployAtStart 即恢复近线补充
    }

    /// 弧线扑靶：预算内以 kTurnRate 朝目标当前方位转向（对齐/耗尽/目标亡 → 直飞）。
    /// AllyBehaviour 同款数学，逐剑成本由 kMaxSteered 封顶。
    private void SteerSwords(float dt)
    {
        if (_swords.Count == 0) return;
        _swords.RemoveAll(s => !GameObject.From(new EntityHandle { Id = s.Id }).Alive);
        if (dt <= 0f) return; // 冻结（timeScale=0）时转向同停
        for (int i = 0; i < _swords.Count; ++i) {
            SwordState s = _swords[i];
            if (s.Steer <= 0f) continue;
            var g = GameObject.From(new EntityHandle { Id = s.Id });
            if (!g.TryGetComponent<Transform2D>(out var st)) continue;
            if (!g.TryGetComponent<Velocity>(out var v)) continue;
            float speed = Length(v.V);
            var tgt = GameObject.From(new EntityHandle { Id = s.Target });
            if (speed > 1f && s.Target != 0 && tgt.TryGetComponent<Transform2D>(out var tt)) {
                Vec2 toT = tt.Pos - st.Pos;
                float d = Length(toT);
                if (d > 1f) {
                    Vec2 axis = toT / d;
                    float cur = System.MathF.Atan2(v.V.Y, v.V.X);
                    float want = System.MathF.Atan2(axis.Y, axis.X);
                    float delta = WrapPi(want - cur);
                    float step = kTurnRate * dt;
                    Vec2 dir = System.MathF.Abs(delta) <= step
                        ? axis
                        : Rotate(new Vec2(System.MathF.Cos(cur), System.MathF.Sin(cur)),
                                 System.MathF.Sign(delta) * step);
                    g.SetComponent(new Velocity { V = dir * speed });
                    st.Rot = System.MathF.Atan2(dir.Y, dir.X);
                    g.SetComponent(st);
                    s.Steer -= dt;
                } else s.Steer = 0f;
            } else s.Steer = 0f; // 无速/目标已亡：余程直飞
            _swords[i] = s;
        }
    }

    /// 列阵：纵深方阵（行 = kRows 帘幕高度，列 = 深度）。baseIndex = 分帧
    /// 播种的累计序号（阵位连续，不因分帧错位）。
    private void SpawnFormation(uint team, float side, int count, int baseIndex)
    {
        uint tint = team == kRedTeam ? kRedTint : kBlueTint;
        uint enemy = team == kRedTeam ? kBlueTeam : kRedTeam;
        for (int i = 0; i < count; ++i) {
            int idx = baseIndex + i;
            float r1 = Rand(ref _lcg), r2 = Rand(ref _lcg), r3 = Rand(ref _lcg);
            float x = side * (kFrontGap + (idx / kRows) * kSpacing);
            float y = ((idx % kRows) - kRows * 0.5f) * kSpacing + (r2 - 0.5f) * 4f;
            SpawnUnit(team, enemy, tint, r3 < kAllyRatio, x, y, r1, r2, r3, side);
        }
        if (team == kRedTeam) { _redAlive += count; _redDeployed += count; }
        else { _blueAlive += count; _blueDeployed += count; }
    }

    private void SpawnUnit(uint team, uint enemy, uint tint, bool allyType,
                           float x, float y, float r1, float r2, float r3, float side)
    {
        // 两型共用英雄本体（玩家皮 + 行走动画）；差异在武器技能与体型
        GameObject g = Instantiate.Spawn(_heroSheetId, new Vec2(x, y));
        if (!g.Alive) return;

        var meta = g.GetComponent<Meta>();
        meta.Team = team;
        g.SetComponent(meta);

        var sr = g.GetComponent<SpriteRenderer>();
        sr.ColorRGBA = tint;
        g.SetComponent(sr);

        var tf = g.GetComponent<Transform2D>();
        tf.Scale = new Vec2(allyType ? 0.85f : 1.0f, allyType ? 0.85f : 1.0f);
        g.SetComponent(tf);

        // 行军：初速朝中 + Patrol 镜像穿中（a=列阵位 b=对侧镜像——大军持续
        // 穿越中线对流，引擎固定速 60；无 Chase = 零索敌开销，后排永远在推进）
        g.SetComponent(new Velocity { V = new Vec2(-side * 60f, 0f) });
        g.SetComponent(new Patrol {
            A = new Vec2(x, y), B = new Vec2(-x, y),
        });
        g.SetComponent(new Knockback { Decay = 8f });
        g.SetComponent(new Animator2D {
            ClipId = kHeroWalkClip, Speed = 0.9f + r3 * 0.3f, Loop = 1, PlayOnStart = 1,
        });

        if (allyType) {
            // 队友型（AllyBehaviour 战斗面）：飞剑技能——行军途中齐射
            // FlySword.prefab（穿透弹道；弧线由 Spawn 认领驱动）
            g.SetComponent(new Health { Max = 45f, Cur = 45f });
            g.SetComponent(new Shooter {
                ProjectileId = kFlySwordLow,
                Interval = 6.5f + r1 * 2.0f,
                Range = 360f,
                TargetTeam = enemy,
                Cooldown = r2 * 2.0f,
            });
        } else {
            // 玩家型（PlayerBehaviour 战斗面）：直射弹幕——Bullet.prefab
            // （伤害 8/命中即毁），行军途中点射；射速快于队友
            g.SetComponent(new Health { Max = 60f, Cur = 60f });
            g.SetComponent(new Shooter {
                ProjectileId = kBulletLow,
                Interval = 5.0f + r1 * 1.5f,
                Range = 320f,
                TargetTeam = enemy,
                Cooldown = r2 * 2.0f,
            });
        }
    }
}

