// Lemon 引擎 — Camera2D（02 §3.5，Prowl2D 已验证清单，ADR-009）
// 正交相机：像素完美（整数缩放 + 像素网格 snap）、指数阻尼跟随 + look-ahead、
// 有界钳制、编辑器自愈（resize 后按新宽高比重导视口 → 视野高度不跳变）。
// 纯数学模块：无 Vulkan/平台依赖，可单测。
#pragma once

#include "Core/Math.h"

namespace lemon::renderer {

struct Camera2D {
    Vec2 center{0, 0};             // 世界中心（跟随目标平滑后的焦点）
    float halfHeight = 360.0f;     // 正交半高（半宽 = 半高 × 宽高比）
    float zoom = 1.0f;             // >1 拉近；像素完美时吸附整数
    bool pixelPerfect = false;     // 像素风默认开（bench 动态场景关）
    Rect worldBounds{{0, 0}, {0, 0}};
    bool hasBounds = false;        // 世界包围盒钳制（TD/ARPG 刚需）

    float HalfWidth(float aspect) const { return halfHeight * aspect; }

    /// 世界→NDC 视图仿射（Y 向下直映射，同 Mat3x2::Ortho 2026-09-19 修订）；像素完美时中心先 snap 到缩放后像素网格
    Mat3x2 ViewProj(float aspect) const {
        Vec2 c = center;
        if (pixelPerfect) c = SnapTo(c, 1.0f / zoom);
        return Mat3x2::Ortho(c, HalfWidth(aspect), halfHeight);
    }

    /// 当前视口世界矩形（RenderableManager::SetViewport 用，外扩由调用方做）
    Rect ViewRect(float aspect) const {
        return Rect::FromCenterHalf(center, HalfWidth(aspect), halfHeight);
    }

    /// 指数阻尼跟随 + look-ahead 预判（smooth-damp 手感）
    void Follow(Vec2 target, float dt, float dampingRate = 5.0f, Vec2 lookAhead = Vec2::Zero()) {
        Vec2 desired = target + lookAhead;
        float t = math::Damp(dampingRate, dt);
        center = math::Lerp(center, desired, t);
    }

    /// 视口钳制到世界包围盒（视野大于世界时居中）
    void ClampToBounds(float aspect) {
        if (!hasBounds) return;
        float hw = HalfWidth(aspect), hh = halfHeight;
        float worldW = worldBounds.max.x - worldBounds.min.x;
        float worldH = worldBounds.max.y - worldBounds.min.y;
        if (worldW <= 2 * hw) {
            center.x = (worldBounds.min.x + worldBounds.max.x) * 0.5f;
        } else {
            center.x = math::Clamp(center.x, worldBounds.min.x + hw, worldBounds.max.x - hw);
        }
        if (worldH <= 2 * hh) {
            center.y = (worldBounds.min.y + worldBounds.max.y) * 0.5f;
        } else {
            center.y = math::Clamp(center.y, worldBounds.min.y + hh, worldBounds.max.y - hh);
        }
    }

    /// 像素完美：zoom 吸附整数、半高按整数倍重导（编辑器 resize 自愈同路径）
    void ApplyPixelPerfect(float referenceHalfHeight) {
        if (!pixelPerfect) return;
        zoom = (float)std::lround(zoom);
        if (zoom < 1.0f) zoom = 1.0f;
        halfHeight = referenceHalfHeight / zoom;
    }

    /// resize 自愈：保视口高度稳定，宽度随宽高比重导（Prowl2D 教训的内核侧形态）
    void OnViewportResized(float newAspect, float oldAspect) {
        (void)newAspect;
        (void)oldAspect; // 半高不变即自愈核心；保留钩子供编辑相机存焦点恢复
    }
};

} // namespace lemon::renderer
