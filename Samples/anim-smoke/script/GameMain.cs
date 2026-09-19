// AnimSmoke — anim-smoke 用户脚本（档① FrameScript：NativeApi 读写 SpriteRenderer 换帧）
// 冒烟契约（与 main.cpp 两侧同步，改动须对齐）：
//   * Animator2D.ClipId = 动画页基 spriteId（16 帧连续 [base, base+16)）
//   * Animator2D.Speed == 0 标记"脚本自管帧"（C++ FrameMapSystem 据此跳过）
//   * 换帧 10fps：Update 累积 1/60s 固定步长，满 0.1s 推进一帧并在页内回绕（零分配：
//     只做 struct 拷贝 + 整型运算）
//   * 同步 CurFrame 镜像（M5 clip 表落地前帧号由脚本自管）
using Lemon;
using Lemon.Interop;

public static class GameMain
{
    public static void Configure()
    {
        Behaviours.Register<FrameScript>(); // typeId 0
    }
}

/// <summary>帧推进（档①）：每 1/10s 把 SpriteId 前推一帧并在动画页内回绕。</summary>
public sealed class FrameScript : LemonBehaviour
{
    private float _acc;

    protected override void Update()
    {
        _acc += 1.0f / 60.0f;
        if (_acc < 0.1f) return;
        _acc -= 0.1f;
        var an = gameObject.GetComponent<Animator2D>();
        var sr = gameObject.GetComponent<SpriteRenderer>();
        uint frame = (sr.SpriteId - an.ClipId + 1u) % 16u;
        sr.SpriteId = an.ClipId + frame;
        an.CurFrame = (ushort)frame;
        gameObject.SetComponent(in sr);
        gameObject.SetComponent(in an);
    }
}
