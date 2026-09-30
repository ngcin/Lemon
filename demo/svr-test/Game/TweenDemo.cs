using System;
using Lemon;
using Lemon.Interop;

/// <summary>ani.scene 专用：Tween.Scale OutBack 弹跳演示（M6a 批② Tween A 档真人
/// 验收载体——ani.scene 无拾取玩法，宝石周期性"弹出"模拟拾取物弹跳手感）。
/// gem.png 素材 16×16：待机 4×（64px，与像素怪观感一致），每 kInterval 秒缩到
/// 1× 再 OutBack 弹回 4×（过冲 ≈ 4.4× 即"弹"）。OutBack 过冲回落即"弹"。
/// 调 consts 后热重载 → Stop/Play 重播生效。</summary>
public sealed class TweenDemo : LemonBehaviour
{
    private const float kInterval = 4f;  // 弹跳间隔（s）
    private const float kFrom = 1f;      // 起始缩放（16px）
    private const float kTo = 4f;        // 终缩放（64px）
    private const float kDur = 0.6f;     // 单次时长（s）

    private float _t;
    private int _popCount;

    protected override void Start()
    {
        Console.WriteLine($"[tween-demo] attached entity={gameObject.Entity.Id}");
        Shrink();
        _t = kInterval; // 首个 Update 即弹第一次
    }

    protected override void Update()
    {
        _t += Time.DeltaTime;
        if (_t < kInterval) return;
        _t = 0f;
        Shrink();
        long h = Tween.Scale(gameObject, kTo, kDur, Tween.Ease.OutBack);
        ++_popCount;
        // handle=0 = 建链失败 no-op（排查口）；非 0 = 补间已建，引擎侧接管
        Console.WriteLine($"[tween-demo] pop #{_popCount} handle={h}");
    }

    private void Shrink()
    {
        if (!gameObject.TryGetComponent<Transform2D>(out var t)) return;
        t.Scale = new Vec2(kFrom, kFrom);
        gameObject.SetComponent(t);
    }
}
