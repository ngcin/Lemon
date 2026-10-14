using Lemon;
using Lemon.Interop;

/// <summary>移动（M6a 批⓪ T4 拆分）：8 向 + 软竞技场钳制；死亡相位停走。</summary>
public sealed class PlayerMovement : LemonBehaviour
{
    private const float kArenaHalf = 1000f; // 软竞技场边界（脚本层钳制）

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var tf = gameObject.GetComponent<Transform2D>();
        var stats = gameObject.GetComponent<Stats>();
        Vec2 axis = Input.Axis;
        tf.Pos = new Vec2(
            System.Math.Clamp(tf.Pos.X + axis.X * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf),
            System.Math.Clamp(tf.Pos.Y + axis.Y * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf));
        gameObject.SetComponent(tf);
    }
}
