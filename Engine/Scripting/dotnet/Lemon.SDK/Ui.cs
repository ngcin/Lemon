// Lemon.SDK — Game RT UI 通道（M5 批① D6 建立最小形态；M5 批④ 完整版）----------
// C# 写、宿主读（编辑器 GameView 叠加；M8 打包 HUD 同通道）。World 级定长 8 槽：
// 命中 key 覆写、空槽即占、满槽忽略；text 截断 47 字符。呈现层专用，不影响模拟
// 状态哈希/回放。
// 批④ 增量：着色 Set（ABGR uint，0 = 默认）/ Clear（删单行）/ 三选一卡片
// （ShowCards + CardPick 消费式回读；选择属用户 IO 不入输入快照，09 §7 分类）。
// 典型用法（VS 模板）：
//   Ui.Set("hp", "68/100", 0.68f, 0xFF30B0F0);     // 血条（着色 + 进度条）
//   Ui.ShowCards("升级三选一", "移速 +10%", "磁力 +25%", "射速 +20%");
//   int pick = Ui.CardPick();                       // 每帧轮询；-1 = 未选
namespace Lemon;

public static class Ui
{
    /// <summary>写一行 HUD。key 稳定即同行覆写（如 "xp"）；frac ∈ [0,1] 附进度条，<0 纯文本。
    /// 仅域线程 tick 期间有效（与 Native 表同窗口约定）。</summary>
    public static void Set(string key, string text, float frac = -1f)
        => Native.UiSet(key, text, frac);

    /// <summary>着色版（color = ABGR uint，0 = 默认色；文本与进度条同色）。
    /// 模板惯例：血条红 0xFF30B0F0 / 经验金 0xFF30D8F0 / 计时白 0xFFF0F0F0。</summary>
    public static void Set(string key, string text, float frac, uint color)
        => Native.UiSetColored(key, text, frac, color);

    /// <summary>删一行 HUD（结算后清屏）。key 不存在 = no-op。</summary>
    public static void Clear(string key)
        => Native.UiClear(key);

    /// <summary>显示三选一卡片（覆盖式）。建议先 Time.Scale = 0 冻结模拟，
    /// 选完恢复——卡片期间玩家静止是 VS 类标准节奏。</summary>
    public static void ShowCards(string title, string optionA, string optionB,
                                 string optionC)
        => Native.UiCards(true, title, optionA, optionB, optionC);

    /// <summary>显示单按钮对话框（卡片通道复用：B/C 留空即不渲染按钮）。
    /// 确认型交互首选——死亡复活/结算重开等；点击或数字键 1 触发 CardPick() == 0
    /// （批④后修④：取代模板 R 键复活，交互不依赖键盘焦点路由）。</summary>
    public static void ShowDialog(string title, string okLabel)
        => Native.UiCards(true, title, okLabel, "", "");

    /// <summary>隐藏卡片（选完/超时）。</summary>
    public static void HideCards()
        => Native.UiCards(false, "", "", "", "");

    /// <summary>轮询已选卡片（消费式：读后即清，同一选择只回报一次）。
    /// -1 = 未选。宿主侧来源 = 卡片按钮点击 / GameView 聚焦时数字键 1/2/3。</summary>
    public static int CardPick()
        => Native.UiCardPick();
}
