using System;
using Lemon;
using Lemon.Interop;

/// <summary>昼夜循环（M8 批⑤）：`Lighting.DayNight` 首个真实消费者——Global 环境
/// 光的 color+intensity 双补间，晨→昼→暮→夜四关键帧环形链式推进（Once 单程
/// 补间无循环语义，脚本轮询 Tween.Alive 接力下一段）。挂 Grass 场景 Global 灯
/// 实体（scripts[]——脚本即灯实体，SDK 无按组件反查 API，定位 = 调用方职责）。
/// Light2D 字段入 StateHash ⇒ 补间可回放（ADR-019 M4；#L5 挂账批⑥ soak 前
/// 检查回放录制耗时）。纪律：**补间在途禁 SetColor/SetIntensity 同字段**——
/// 存活补间拥有字段，同帧脚本写会被覆写（Tween.cs 头注契约）。
/// 无头验证出口：每段发起写 Save:svr.daynight = 累计段数（svr.* 家族，
/// PlayerBehaviour.svr.mobhit 同款；--smoke --play 跑完读 .lemon/saves 档）。
/// 调参：kSeconds 缩短加速观察；关键帧表改色即改氛围——热重载后重发起当前段。</summary>
public sealed class DayNightCycle : LemonBehaviour
{
    // 四关键帧（RGBA + 强度；Global 灯 = lightRT 底色，intensity 即压暗程度）
    private static readonly uint[] kColors = {
        Lighting.Rgba(255, 236, 200), // 晨：暖白微黄
        Lighting.Rgba(255, 255, 255), // 昼：正白满亮
        Lighting.Rgba(255, 160, 96),  // 暮：橙红低角度
        Lighting.Rgba(96, 128, 200),  // 夜：冷蓝压暗
    };
    private static readonly float[] kIntensity = { 0.55f, 0.75f, 0.45f, 0.30f };
    private const float kSeconds = 20f; // 单段时长（s）——走查节奏（一轮 80s）
    private const Tween.Ease kEase = Tween.Ease.InOutQuad; // 段内缓入缓出 = 天色渐变

    private int _phase;   // 当前关键帧索引（Start 落帧；Update 补向下一帧）
    private int _count;   // 累计发起段数（svr.daynight 机器出口）
    private long _handle; // 在途段 intensity 补间句柄（0 = 无在途；等它即等双补间组）

    protected override void Start()
    {
        // 场景自带 Global 灯组件 = SetGlobalAmbient 即时整写（两拍坑仅在
        // Instantiate+Add 路径；显式置 Type/Color/Flags 绕镜像默认值三连）
        Lighting.SetGlobalAmbient(gameObject, kColors[0], kIntensity[0]);
    }

    protected override void Update()
    {
        if (_handle != 0 && Tween.Alive(_handle)) return; // 在途——同字段禁写
        _phase = (_phase + 1) & 3;                        // 上一段完（或热进重发起）
        _handle = Lighting.DayNight(gameObject, kColors[_phase], kIntensity[_phase],
                                    kSeconds, kEase);
        if (_handle == 0) return; // 组件缺失/旧宿主 no-op——下帧重试（不推进计数）
        ++_count;
        Save.SetString("svr.daynight", _count.ToString());
    }

    // 热重载：段索引/计数随包走；_handle 跨域不可靠——置 0 由下次 Update 自
    // 当前色值重发起（Tween 从现值起步，无视觉跳变）
    protected override void OnHotReloadOut(Lemon.StateBag bag)
    {
        bag.Set("phase", _phase);
        bag.Set("count", _count);
    }

    protected override void OnHotReloadIn(Lemon.StateBag bag)
    {
        if (bag.TryGet("phase", out int p)) _phase = p;
        if (bag.TryGet("count", out int c)) _count = c;
        _handle = 0;
    }
}
