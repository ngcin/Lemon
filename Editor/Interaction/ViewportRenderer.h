// Lemon 编辑器 — 视口渲染器（M4-Editor-Plan §2.2 SceneView/GameView；内核 #2/#4/#11/#13）
// 职责：
//   * ECS → RenderableManager 提取（世界矩阵合成消费内核 #1；销毁/禁用释放 #2；
//     场景切换映射失效 #4）
//   * 程序化测试图集 + 字体页（M4.4 PNG 导入器落地前的 sprite 来源，零外部素材）
//   * 双视口离屏渲染：SceneView（编辑相机 + overlay：网格/选框/Gizmo/实体名标签）、
//     GameView（游戏相机，纯游戏内容）；RT 显式声明规约 = cl.BeginOffscreenPass（#11）
//   * overlay 通道：SceneView 面板每帧 Render 前注入世界空间四边形（细线/框 = 细条）
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ECS/Entity.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteBatcher.h"

namespace lemon::editor {

class ImGuiBackend;
class EditorContext;

using renderer::AtlasRegistry;
using renderer::BitmapFont;
using renderer::Camera2D;
using renderer::RenderableManager;
using renderer::SpriteBatcher;
using renderer::SpritePacket;

/// 程序化测试图集：8×64px 调色板（0=白 overlay 染色底纹，1..7 彩色）+ ASCII 字体页
class ProceduralAtlas {
public:
    static constexpr uint32_t kPaletteSprites = 8;
    static constexpr uint32_t kCellPx = 64;

    void Build(rhi::Device& device); // 页纹理 + 采样器 + 图集槽 0 / 字体槽 1
    AtlasRegistry& Registry() { return atlas_; }
    const AtlasRegistry& Registry() const { return atlas_; }
    BitmapFont& Font() { return font_; }
    uint32_t WhiteSprite() const { return whiteId_; } // overlay 染色底纹（Build 时登记）
    rhi::Texture Page() const { return page_; }       // 调色板页（AssetBrowser 图标源）

private:
    AtlasRegistry atlas_;
    BitmapFont font_;
    rhi::Texture page_{};
    rhi::Sampler linear_{}, point_{};
    uint32_t whiteId_ = 1; // spriteId 1 起始（Atlas 句柄约定；0 无效）
};

/// 双视口渲染器（SceneView idx=0 / GameView idx=1）
class ViewportRenderer {
public:
    void Init(rhi::Device& device, ImGuiBackend& ui);
    void OnDeviceRecreated(rhi::Device& device); // 设备丢失回调（图集/管线/RT 重建）

    /// 每渲染帧（BuildUI 之后调用：overlay 已由面板注入；ImGui::Image 已引用 RT）
    void Render(rhi::CommandList& cl, EditorContext& ctx);

    // ---- 面板侧接口 ----
    /// 视口尺寸上报（ImGui 内容区像素）；尺寸变化时重建 RT 并重注册 ImGui 纹理。
    /// 返回当前 ImTextureID（可能为 null = 本帧无内容可显示）。
    void* EnsureRenderTarget(uint32_t idx, uint32_t wantW, uint32_t wantH, const char* debugName);
    uint32_t RenderTargetWidth(uint32_t idx) const { return rts_[idx].w; }
    uint32_t RenderTargetHeight(uint32_t idx) const { return rts_[idx].h; }

    Camera2D& SceneCam() { return sceneCam_; }
    Camera2D& GameCam() { return gameCam_; }
    RenderableManager& Renderables() { return rm_; }

    // ---- overlay 注入（世界空间；仅进 SceneView 包）----
    void PushOverlayQuad(Vec2 pos, Vec2 size, float rot, uint32_t rgba, int16_t order = 1000);
    void PushOverlayLine(Vec2 a, Vec2 b, uint32_t rgba, float lineW, int16_t order = 1000);
    void PushOverlayRect(Vec2 center, Vec2 size, float rot, uint32_t rgba, float lineW,
                         int16_t order = 1000);

    /// 拾取：世界坐标 → 最近命中实体（SpriteRenderer 世界 AABB；无 sprite 实体 24px 盒）
    ecs::Entity Pick(EditorContext& ctx, Vec2 worldPos) const;
    /// 实体世界包围盒（拾取/选框/Gizmo 共用）
    bool WorldBoundsOf(EditorContext& ctx, ecs::Entity e, Vec2& center, Vec2& size,
                       float& rot) const;

    /// 帧推进（present 之后；两个合批器环形缓冲推进）
    void AdvanceFrame() {
        sceneBatcher_.AdvanceFrame();
        gameBatcher_.AdvanceFrame();
    }

    uint32_t LastSceneVisible() const { return lastSceneVisible_; }
    ProceduralAtlas& Assets() { return assets_; }
    /// 调色板页的 ImGui 纹理（AssetBrowser 非资产图标：色块 uv 子区）。null = 未注册
    void* PaletteIconTex() const { return paletteIconTex_; }
    /// 编辑相机世界→屏幕（用于鼠标坐标换算；面板持有 RT 尺寸）
    Vec2 WorldToScreen(const Camera2D& cam, Vec2 world, uint32_t rtW, uint32_t rtH) const;
    Vec2 ScreenToWorld(const Camera2D& cam, Vec2 screen, uint32_t rtW, uint32_t rtH) const;

private:
    void ExtractScene(EditorContext& ctx);           // 内核 #1/#2/#4
    void RenderViewport(rhi::CommandList& cl, uint32_t idx, SpriteBatcher& batcher,
                        const Camera2D& cam, bool withOverlay, EditorContext& ctx);

    rhi::Device* device_ = nullptr;
    ImGuiBackend* ui_ = nullptr;
    ProceduralAtlas assets_;
    RenderableManager rm_;
    SpriteBatcher sceneBatcher_, gameBatcher_;

    Camera2D sceneCam_{}; // 编辑相机（02 §3.5：与游戏相机同类型双实例，输入来源分离 #13）
    Camera2D gameCam_{};  // 游戏相机（M4.3 Play 中可被脚本驱动；编辑态固定默认位）
    uint64_t lastSceneStamp_ = 0;

    std::unordered_map<uint64_t, uint32_t> entityToRenderable_; // Entity.id → renderable id
    std::vector<SpritePacket> overlay_;                         // SceneView 专属（面板注入）
    std::vector<SpritePacket> scenePackets_, gamePackets_;      // Extract 快照（合并 overlay）

    struct RT {
        uint32_t w = 0, h = 0;
        rhi::Texture tex{};
        void* imguiTexId = nullptr; // VkDescriptorSet（ImGui_Image 用）
    };
    RT rts_[2];
    uint32_t lastSceneVisible_ = 0;
    void* paletteIconTex_ = nullptr; // 调色板页 ImTextureID（图标源；设备丢失重注册）
};

} // namespace lemon::editor
