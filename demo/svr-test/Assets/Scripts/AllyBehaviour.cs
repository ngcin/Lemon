using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>队友英雄（跟班）：贴身跟随玩家 + 引擎 Shooter 自动索敌发射飞剑
/// （FlySword.prefab：穿透弹道）+ 为本队飞剑驱动朝向、弧线弹道与粒子拖尾。
///
/// 弹道：飞剑出膛即扇形偏转（数量越多张角越宽，单发也有固定侧旋），出膛后有限
/// 预算内朝目标转向（kSteerTime/kTurnRate）——弧线扑靶后余程直飞穿透；引擎弹道
/// 本体零改动（homing=0 直线积分，转向由本脚本改写 Velocity 实现，弹速守恒）。
///
/// 认领链：预制体 Play 中途实例化不挂脚本（ResolvePlayScripts 仅 EnterPlay 一次），
/// 故飞剑表现层由本脚本经 GameEvent.Spawn（Dst=射手）认领代驱；齐射追加剑
/// （ExtraSwords，升级"飞剑 +1"）在认领时刻以引擎剑为轴克隆，共享同一段转向/拖尾。
/// 拖尾光点挂中立队（team 2）不入敌对目标板（怪 AI 不追光点）。
/// 零 RNG（偏转角查表式确定），升级卡片冻结（dt=0）时移动/转向/拖尾同停。</summary>
public sealed class AllyBehaviour : LemonBehaviour
{
    // 模板资产 GUID（与 svr-test Assets/Prefabs 内 .meta 对账，勿改）
    internal const string kHeroSheet = "5bd31a7c10000001";     // dungeon_hero_1.png
    internal const string kBladePng = "7e57000000000004";      // 拖尾光点源（青白）
    internal const string kFlySwordPng = "7e570000000000a1";   // 指向性飞剑贴图
    internal const string kFlySwordPrefab = "7e571000000000a1"; // FlySword.prefab
    internal const uint kFlySwordPrefabLow = 0x000000A1;       // FlySword.prefab 低 32 位
    internal const uint kHeroWalkClip = 536870913u;            // hero-walk.clip 低 32 位

    private const float kFollowSpeed = 150f; // 跟随速度（> 玩家基础移速，掉队可追回）
    private const float kFollowGap = 56f;    // 贴身距离（小于此不再逼近）
    private const float kSnapDist = 640f;    // 脱离过远直接就位（防追不上玩家移速升级）
    private const float kTrailInterval = 0.03f; // 拖尾光点发射间隔秒
    private const float kTrailLife = 0.32f;     // 光点寿命秒
    private const int kMaxPuffs = 96;           // 光点全局上限（预算护栏）
    private const byte kSrFlipX = 0x01;

    // ---- 弧线弹道（表现层；改写 Velocity，弹速取 prefab Projectile.speed 守恒）----
    internal const int kMaxExtraSwords = 20;   // 齐射追加剑上限（升级"飞剑 +1"）
    private const float kArcSwirl = 0.7f;     // 固定侧旋（rad，奇偶交替正负——单发也带弧）
    private const float kArcFan = 0.35f;      // 齐射扇形相邻步进（rad）
    private const float kTurnRate = 4.0f;     // 扑靶转向角速度（rad/s）
    private const float kSteerTime = 0.6f;    // 转向预算秒（耗尽/对齐后余程直飞）

    /// <summary>玩家锚点（PlayerBehaviour.Start/EnsureAlly 重设；热重载换域后随新实例恢复）。</summary>
    internal static EntityHandle Player;

    /// <summary>齐射追加剑数量（升级"飞剑 +1"写；PlayerBehaviour 持有并经 StateBag 迁移）。</summary>
    internal static int ExtraSwords;

    /// <summary>在飞飞剑状态（认领序；Target=开火时刻引擎 Shooter.target）。</summary>
    private struct SwordState
    {
        public ulong Id, Target;
        public float Steer; // 剩余转向预算秒（0 = 直飞）
    }

    private readonly List<SwordState> _swords = new();
    private readonly List<ulong> _puffs = new();
    private readonly List<float> _puffAge = new();
    private float _trailAcc;
    private uint _swordSprite; // 0 = 资产未命中（保留预制体兜底贴图）
    private uint _puffSprite;
    private bool _moving;

    public AllyBehaviour()
    {
        // 认领本队开火的飞剑（AISystem Spawn 事件：Src=弹体，Dst=射手；开火同帧
        // sh.target 刚写入 = 本轮齐射共同目标），并以引擎剑为轴克隆齐射追加剑
        Subscribe(GameEvent.Spawn, m => {
            if (m.Dst.Id != gameObject.Entity.Id) return;
            var sword = GameObject.From(m.Src);
            if (!sword.TryGetComponent<Transform2D>(out var s0)) return;

            // 瞄准轴：引擎剑刚被 AISystem 写入朝目标初速；活目标优先（更贴当前方位）
            Vec2 axis = Vec2.Zero;
            if (sword.TryGetComponent<Velocity>(out var sv)) {
                float len = Length(sv.V);
                if (len > 1f) axis = sv.V / len;
            }
            ulong target = 0;
            var sh = gameObject.GetComponent<Shooter>();
            if (!sh.Target.IsNull &&
                GameObject.From(sh.Target).TryGetComponent<Transform2D>(out var tt)) {
                target = sh.Target.Id;
                Vec2 toT = tt.Pos - s0.Pos;
                float d = Length(toT);
                if (d > 1f) axis = toT / d;
            }

            // 无有效轴（目标与弹道双缺失）：只认领引擎剑原速直飞，不做齐射
            float axisLen = Length(axis);
            if (axisLen <= 0.5f) {
                _swords.Add(new SwordState { Id = sword.Entity.Id, Steer = 0f });
                return;
            }
            axis /= axisLen;

            int count = 10 + System.Math.Clamp(ExtraSwords, 0, kMaxExtraSwords);
            for (int i = 0; i < count; ++i) {
                GameObject g = sword;
                if (i > 0) { // 齐射克隆：同 prefab（弹速/穿透/命中同源），出生点即 muzzle
                    g = Instantiate.Prefab(kFlySwordPrefab, s0.Pos);
                    if (!g.Alive) continue;
                }
                // 出膛即偏转：扇形展开 + 奇偶侧旋（单发也带弧），转向预算内扑向目标
                float fan = (i - (count - 1) * 0.5f) * kArcFan;
                float swirl = (i % 2 == 0 ? 1f : -1f) * kArcSwirl;
                Vec2 dir = Rotate(axis, fan + swirl);
                if (g.TryGetComponent<Projectile>(out var p))
                    g.SetComponent(new Velocity { V = dir * p.Speed });
                _swords.Add(new SwordState { Id = g.Entity.Id, Target = target,
                                             Steer = kSteerTime });
            }
        });
    }

    protected override void Awake()
    {
        _swordSprite = Assets.SpriteOf(kFlySwordPng);
        _puffSprite = Assets.SpriteOf(kBladePng);

        var tf = gameObject.GetComponent<Transform2D>();
        tf.Scale = new Vec2(0.85f, 0.85f); // 比玩家略小一号（跟班观感）
        gameObject.SetComponent(tf);

        var sr = gameObject.GetComponent<SpriteRenderer>();
        sr.SpriteId = Assets.SpriteOf(kHeroSheet);
        sr.ColorRGBA = 0xFFFFD0B0u; // 微冷色相区分玩家（ABGR：RGB 176,208,255）
        gameObject.SetComponent(sr);

        // Spawn 通道只带 Transform+SpriteRenderer+Meta（team 0），动画与开火在此补挂
        gameObject.SetComponent(new Animator2D {
            ClipId = kHeroWalkClip, Speed = 1f, Loop = 1, PlayOnStart = 1,
        });
        gameObject.SetComponent(new Shooter {
            ProjectileId = kFlySwordPrefabLow,
            Interval = 0.9f,
            Range = 400f,
            TargetTeam = 1,
            Cooldown = 0.6f, // 落位后稍候首射
        });
    }

    protected override void Update()
    {
        float dt = Time.DeltaTime;
        var tf = gameObject.GetComponent<Transform2D>();
        _moving = false;

        // ---- 跟随：贴身保持 kFollowGap，脱离过远直接就位 ----
        var player = GameObject.From(Player);
        if (player.Alive && player.TryGetComponent<Transform2D>(out var pt)) {
            float dx = pt.Pos.X - tf.Pos.X, dy = pt.Pos.Y - tf.Pos.Y;
            float d = System.MathF.Sqrt(dx * dx + dy * dy);
            if (d > kSnapDist) {
                tf.Pos = pt.Pos + new Vec2(-42f, -30f);
            } else if (d > kFollowGap && dt > 0f) {
                float k = System.MathF.Min(1f, kFollowSpeed * dt / d);
                tf.Pos = new Vec2(tf.Pos.X + dx * k, tf.Pos.Y + dy * k);
                _moving = true;
            }
            var sr = gameObject.GetComponent<SpriteRenderer>();
            if (dx < -1f) sr.Flags |= kSrFlipX;             // 向左走翻面
            else if (dx > 1f) sr.Flags &= unchecked((byte)~kSrFlipX);
            gameObject.SetComponent(sr);
        }
        gameObject.SetComponent(tf);

        var an = gameObject.GetComponent<Animator2D>();
        an.Speed = _moving ? 1.2f : 0.4f; // 走路帧速/待机慢摆
        gameObject.SetComponent(an);

        UpdateSwords(dt);
        UpdatePuffs(dt);
        Ui.Set("ally", $"✦ 队友 在线 · 飞剑 ×{_swords.Count}", -1f, 0xFF50E0B0u);
    }

    private void UpdateSwords(float dt)
    {
        _swords.RemoveAll(s => !GameObject.From(new EntityHandle { Id = s.Id }).Alive);
        // 拖尾节拍：全队飞剑同一节拍发射（每剑一颗），冻结（dt=0）时自然停拍
        _trailAcc += dt;
        bool emit = _trailAcc >= kTrailInterval;
        if (emit) _trailAcc = 0f;

        for (int i = 0; i < _swords.Count; ++i) {
            var g = GameObject.From(new EntityHandle { Id = _swords[i].Id });
            if (!g.TryGetComponent<Transform2D>(out var st)) continue;

            SwordState s = _swords[i];
            Vec2 vel = g.TryGetComponent<Velocity>(out var v) ? v.V : Vec2.Zero;

            // 弧线扑靶：预算内以 kTurnRate 朝目标当前方位转向（对齐/耗尽/目标亡 → 直飞）
            if (s.Steer > 0f && dt > 0f) {
                s.Steer -= dt;
                float speed = Length(vel);
                var tgt = GameObject.From(new EntityHandle { Id = s.Target });
                if (speed > 1f && s.Target != 0 &&
                    tgt.TryGetComponent<Transform2D>(out var tt)) {
                    Vec2 toT = tt.Pos - st.Pos;
                    float d = Length(toT);
                    if (d > 1f) {
                        Vec2 axis = toT / d;
                        float cur = System.MathF.Atan2(vel.Y, vel.X);
                        float want = System.MathF.Atan2(axis.Y, axis.X);
                        float delta = WrapPi(want - cur);
                        float step = kTurnRate * dt;
                        Vec2 dir = System.MathF.Abs(delta) <= step
                            ? axis // 对齐：余程直飞（穿透线）
                            : Rotate(new Vec2(System.MathF.Cos(cur), System.MathF.Sin(cur)),
                                     System.MathF.Sign(delta) * step);
                        g.SetComponent(new Velocity { V = dir * speed });
                        vel = dir * speed;
                    } else s.Steer = 0f;
                } else s.Steer = 0f; // 无速/目标已亡：余程直飞
                _swords[i] = s;
            }

            if (vel.X != 0f || vel.Y != 0f) {
                st.Rot = System.MathF.Atan2(vel.Y, vel.X); // 弹头指向飞行方向
                g.SetComponent(st);
            }
            if (_swordSprite != 0) { // 预制体兜底贴图 → 指向性飞剑（认领后一次性改）
                var sr = g.GetComponent<SpriteRenderer>();
                if (sr.SpriteId != _swordSprite) {
                    sr.SpriteId = _swordSprite;
                    g.SetComponent(sr);
                }
            }
            if (emit) {
                Vec2 off = Vec2.Zero;
                float len = Length(vel);
                if (len > 1f) off = vel / len * -7f; // 尾迹起点后撤（确定式，零 RNG）
                SpawnPuff(st.Pos + off);
            }
        }
    }

    private static float Length(Vec2 v) => System.MathF.Sqrt(v.X * v.X + v.Y * v.Y);

    private static Vec2 Rotate(Vec2 v, float rad)
    {
        float c = System.MathF.Cos(rad), s = System.MathF.Sin(rad);
        return new Vec2(v.X * c - v.Y * s, v.X * s + v.Y * c);
    }

    /// <summary>夹到 (-π, π]：转向取最短弧（不绕远圈）。</summary>
    private static float WrapPi(float a)
    {
        while (a > System.MathF.PI) a -= 2f * System.MathF.PI;
        while (a <= -System.MathF.PI) a += 2f * System.MathF.PI;
        return a;
    }

    private void SpawnPuff(Vec2 pos)
    {
        if (_puffSprite == 0 || _puffs.Count >= kMaxPuffs) return;
        var g = Instantiate.Spawn(_puffSprite, pos);
        if (!g.Alive) return;
        var m = g.GetComponent<Meta>();
        m.Team = 2; // 中立：不入敌对目标板（怪 AI 不追光点；弹幕命中掩码也不含 2）
        g.SetComponent(m);
        var sr = g.GetComponent<SpriteRenderer>();
        sr.ColorRGBA = 0xF0FFF8E0u; // 青白（ABGR：RGB 224,248,255）
        sr.SortOrder = 1;           // 压角色、垫飞剑（飞剑 sortOrder=2）
        g.SetComponent(sr);
        var tf = g.GetComponent<Transform2D>();
        tf.Scale = new Vec2(0.55f, 0.55f);
        g.SetComponent(tf);
        _puffs.Add(g.Entity.Id);
        _puffAge.Add(0f);
    }

    private void UpdatePuffs(float dt)
    {
        for (int i = _puffs.Count - 1; i >= 0; --i) {
            float age = _puffAge[i] + dt;
            var g = GameObject.From(new EntityHandle { Id = _puffs[i] });
            float t = age / kTrailLife;
            if (t >= 1f || !g.Alive) {
                if (g.Alive) g.Destroy();
                _puffs.RemoveAt(i);
                _puffAge.RemoveAt(i);
                continue;
            }
            _puffAge[i] = age;
            float inv = 1f - t;
            var sr = g.GetComponent<SpriteRenderer>();
            sr.ColorRGBA = ((uint)(0xF0 * inv) << 24) | 0x00FFF8E0u; // alpha 渐隐
            g.SetComponent(sr);
            var tf = g.GetComponent<Transform2D>();
            float s = 0.55f * inv + 0.12f; // 收缩
            tf.Scale = new Vec2(s, s);
            g.SetComponent(tf);
        }
    }

    // 热重载：飞剑/光点清单不迁移（域重建后余弹 ≤1.8s 自然消亡，无拖尾属可接受降级）；
    // 实体本体不销毁，PlayerBehaviour 侧 Alive 自愈不重刷
    protected override void OnHotReloadOut(StateBag bag) { }
    protected override void OnHotReloadIn(StateBag bag) { }
}
