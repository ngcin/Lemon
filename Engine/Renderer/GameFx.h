// Lemon 引擎 — Play 表现层段装配（M7a 批④；M6a 批① 随迁）
// 自 ViewportRenderer 渲染 Play 路径下沉（搬家非复制）：世界空间表现通道
//（06 §8 恒定原则——恒走 sprite 管线）。飘字 = 位图字体页层 252（文本段）；
// 血条 = 白精灵/贴图四边形层 251（精灵段，压精灵与粒子、让位 UI 文本 254）。
// ViewportRenderer（gameRT）与 lemon-game（swapchain 直渲染）两薄壳共用——
// 防第三套实现。Simulate 按渲染帧 dt 自计时（粒子先例：表现层语义，非确定
// 可接受、不入回放）——时钟归调用方（编辑器 per-viewport lastFxTime_）。
// M7c 批①：贴图血条（FxQuad.spriteId/uFrac → 逐实例 UV 裁剪）与飘字动效
//（TextMotion 曲线/漂移/缩放 × BitmapFont 外部页 UTF-8）在此汇流。
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Math.h" // Rect
#include "Renderer/Renderable.h" // SpritePacket

namespace lemon::ecs {
class Scene;
class FxChannel;
} // namespace lemon::ecs

namespace lemon::renderer {
class BitmapFont;
class AtlasRegistry;

/// 装配一段 Play 表现层：fx.Simulate(fxDt) + 血条（实体位锚定，悬空/出视口由
/// 通道过滤）+ 飘字（视口剔除 + 曲线上浮/漂移 + 末 30% 淡出）。bars 追加进 Bake
/// 尾段缓冲、texts 追加进文本段缓冲（调用方每帧清空后传入）。whiteSprite = 白精
/// 灵号（血条无贴图路径的染色底纹）；atlas = UV 查表（贴图血条横向裁剪）；
/// view = 当前相机视口世界矩形。
void AppendGameFx(ecs::FxChannel& fx, ecs::Scene& scene, const Rect& view,
                  uint32_t whiteSprite, const BitmapFont& font,
                  const AtlasRegistry& atlas, float fxDt,
                  std::vector<SpritePacket>& bars, std::vector<SpritePacket>& texts);

} // namespace lemon::renderer
