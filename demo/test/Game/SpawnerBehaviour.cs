using Lemon;

/// <summary>在自身位置周期刷怪（spawn.png 种子资产；StateBag 热重载示例）。</summary>
public sealed class SpawnerBehaviour : LemonBehaviour
{
    private const string kSpriteGuid = "f5dbdb7729300e31";
    private int _tick;
    private uint _spriteId;

    protected override void Start()
        => _spriteId = Lemon.Assets.SpriteOf(kSpriteGuid);

    protected override void Update()
    {
        if (++_tick < 5 || _tick > 40) return;
        var t = gameObject.GetComponent<Lemon.Interop.Transform2D>();
        Lemon.Instantiate.Spawn(_spriteId,
            new Lemon.Vec2(t.Pos.X + 40 + _tick * 2, t.Pos.Y - 20));
    }

    protected override void OnHotReloadOut(StateBag bag)
    {
        bag.Set("tick", _tick);
        bag.Set("spriteId", _spriteId);
    }

    protected override void OnHotReloadIn(StateBag bag)
    {
        if (bag.TryGet("tick", out int t)) _tick = t;
        if (bag.TryGet("spriteId", out uint s)) _spriteId = s;
    }
}
