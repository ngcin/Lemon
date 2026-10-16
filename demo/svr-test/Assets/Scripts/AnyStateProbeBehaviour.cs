using System;
using Lemon;
using Lemon.Interop;

/// <summary>批⑪ any-state 运行时验收场（Scenes/anytest.scene）。
///
/// 单实体挂 Duelist.controller（用户手档 8 边拓扑：any→Death 文件序第一 /
/// any→Hit 第二 + Idle↔Walk/Attack 状态边 + Attack/Hit 段末回 Idle）+
/// Monster02.ani（五段与状态名一一全绑——缺绑态会 warn-once 保持原样，选全绑
/// 才能断言切换）。时间线自动驱动 + 逐相断言，无头可跑：
///   ① 入口 = Idle（entry）
///   ② speed=5 → Walk（状态边基线）
///   ③ attack 触发 → Attack（状态边基线）
///   ④ Attack 段中（命中确认帧立即发）hit 触发 → Hit —— **any 边全局中断，
///      本场核心断言**：段中打断 = 比 exitTime 早即证
///   ⑤ Hit 段末 → Idle（exitTime 回位）
///   ⑥ death 触发 → Death（any 边，终态驻留）
///   ⑦ 观察（不计分）：Death 中再发 hit → 拓扑事实 = any→Hit 对 Death 也生效；
///      "死亡中不受击"属伤害脚本代码侧门（Unity 同款惯例），非图职责
/// 段 hex = Monster02.ani 五段 clip GUID（与 DuelBehaviour 血条皮肤同款
/// hex 引用风格，非裸魔法数）。验收出口 = [anytest] 结论行 + 存档键
/// anytest.verdict（DuelBehaviour duel.winner 同款无头口径）。</summary>
public sealed class AnyStateProbeBehaviour : LemonBehaviour
{
    private static readonly uint kIdle = Anim.ClipId("aa02000000000201");
    private static readonly uint kWalk = Anim.ClipId("aa02000000000202");
    private static readonly uint kAttack = Anim.ClipId("aa02000000000203");
    private static readonly uint kHit = Anim.ClipId("aa02000000000204");
    private static readonly uint kDeath = Anim.ClipId("aa02000000000205");

    private enum Phase { WaitIdle, WaitWalk, WaitAttack, WaitHit, WaitBackIdle, WaitDeath, Observe, Done }

    private Phase _phase = Phase.WaitIdle;
    private float _t;      // 相内计时
    private int _pass, _total = 6;
    private string _failAt = "";

    private static string Name(uint clipId) => clipId == kIdle ? "Idle" : clipId == kWalk ? "Walk"
        : clipId == kAttack ? "Attack" : clipId == kHit ? "Hit" : clipId == kDeath ? "Death"
        : "clip " + clipId.ToString("X8");

    private void Next() { _phase = (Phase)((int)_phase + 1); _t = 0f; }

    private void Ok(string what)
    {
        _pass++;
        Console.WriteLine($"[anytest] PASS {_pass}/{_total} {what}");
    }

    private void Fail(string what)
    {
        _failAt = what;
        Console.WriteLine($"[anytest] FAIL {what}（当前 {Name(Cur())}）");
        Verdict();
        _phase = Phase.Done;
    }

    private void Verdict()
    {
        bool ok = _failAt.Length == 0;
        Console.WriteLine($"[anytest] any-state 运行时验收 => {(ok ? "PASS" : "FAIL")} " +
                          $"({_pass}/{_total}{(ok ? "" : " @" + _failAt)})");
        Lemon.Save.SetString("anytest.verdict", ok ? "PASS" : "FAIL:" + _failAt);
    }

    private uint Cur()
        => gameObject.TryGetComponent<Animator2D>(out var an) ? an.ClipId : 0u;

    protected override void Update()
    {
        _t += Time.DeltaTime;
        if (_phase == Phase.Done) return;
        uint cur = Cur();
        switch (_phase)
        {
            case Phase.WaitIdle:
                if (cur == kIdle)
                {
                    Ok("入口 = Idle（entry 词面缺省 states[0]）");
                    Next();
                    Anim.SetParam(gameObject, "speed", 5f);
                }
                else if (_t > 1f) Fail("入口应为 Idle");
                break;
            case Phase.WaitWalk:
                if (cur == kWalk)
                {
                    Ok("speed=5 → Walk（状态条件边）");
                    Next();
                    Anim.Trigger(gameObject, "attack");
                }
                else if (_t > 0.5f) Fail("speed=5 未切 Walk");
                break;
            case Phase.WaitAttack:
                if (cur == kAttack)
                {
                    Ok("attack 触发 → Attack（状态 trigger 边）");
                    Next();
                    // 命中确认帧立即发 = 稳处 Attack 段中（远离段末），中断才可证
                    Anim.Trigger(gameObject, "hit");
                }
                else if (_t > 0.5f) Fail("attack 未切 Attack");
                break;
            case Phase.WaitHit:
                if (cur == kHit)
                {
                    Ok("Attack 段中 hit 触发 → Hit（any 边全局中断——核心断言）");
                    Next();
                }
                else if (_t > 0.5f) Fail("any→Hit 未中断 Attack");
                break;
            case Phase.WaitBackIdle:
                if (cur == kIdle)
                {
                    Ok("Hit 段末 → Idle（exitTime 回位）");
                    Next();
                    Anim.Trigger(gameObject, "death");
                }
                else if (_t > 6f) Fail("Hit 未按 exitTime 回 Idle");
                break;
            case Phase.WaitDeath:
                if (cur == kDeath)
                {
                    Ok("death 触发 → Death（any 边，终态驻留）");
                    Next();
                    Anim.Trigger(gameObject, "hit"); // ⑦ 观察位：死亡中再受击
                }
                else if (_t > 0.5f) Fail("any→Death 未切");
                break;
            case Phase.Observe:
                if (_t > 0.6f)
                {
                    Console.WriteLine($"[anytest] 观察（不计分）：Death 中再发 hit → {Name(cur)}——" +
                                      "any→Hit 对 Death 同样生效（拓扑事实）；'死亡中不受击' = 伤害脚本" +
                                      "代码侧门职责（Unity 同款惯例），图不越权");
                    Verdict();
                    Next();
                }
                break;
        }
    }
}
