using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>M6a 批② T3d 终验场（Scenes/ani.scene）：双怪对决——动画控制面全链展示。
///
/// 同一份 Duelist.controller（Idle/Walk/Attack/Hit/Death 词表）驱动两只素材与动画
/// 数量都不一致的角色：Monster01 只绑 Idle/Walk/Attack（受击/死亡状态缺绑 = 图
/// warn-once 保持当前状态，ADR-013 D4 的活样），Monster02 五状态全绑（Hurt 受击
/// 打断 + Dying 终态）。本脚本零 GUID 魔法常量——全部经 AnimGraph 绑定按名驱动：
///   Anim.SetParam(speed)      → Idle↔Walk 条件边（移动/停下）
///   Anim.Trigger(attack)      → Attack 态（攻击 clip 第 7 帧打点事件 → 出伤害）
///   Anim.Trigger(hit/death)   → 对手受击/死亡（M01 无绑原地，M02 播 Hurt/Dying）
/// 攻击命中判定 = 帧事件（GameEvent.AnimFrame，user=1）而非计时器——判定帧即
/// 表现帧，T3d 批③的语义演示。伤害/胜负数据落存档键 duel.*（无头验收出口）。
/// M7c 批①：Fx 表现升级验收演示场——暴击中文 Pop 弹跳黄字 + 贴图血条 +
/// 受击延迟白条三项全接（字体 = project.lemon fxFont 烘焙页，中文「暴击」可显）。
/// 调参：改 consts 后热重载 → Stop/Play 重播。</summary>
public sealed class DuelBehaviour : LemonBehaviour
{
    private const float kMoveSpeed = 90f;    // 行军速度（px/s）
    private const float kAttackRange = 170f; // 进攻击距（px）
    private const float kAttackCd = 0.9f;    // 攻击间隔（s；clip 12 帧 @14fps ≈ 0.86s）
    private const float kDamage = 18f;       // 单次打点伤害（A 血 140 / B 血 100 → B 先倒）

    // M7c 批①：贴图血条皮肤（Assets/bar_bg.png / bar_fg.png，64×10 → 高 = 宽×10/64）
    // + 延迟白条 + 25% 暴击率（数值层演示——伤害 ×1.5 与表现同源判定）
    private const double kCritChance = 0.25;
    private static readonly System.Random s_rng = new();

    /// <summary>每型 Fx 常数（走查轮④：头顶自动锚定按整帧高计，帧内透明边距会把
    /// 条/字悬空抬离可见头顶——按 Idle_000 alpha 包围盒实测下压）。可见头顶 =
    /// pos.y + headDy（Y 向下，负 = 上方）：
    ///   Monster01 帧 440×420 内容顶 57 → headDy = -210+57 = -153；条宽 140/高 22
    ///   Monster02 帧 600×480 内容顶 169 → headDy = -240+169 = -71；条宽 200/高 31
    /// skin.anchorDy = 引擎自动锚定的下压修正（+57/+169）；文字锚 = 头顶 - 2 - 条高
    /// - 行高(48×字号)（行底贴条上方）。</summary>
    private sealed class FxProfile
    {
        public FxBarSkin Skin; public float BarW, BarH, HeadDy;
    }

    private static readonly FxProfile kM01 = new()
    {
        Skin = new FxBarSkin("7e57000000000101", "7e57000000000102", 0xFFE8E8E8u, 22f, 57f),
        BarW = 140f, BarH = 22f, HeadDy = -153f,
    };
    private static readonly FxProfile kM02 = new()
    {
        Skin = new FxBarSkin("7e57000000000101", "7e57000000000102", 0xFFE8E8E8u, 31f, 169f),
        BarW = 200f, BarH = 31f, HeadDy = -71f,
    };

    // 场上对决者档案（无全局遍历 API，静态名册配对——s_roster 同款）
    private static readonly List<ulong> s_roster = new();
    private static readonly Dictionary<ulong, FxProfile> s_fx = new();
    private FxProfile? _mine;

    /// <summary>自身型档案（惰性——热重载恢复不经 Awake，字段态为空时按 team
    /// 现场判型补注册）。</summary>
    private FxProfile Mine
    {
        get
        {
            if (_mine != null) return _mine;
            if (s_fx.TryGetValue(gameObject.Entity.Id, out _mine)) return _mine;
            _mine = SelfProfile();
            s_fx[gameObject.Entity.Id] = _mine;
            return _mine;
        }
    }

    /// <summary>自型判定：DuelA(Monster01 140HP) team=1，DuelB(Monster02 100HP)
    /// team=3（ani.scene 常量；血量同型可复核）。</summary>
    private FxProfile SelfProfile()
        => gameObject.TryGetComponent<Meta>(out var meta) && meta.Team != 1 ? kM02 : kM01;

    private ulong _foe;
    private float _cd;
    private bool _dead;
    private float _hudT;
    private long _flashHandle;   // 在途的受击闪色补间（0 = 无）
    private ulong _flashFoe;     // 闪色目标（TweenFinished 回程用）

    protected override void Awake()
    {
        // 新局残留清场：名册已满且我不在其中 = 上局尸体（热重载走 StateBag 不经此）
        if (Time.FrameCount == 0 && s_roster.Count >= 2 && !s_roster.Contains(gameObject.Entity.Id))
        { s_roster.Clear(); s_fx.Clear(); }
        if (!s_roster.Contains(gameObject.Entity.Id))
        {
            s_roster.Add(gameObject.Entity.Id);
            s_fx[gameObject.Entity.Id] = SelfProfile();
        }
    }

    protected override void Start()
    {
        foreach (ulong id in s_roster)
            if (id != gameObject.Entity.Id) { _foe = id; break; }
        // 攻击打点（AnimFrame user=1）→ 出伤；订阅助手 OnDestroy 自动退订
        Subscribe(GameEvent.AnimFrame, m => {
            if (m.Src.Id == gameObject.Entity.Id && m.User == 1) Strike();
        });
        // 受击闪色回程：闪入补间（Once）完成事件 → 补回本色
        Subscribe(GameEvent.TweenFinished, m => {
            if (_flashFoe != 0 && (ulong)_flashHandle == m.UserArg) {
                Tween.Color(GameObject.From(new EntityHandle { Id = _flashFoe }),
                            0xFFFFFFFFu, 0.15f);
                _flashFoe = 0;
            }
        });
    }

    private void Strike()
    {
        if (_dead || _foe == 0) return;
        GameObject foe = GameObject.From(new EntityHandle { Id = _foe });
        if (!foe.Alive || !foe.TryGetComponent<Health>(out var fh) || fh.Cur <= 0f) return;
        if (!gameObject.TryGetComponent<Transform2D>(out var me) ||
            !foe.TryGetComponent<Transform2D>(out var ot)) return;
        float dx = ot.Pos.X - me.Pos.X, dy = ot.Pos.Y - me.Pos.Y;
        if (dx * dx + dy * dy > (kAttackRange + 60f) * (kAttackRange + 60f)) return; // 打点时对手已走远
        // M7c 批①：暴击判定数值层同源（×1.5 伤害 + Crit 糖表现——同一随机源）
        bool crit = s_rng.NextDouble() < kCritChance;
        float dmg = crit ? kDamage * 1.5f : kDamage;
        fh.Cur -= dmg;
        foe.SetComponent(fh);
        Anim.Trigger(foe, fh.Cur <= 0f ? "death" : "hit");
        // 受击闪色（Tween.Color 演示，M6a 批② Tween A 档验收项）：底色为白、乘法
        // 混色下提亮不可见 → 闪红同语义（0.06s 闪入 + TweenFinished 回程 0.15s）
        Tween.Kill<SpriteRenderer>(foe, "colorRGBA");
        _flashHandle = Tween.Color(foe, 0xFF6060FFu, 0.06f);
        _flashFoe = _foe;
        // M7c 批① 飘字：暴击 = 中文「暴击 N」黄字 Pop 弹跳 + 随机水平散布（Crit 糖
        // 现为真黄字 0xFF4AD2FF——RGBA 序，轮④ 修正）；常规 = 暖红伤害字。
        // 锚点 = 对手可见头顶上方的行顶（世界 Y 向下 = 负偏移；行底贴对手血条
        // 上方：头顶 - 2 - 条高 - 行高 48×字号，字号 ×2.5/×2.0 对齐本场景美术
        // 比例）。对手档案查静态名册（无档案 = 兜底自身型）
        FxProfile fp = s_fx.GetValueOrDefault(_foe, Mine);
        float barTop = fp.HeadDy - 2f - fp.BarH;
        if (crit)
            Fx.Crit($"暴击 {dmg:0}",
                    new Vec2(ot.Pos.X, ot.Pos.Y + barTop - 48f * 2.5f), 2.5f);
        else
            Fx.Text(dmg.ToString("0"),
                    new Vec2(ot.Pos.X, ot.Pos.Y + barTop - 48f * 2.0f),
                    0xFF5050FFu, new FxStyle(2.0f));
        // 贴图血条 + 延迟白条（对手型皮肤含帧边距 anchorDy 下压；受击刷新续命；
        // 每帧常显在 Update 节流段）
        Fx.Bar(foe, fh.Cur / fh.Max, 0xFF30B0F0u, fp.BarW, fp.Skin);
    }

    protected override void Update()
    {
        if (!gameObject.TryGetComponent<Health>(out var hp)) return;
        if (hp.Cur <= 0f) { // 死亡姿态：收手待机（Dying 一次播完钳末帧；M01 无绑保持原样）
            if (!_dead) {
                _dead = true;
                Anim.SetParam(gameObject, "speed", 0f);
                if (_foe != 0) Lemon.Save.SetString("duel.winner", _foe.ToString()); // 无头验收出口
            }
            return;
        }
        GameObject foeGo = default;
        if (_foe != 0) foeGo = GameObject.From(new EntityHandle { Id = _foe });
        if (_foe == 0 || !foeGo.Alive || !foeGo.TryGetComponent<Health>(out var fh) || fh.Cur <= 0f) {
            Anim.SetParam(gameObject, "speed", 0f); // 对手已亡/未配对：胜者收势待机
        } else if (!foeGo.TryGetComponent<Transform2D>(out var ot) ||
                   !gameObject.TryGetComponent<Transform2D>(out var t)) {
            return;
        } else {
            float dx = ot.Pos.X - t.Pos.X, dy = ot.Pos.Y - t.Pos.Y;
            float dist = System.MathF.Sqrt(dx * dx + dy * dy);
            if (dist > kAttackRange) {
                float inv = 1f / dist;
                t.Pos.X += dx * inv * kMoveSpeed * Time.DeltaTime;
                t.Pos.Y += dy * inv * kMoveSpeed * Time.DeltaTime;
                gameObject.SetComponent(t);
                Anim.SetParam(gameObject, "speed", 5f);
                if (gameObject.TryGetComponent<SpriteRenderer>(out var sr)) { // 朝向随移动方向
                    sr.Flags = dx < 0 ? (byte)(sr.Flags | 1) : (byte)(sr.Flags & ~1);
                    gameObject.SetComponent(sr);
                }
            } else {
                Anim.SetParam(gameObject, "speed", 0f);
                _cd -= Time.DeltaTime;
                if (_cd <= 0f) { Anim.Trigger(gameObject, "attack"); _cd = kAttackCd; }
            }
        }
        // HUD 血条行（0.25s 节流；RtUi 通道，GameView 叠加）+ 世界贴图血条常显
        // （M7c 批①：延迟白条在两次刷新间由引擎 Simulate 自行收敛——0.25s 节流
        // 恰好让收敛过程可观察）
        _hudT -= Time.DeltaTime;
        if (_hudT <= 0f) {
            _hudT = 0.25f;
            string foeHp = foeGo.Entity.Id != 0 && foeGo.Alive &&
                           foeGo.TryGetComponent<Health>(out var f2)
                       ? f2.Cur.ToString("F0") : "已倒下";
            Ui.Set("duel", $"对决  我方 {hp.Cur:F0} HP   对手 {foeHp} HP", hp.Cur / hp.Max,
                  0xFF30B0F0u);
            Fx.Bar(gameObject, hp.Cur / hp.Max, 0xFF60D060u, Mine.BarW, Mine.Skin);
            if (foeGo.Entity.Id != 0 && foeGo.Alive &&
                foeGo.TryGetComponent<Health>(out var f3)) {
                FxProfile fp2 = s_fx.GetValueOrDefault(foeGo.Entity.Id, Mine);
                Fx.Bar(foeGo, f3.Cur / f3.Max, 0xFF30B0F0u, fp2.BarW, fp2.Skin);
            }
        }
    }

    protected override void OnDestroy()
    {
        s_roster.Remove(gameObject.Entity.Id);
    }
}
