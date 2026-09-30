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
/// 调参：改 consts 后热重载 → Stop/Play 重播。</summary>
public sealed class DuelBehaviour : LemonBehaviour
{
    private const float kMoveSpeed = 90f;    // 行军速度（px/s）
    private const float kAttackRange = 170f; // 进攻击距（px）
    private const float kAttackCd = 0.9f;    // 攻击间隔（s；clip 12 帧 @14fps ≈ 0.86s）
    private const float kDamage = 18f;       // 单次打点伤害（A 血 140 / B 血 100 → B 先倒）

    private static readonly List<ulong> s_roster = new(); // 场上对决者（无全局遍历 API，静态名册配对）

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
            s_roster.Clear();
        if (!s_roster.Contains(gameObject.Entity.Id)) s_roster.Add(gameObject.Entity.Id);
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
        fh.Cur -= kDamage;
        foe.SetComponent(fh);
        Anim.Trigger(foe, fh.Cur <= 0f ? "death" : "hit");
        // 受击闪色（Tween.Color 演示，M6a 批② Tween A 档验收项）：底色为白、乘法
        // 混色下提亮不可见 → 闪红同语义（0.06s 闪入 + TweenFinished 回程 0.15s）
        Tween.Kill<SpriteRenderer>(foe, "colorRGBA");
        _flashHandle = Tween.Color(foe, 0xFF6060FFu, 0.06f);
        _flashFoe = _foe;
        Fx.Text(kDamage, new Vec2(ot.Pos.X, ot.Pos.Y - 50f), 0xFF5050FFu); // 伤害飘字
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
        // HUD 血条行（0.25s 节流；RtUi 通道，GameView 叠加）
        _hudT -= Time.DeltaTime;
        if (_hudT <= 0f) {
            _hudT = 0.25f;
            string foeHp = foeGo.Entity.Id != 0 && foeGo.Alive &&
                           foeGo.TryGetComponent<Health>(out var f2)
                       ? f2.Cur.ToString("F0") : "已倒下";
            Ui.Set("duel", $"对决  我方 {hp.Cur:F0} HP   对手 {foeHp} HP", hp.Cur / hp.Max,
                  0xFF30B0F0u);
        }
    }

    protected override void OnDestroy()
    {
        s_roster.Remove(gameObject.Entity.Id);
    }
}
