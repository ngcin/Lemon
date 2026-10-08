// Lemon 引擎 — 世界空间表现通道（M6a 批①；06 §8 恒定原则：血条/飘字恒走 sprite
// 管线、不进 UI 框架；03 §10 池化纪律——"飘血数字"预列项落账）
// World 级第三呈现通道（RtUi/Cards 同款）：C# Lemon.Fx / C++ harness 写入，
// 视图侧（编辑器 GameView / M8 打包 HUD 同通道）读出产 sprite 包。
// 呈现层语义：Simulate 按渲染帧 dt（粒子先例——非确定可接受，不入 03 §12 确定性
// 回放）；不入 StateHash（ComputeStateHash 只读 Scene）；EnterPlay 新建 World 自清零。
// 零 Renderer 依赖：血条产包 = 纯 FxQuad 描述子（视图侧转 SpritePacket，引擎测试
// 可直测数学）；飘字产包在视图侧（字形表是 BitmapFont 的 GPU 态）。
// M7c 批① S2/S3：血条贴图化（bg/fg sprite + UV 横向裁剪 + 延迟条 lagFrac 收敛）
// 与飘字动效参数化（scale/life/driftX/Pop 曲线）——全部尾加字段，旧调用零迁移。
#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "Core/Math.h"

namespace lemon::ecs {

/// 飘字轨迹曲线（S3）：Linear = 现状匀升；Pop = 出生 1.4× 回落 1.0 + ease-out
/// 陡升上浮（暴击弹跳）
enum class FxCurve : uint8_t { Linear = 0, Pop = 1 };

/// 飘字条目（伤害/治疗/短文本；16 字节截断——位图字体页渲染，06 §8；多字节
/// UTF-8 截断尾由解码器容错丢弃）
struct FxText {
    char text[16] = {};
    float x = 0.0f, y = 0.0f; // 世界坐标锚点 = 首字符中心（文本向右延伸）
    uint32_t color = 0xFFFFFFFFu;
    float age = 0.0f;         // Simulate 递增；≥life 释放
    // ---- S3 尾加（默认值 = M6a 现状行为）----
    float scale = 1.0f;       // 字号缩放
    float life = 0.0f;        // 寿命秒（0 = 默认 kTextLife）
    float driftX = 0.0f;      // 水平恒速漂移（px/s；暴击散布用）
    FxCurve curve = FxCurve::Linear;
};

/// 世界血条条目（按实体键控；受击显伤条 sticky 自隐、每帧刷新即常显）
struct FxBar {
    uint64_t entity = 0;      // 0 = 空槽；实体死亡由渲染侧跳过（不提前释放）
    float frac = 1.0f;        // [0,1]
    uint32_t color = 0xFF30B0F0u; // RGBA 序（r|g<<8|b<<16|a<<24——同 SpriteRenderer.colorRGBA；注意 Lemon.Ui 是 ImGui 的 ABGR 序，勿互拷常量）
    float age = 0.0f;         // 距最近一次刷新；≥kBarSticky 释放
    float width = 32.0f;      // 世界像素（fg 宽 = frac×width 左锚）
    // ---- S2 尾加（默认值 = 白精灵现状路径，向后兼容零迁移）----
    uint32_t bgSpriteId = 0;  // 背板贴图（0 = 白精灵染色）
    uint32_t fgSpriteId = 0;  // 前景贴图（0 = 白精灵染色；贴图形态下 fg 按横向 UV 裁剪）
    uint32_t lagColor = 0;    // 延迟条配色（0 = 无延迟条；RGBA）
    float height = 0.0f;      // 条高（0 = kBarHeight 默认 4px）
    float lagFrac = 0.0f;     // 延迟条比例（Simulate 维护向 frac 线性收敛；表现层语义）
    float anchorDy = 0.0f;    // 锚点修正（世界 px，+ = 下移）——头顶自动锚定按
                              // 精灵整帧高计，帧内透明边距会把条悬空抬离可见
                              // 头顶（实测 440×420 帧 57px / 600×480 帧 169px），
                              // 游戏侧按美术帧实测边距给正值下压
};

/// 血条四边形描述子（bg 整宽 + lag/fg 比例宽；视图侧 → SpritePacket）
struct FxQuad {
    Vec2 center;
    Vec2 size;     // 世界像素边长
    uint32_t color;
    uint32_t spriteId = 0; // 0 = 白精灵；贴图形态（bg/fg）
    float uFrac = 1.0f;    // 右缘 UV 裁剪比例 [0,1]（1 = 全幅；视图侧换算逐实例 UV）
};

class FxChannel {
public:
    static constexpr uint32_t kMaxTexts = 512;  // 环形池（满 = 最老者淘汰，03 §10；2026-10-08 M7c 批④ 256→512——满屏跳字容量翻倍，bench 门禁复跑无降）
    static constexpr uint32_t kMaxBars = 128;   // 实体键控槽（满 = 淘汰最旧 age 最大者）
    static constexpr float kTextLife = 0.8f;    // 飘字默认寿命（秒；S3 起可逐条覆盖）
    static constexpr float kTextRise = 24.0f;   // 寿命内总上浮量（世界像素；世界 Y 向下，上浮 = -y）
    static constexpr float kBarSticky = 3.0f;   // 血条无刷新自隐（秒）
    static constexpr float kBarHeight = 4.0f;   // 血条默认高（世界像素；S2 起可逐条覆盖）
    static constexpr float kLagCatch = 0.35f;   // 延迟条追赶速率（满量程/秒，线性）
    static constexpr uint32_t kBarBgColor = 0xA0202020u; // 背板暗色
    static constexpr float kPopScaleBoost = 0.4f; // Pop 出生加成（1.4× 回落 1.0）
    static constexpr float kPopScaleWindow = 0.35f; // Pop 缩放窗（寿命前 35%）

    /// 飘字入池（16 字符截断；池满覆写最老槽）——现状签名（S3 默认参数形态）
    void PopupText(const char* text, float x, float y, uint32_t color = 0xFFFFFFFFu);
    /// S3 全参数形态（动效尾加；scale ≤ 0 按 1 处理，life < 0 按默认）
    void PopupTextEx(const char* text, float x, float y, uint32_t color, float scale,
                     float life, float driftX, FxCurve curve);
    /// 血条写入（命中实体即覆写刷新 age；新实体占空槽；满 = 淘汰最旧）——现状签名
    void Bar(uint64_t entity, float frac, uint32_t color = 0xFF30B0F0u,
             float width = 32.0f);
    /// S2 全参数形态（贴图 bg/fg + 延迟条 + 高度；spriteId 0 = 白精灵现状路径；
    /// anchorDy = 头顶锚定修正，帧透明边距下压用，0 = 现状）
    void BarEx(uint64_t entity, float frac, uint32_t color, float width,
               uint32_t bgSpriteId, uint32_t fgSpriteId, uint32_t lagColor, float height,
               float anchorDy = 0.0f);

    /// 表现推进（渲染帧 dt）：age 递增、到期释放（飘字环形队首侧、血条逐槽）、
    /// 延迟条 lagFrac 向 frac 线性收敛（S2）
    void Simulate(float dt);
    void Clear() {
        textHead_ = textCount_ = 0;
        barCount_ = 0;
        for (FxBar& b : bars_) b = FxBar{};
    }

    uint32_t TextCount() const { return textCount_; }
    uint32_t BarCount() const { return barCount_; }
    /// 飘字按插入序访问（i < TextCount()；升序 = 时间旧→新）
    const FxText& TextAt(uint32_t i) const {
        return texts_[(textHead_ + kMaxTexts - textCount_ + i) % kMaxTexts];
    }
    /// 逐条寿命（life 0 = 默认 kTextLife）
    static float TextLifeOf(const FxText& t) { return t.life > 0.0f ? t.life : kTextLife; }
    /// 当前时刻动效（S3：曲线分支 + 漂移；单测直测数学）。
    /// outDx/outDy = 锚点偏移（世界像素）；outScale = 字号乘子；outAlpha = [0,1]
    static void TextMotion(const FxText& t, float& outDx, float& outDy, float& outScale,
                           float& outAlpha);
    /// 现状口径保留（GameFx 旧路径/单测）：上浮偏移
    static float TextRise(const FxText& t) {
        float dx, dy, s, a;
        TextMotion(t, dx, dy, s, a);
        return dy;
    }
    /// 现状口径保留：透明度（末 30% 线性淡出）
    static float TextAlpha(const FxText& t) {
        float dx, dy, s, a;
        TextMotion(t, dx, dy, s, a);
        return a;
    }
    std::span<const FxBar> Bars() const { return bars_; }

    /// 血条产包（纯数学，零 GPU 依赖）：resolve = 实体→世界位 + 精灵头顶高度
    ///（outTop = 精灵顶相对锚点的世界高度，贴头顶锚定用；无精灵/未知 = 0——
    /// 条画在锚点上方 2px，M6a 小精灵语义不变）。悬空/越界 resolve 返回 false 跳过。
    /// 每条产 2–3 描述子（bg 整宽 → 延迟条（lagColor≠0 且 lagFrac>frac）→
    /// fg 比例宽左锚，条中心 = 锚点 - outTop/2 - height/2 - 2px + anchorDy
    ///（世界 Y 向下，outTop = 精灵全高 × scale.y，条贴精灵头顶；anchorDy =
    /// 帧透明边距下压修正）。贴图形态
    /// uFrac<1 = 横向 UV 裁剪）。返回写出数（≤ maxOut）。
    uint32_t ExtractBarQuads(FxQuad* out, uint32_t maxOut,
                             const std::function<bool(uint64_t, Vec2&, float&)>& resolve,
                             Rect view, float margin = 64.0f) const;

private:
    FxText texts_[kMaxTexts]{};
    uint32_t textHead_ = 0;   // 环形写位
    uint32_t textCount_ = 0;  // 在场数（环形尾部连续）
    FxBar bars_[kMaxBars]{};
    uint32_t barCount_ = 0;
};

} // namespace lemon::ecs
