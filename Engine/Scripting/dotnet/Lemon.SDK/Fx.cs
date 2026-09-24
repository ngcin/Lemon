// Lemon.SDK — 世界空间表现通道（M6a 批①；06 §8 恒定原则：恒走 sprite 管线）------
// C# 写、视图侧（编辑器 GameView / M8 打包 HUD）读出：飘字 = 内置位图字体页
// （世界像素 1:1，寿命 0.8s 上浮 24px 末 30% 淡出）；血条 = 白精灵双四边形
// （按实体锚定、受击 sticky 3s 自隐、每帧刷新即常显；实体死亡自动不渲染）。
// 呈现层专用：不影响模拟状态哈希/回放（FxChannel 头说明）。
// 池化上限（引擎侧最老者淘汰）：飘字 256 / 血条 128——写超不炸、旧者让位。
// 典型用法（VS 模板受击）：
//   Fx.Text($"{damage:0}", pos, 0xFF5060F0);         // 伤害数字（暖红 RGBA）
//   Fx.Bar(enemy, hp.Cur / hp.Max, 0xFF30B0F0u);      // 受击显伤条
// 仅域线程 tick 期间有效（与 Native 表同窗口约定）。
using System;
using Lemon.Interop;

namespace Lemon;

public static class Fx
{
    /// <summary>世界空间飘字（伤害/治疗/短文本；16 字符截断、位图字体渲染）。
    /// pos = 首字符中心锚点（文本向右延伸，世界坐标）。高频命中场景直接打点即可
    /// ——池化接管。颜色 = RGBA uint（同 SpriteRenderer.colorRGBA；Lemon.Ui 的
    /// ABGR 惯例色不能直接搬——通道序不同）。</summary>
    public static void Text(string text, Vec2 pos, uint color = 0xFFF0F0F0u)
        => Native.FxPopup(text, pos.X, pos.Y, color);

    /// <summary>数字快路径（伤害整数最常用形态；内联格式省一次 string 拼接）。</summary>
    public static void Text(float number, Vec2 pos, uint color = 0xFFF0F0F0u)
        => Native.FxPopup(number.ToString("0"), pos.X, pos.Y, color);

    /// <summary>世界血条（按实体锚定：条画在实体位上方 2px，宽 width 世界像素）。
    /// 同实体重复调用 = 刷新比例续命（受击显伤条 3s 无刷新自隐；玩家条每帧
    /// 刷新即常显）；frac 钳 [0,1]；实体销毁后条不再渲染（槽等 sticky 过期回收）。</summary>
    public static void Bar(GameObject g, float frac, uint color = 0xFF30B0F0u,
                           float width = 32f)
    {
        if (g.Entity.IsNull) return;
        Native.FxBar(g.Entity.Id, frac, color, width);
    }
}
