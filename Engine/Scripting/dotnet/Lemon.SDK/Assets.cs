// Lemon.SDK — Assets 查询 + Instantiate/Spawn（M4-Editor-Plan §4-8 最小增量）
// Assets.SpriteOf：编辑器资产库 GUID → spriteId（纯运行时无资产库 = 0，调用方自查）。
// Instantiate：Spawn 就地建实体（当帧已构造的批量块不受影响，下帧对系统可见——
// 与 SceneOps 命令缓冲同一跨帧语义）；Prefab 走编辑器资产库（运行时打包后接
// .baked 资产缓存，06 §4，M7）。
using System;

namespace Lemon;

public static class Assets
{
    /// <summary>资产 GUID（16 位 hex 字符串，.meta 内 0x%016llx 形态）→ spriteId。</summary>
    public static uint SpriteOf(string guidHex) => Native.SpriteOfGuid(guidHex);
}

/// <summary>实体生成语法糖（Unity 心智；包装 M4.4 native spawn 通道）。</summary>
public static class Instantiate
{
    /// <summary>生成带 Transform+SpriteRenderer 的实体（spriteId=0 = 无渲染）。
    /// 返回的 GameObject 立即可用于低频读写（真实句柄，非占位）。</summary>
    public static GameObject Spawn(uint spriteId, Vec2 pos)
    {
        ulong id = Native.Spawn(spriteId, pos.X, pos.Y);
        return new GameObject(new Interop.EntityHandle { Id = id });
    }

    /// <summary>生成实体并挂脚本（typeId 由类名解析；未注册类型 = 不挂）。</summary>
    public static GameObject Spawn<T>(uint spriteId, Vec2 pos) where T : LemonBehaviour, new()
    {
        var g = Spawn(spriteId, pos);
        int typeId = Behaviours.TypeIdOf<T>();
        if (typeId >= 0) SceneOps.AttachScript(g.Entity, typeId);
        return g;
    }

    /// <summary>Prefab 资产实例化（编辑器资产库；guid 16 位 hex）。失败返回句柄 0 的
    /// GameObject（Alive=false，调用方自查）。</summary>
    public static GameObject Prefab(string assetGuidHex, Vec2 pos)
    {
        ulong id = Native.InstantiatePrefabGuid(assetGuidHex, pos.X, pos.Y);
        return new GameObject(new Interop.EntityHandle { Id = id });
    }
}
