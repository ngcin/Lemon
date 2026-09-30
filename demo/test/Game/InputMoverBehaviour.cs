using Lemon;

/// <summary>方向键/WASD 移动（GameView 聚焦时输入进游戏）。</summary>
public sealed class InputMoverBehaviour : LemonBehaviour
{
    public const float Speed = 240f;

    protected override void Update()
    {
        var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();
        t.Pos = t.Pos + Lemon.Input.Axis * (Speed / 60f);
        gameObject.SetComponent(t);
    }
}
