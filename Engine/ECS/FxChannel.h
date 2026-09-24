// Lemon 引擎 — 世界空间表现通道（M6a 批①；06 §8 恒定原则：血条/飘字恒走 sprite
// 管线、不进 UI 框架；03 §10 池化纪律——"飘血数字"预列项落账）
// World 级第三呈现通道（RtUi/Cards 同款）：C# Lemon.Fx / C++ harness 写入，
// 视图侧（编辑器 GameView / M8 打包 HUD 同通道）读出产 sprite 包。
// 呈现层语义：Simulate 按渲染帧 dt（粒子先例——非确定可接受，不入 03 §12 确定性
// 回放）；不入 StateHash（ComputeStateHash 只读 Scene）；EnterPlay 新建 World 自清零。
// 零 Renderer 依赖：血条产包 = 纯 FxQuad 描述子（视图侧转 SpritePacket，引擎测试
// 可直测数学）；飘字产包在视图侧（字形表是 BitmapFont 的 GPU 态）。
#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "Core/Math.h"

namespace lemon::ecs {

/// 飘字条目（伤害/治疗/短文本；16 字符截断——位图字体页渲染，06 §8）
struct FxText {
    char text[16] = {};
    float x = 0.0f, y = 0.0f; // 世界坐标锚点 = 首字符中心（文本向右延伸）
    uint32_t color = 0xFFFFFFFFu;
    float age = 0.0f;         // Simulate 递增；≥kTextLife 释放
};

/// 世界血条条目（按实体键控；受击显伤条 sticky 自隐、每帧刷新即常显）
struct FxBar {
    uint64_t entity = 0;      // 0 = 空槽；实体死亡由渲染侧跳过（不提前释放）
    float frac = 1.0f;        // [0,1]
    uint32_t color = 0xFF30B0F0u; // RGBA 序（r|g<<8|b<<16|a<<24——同 SpriteRenderer.colorRGBA；注意 Lemon.Ui 是 ImGui 的 ABGR 序，勿互拷常量）
    float age = 0.0f;         // 距最近一次刷新；≥kBarSticky 释放
    float width = 32.0f;      // 世界像素（fg 宽 = frac×width 左锚；高恒 4）
};

/// 血条四边形描述子（bg 暗色整宽 + fg 着色比例宽；视图侧 → 白精灵 SpritePacket）
struct FxQuad {
    Vec2 center;
    Vec2 size;     // 世界像素边长
    uint32_t color;
};

class FxChannel {
public:
    static constexpr uint32_t kMaxTexts = 256;  // 环形池（满 = 最老者淘汰，03 §10）
    static constexpr uint32_t kMaxBars = 128;   // 实体键控槽（满 = 淘汰最旧 age 最大者）
    static constexpr float kTextLife = 0.8f;    // 飘字寿命（秒）
    static constexpr float kTextRise = 24.0f;   // 寿命内总上浮（世界像素；开局 70% 匀升）
    static constexpr float kBarSticky = 3.0f;   // 血条无刷新自隐（秒）
    static constexpr float kBarHeight = 4.0f;   // 血条高（世界像素）
    static constexpr uint32_t kBarBgColor = 0xA0202020u; // 背板暗色

    /// 飘字入池（16 字符截断；池满覆写最老槽）
    void PopupText(const char* text, float x, float y, uint32_t color = 0xFFFFFFFFu);
    /// 血条写入（命中实体即覆写刷新 age；新实体占空槽；满 = 淘汰最旧）
    void Bar(uint64_t entity, float frac, uint32_t color = 0xFF30B0F0u,
             float width = 32.0f);

    /// 表现推进（渲染帧 dt）：age 递增、到期释放（飘字环形队首侧、血条逐槽）
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
    /// 当前时刻渲染偏移（上浮进度；0..1 × kTextRise）
    float TextRise(const FxText& t) const {
        const float k = t.age / kTextLife;
        return (k < 0.7f ? k / 0.7f : 1.0f) * kTextRise;
    }
    /// 当前时刻透明度（末 30% 线性淡出；1.0 = 全显）
    float TextAlpha(const FxText& t) const {
        const float k = t.age / kTextLife;
        return k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f;
    }
    std::span<const FxBar> Bars() const { return bars_; }

    /// 血条产包（纯数学，零 GPU 依赖）：resolve = 实体→世界位（悬空/越界由调用方
    /// 表达：resolve 返回 false 跳过该条）；视图内（view ± margin）过滤。
    /// 每条产 2 描述子（bg 整宽 + fg 比例宽左锚，条中心上方 kBarHeight + 2px）。
    /// 返回写出数（≤ maxOut）。
    uint32_t ExtractBarQuads(FxQuad* out, uint32_t maxOut,
                             const std::function<bool(uint64_t, Vec2&)>& resolve,
                             Rect view, float margin = 64.0f) const;

private:
    FxText texts_[kMaxTexts]{};
    uint32_t textHead_ = 0;   // 环形写位
    uint32_t textCount_ = 0;  // 在场数（环形尾部连续）
    FxBar bars_[kMaxBars]{};
    uint32_t barCount_ = 0;
};

} // namespace lemon::ecs
