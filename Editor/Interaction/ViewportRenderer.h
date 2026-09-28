// Lemon 编辑器 — 视口渲染器（M4.md §2.2 SceneView/GameView；内核 #2/#4/#11/#13）
// 职责：
//   * ECS → RenderableManager 提取（世界矩阵合成消费内核 #1；销毁/禁用释放 #2；
//     场景切换映射失效 #4）
//   * 程序化测试图集 + 字体页（M4.4 PNG 导入器落地前的 sprite 来源，零外部素材）
//   * 双视口离屏渲染：SceneView（编辑相机 + overlay：网格/选框/Gizmo/实体名标签）、
//     GameView（游戏相机，纯游戏内容）；RT 显式声明规约 = cl.BeginOffscreenPass（#11）
//   * overlay 通道：SceneView 面板每帧 Render 前注入世界空间四边形（细线/框 = 细条）
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
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

/// UI 图标集（M4.7b 决议 D1：自绘最小集，程序化形状页，零新依赖）。
/// 形状页 = 图集槽 2 的 N×32 页（每枚 32px 槽，N = 枚数），白色形状 → ImGui::Image 染色。
enum class IconKind : uint8_t {
    Play = 0, Pause, Step, Stop,       // 传输控制
    Move, Rotate, Scale, Grid,         // 变换工具
    Entity, Sprite, Camera, Script,    // 实体类型（Hierarchy 行前）
    AssetSprite, AssetPrefab, AssetScript, AssetGeneric, // 资产类型
    AssetRml, AssetRcss,               // UI 文档/样式表（M6a 批③b，ADR-014）
    Cursor,                            // Select 工具（Godot 式选择模式）
    Magnet,                            // 拖拽吸附开关（Godot 磁铁语义）
    Add, Duplicate, Delete, Rename, Search, // 列表行操作（动画工作台 v3.1 左列图标化）
    Count
};
inline constexpr uint32_t kIconCount = (uint32_t)IconKind::Count;

/// overlay 特征色（SceneView overlay 推送与 EditorApp 冒烟像素断言共用——改值自动同步）
namespace overlay {
inline constexpr uint32_t Pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}
// 选框青 / 主选亮青 / 网格 / 主轴稍亮 / Gizmo 手柄黄 / 实体名标签墨色
// （网格 α110 / major α150：α70 时半像素覆盖的线几乎隐形——缩小后格子"忽有忽无"）
inline constexpr uint32_t kSelectColor = Pack(120, 225, 240, 220);
inline constexpr uint32_t kPrimaryColor = Pack(90, 215, 245, 255);
inline constexpr uint32_t kGridColor = Pack(120, 140, 150, 110);
inline constexpr uint32_t kAxisColor = Pack(150, 175, 185, 150);
inline constexpr uint32_t kHandleColor = Pack(250, 220, 90, 255);
inline constexpr uint32_t kLabelInk = Pack(240, 255, 200, 230);
} // namespace overlay

using renderer::AtlasRegistry;
using renderer::BitmapFont;
using renderer::Camera2D;
using renderer::RenderableManager;
using renderer::SpriteBatcher;
using renderer::SpritePacket;

/// 游戏 UI 层回调（M6a 批③a，ADR-014）：gameRT 动态渲染块内、sprite Record 之后
/// EndPass 之前调用（Play 中独占）。std::function——编辑器侧把 UiSubsystem::Render
/// 接进来（引擎 UI 模块不反依赖编辑器；ImGui 层同款"块内追加录制"豁免语义）。
using GameUiLayerFn = std::function<void(rhi::CommandList&, uint32_t, uint32_t)>;

/// 程序化测试图集：8×64px 调色板（0=白 overlay 染色底纹，1..7 彩色）+ ASCII 字体页
/// + 16×32px 图标形状页（M4.7b；白形状 → UI 染色）
class ProceduralAtlas {
public:
    static constexpr uint32_t kPaletteSprites = 8;
    static constexpr uint32_t kCellPx = 64;
    static constexpr uint32_t kIconPx = 32; // 形状页图标边长

    void Build(rhi::Device& device); // 页纹理 + 采样器 + 图集槽 0 / 字体槽 1 / 图标槽 2
    AtlasRegistry& Registry() { return atlas_; }
    const AtlasRegistry& Registry() const { return atlas_; }
    BitmapFont& Font() { return font_; }
    uint32_t WhiteSprite() const { return whiteId_; } // overlay 染色底纹（Build 时登记）
    rhi::Texture Page() const { return page_; }       // 调色板页（AssetBrowser 图标源）
    rhi::Texture IconPage() const { return iconPage_; } // 形状页（UI 图标源）
    /// 图标 uv 子区（ImGui::Image 用；页 16 槽行优先）
    void IconUV(IconKind k, float& u0, float& v0, float& u1, float& v1) const;

private:
    AtlasRegistry atlas_;
    BitmapFont font_;
    rhi::Texture page_{};
    rhi::Texture iconPage_{};
    rhi::Sampler linear_{}, point_{};
    uint32_t whiteId_ = 1; // spriteId 1 起始（Atlas 句柄约定；0 无效）
    rhi::Texture BuildIconPage(rhi::Device& device); // M4.7b 形状页（逐枚 32px 白形状）
};

/// 双视口渲染器（SceneView idx=0 / GameView idx=1）
class ViewportRenderer {
public:
    void Init(rhi::Device& device, ImGuiBackend& ui);
    void OnDeviceRecreated(rhi::Device& device); // 设备丢失回调（图集/管线/RT 重建）

    /// 换项目复位后重绑程序化页的 ImGui 纹理（M5 批④后修②：Reset+Build 产出新
    /// 页句柄，paletteIconTex_/iconTex_ 的旧绑定须注销重注册——防泄漏/防采样旧句柄）
    void RebindProceduralIcons();

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
    /// 场景视口 RT（冒烟像素断言回读用；无效 = 面板折叠/未建）
    rhi::Texture SceneRenderTarget() const { return rts_[0].tex; }
    /// 游戏视口 RT（批③a smoke-uirml 像素断言回读用；对照 SceneRenderTarget）
    rhi::Texture GameRenderTarget() const { return rts_[1].tex; }
    /// 游戏 UI 层挂接（批③a：null = 无 UI；EditorApp 在 UiSubsystem Init 成功后挂入）
    void SetGameUiLayer(GameUiLayerFn fn) { gameUi_ = std::move(fn); }
    /// 调色板页的 ImGui 纹理（AssetBrowser 非资产图标：色块 uv 子区）。null = 未注册
    void* PaletteIconTex() const { return paletteIconTex_; }
    /// 图标形状页的 ImGui 纹理（M4.7b 工具栏/Hierarchy/AssetBrowser 图标源）
    void* IconTex() const { return iconTex_; }
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
    uint64_t extractEpoch_ = 0; // ExtractScene 调用计数（差集判定：lastSeen != 当前纪元 → 释放）

    // Entity → renderable 映射（2026-09-26 渲染提取批：unordered_map → 位索引
    // 数组。原 map find 是五万场 ExtractScene 的单项大头；数组 = Scene::EnttIndex
    // 直下标 + EnttVersion 防回收串槽。容量随实体池只增；稳态零分配）
    struct SlotMap {
        uint32_t rid = 0;      // 0 = 空
        uint32_t version = 0;  // 写入时实体 version（校验防串）
        uint64_t lastSeen = 0;
    };
    std::vector<SlotMap> ridBySlot_;
    std::vector<SpritePacket> overlay_;                         // SceneView 专属（面板注入）
    std::vector<SpritePacket> textBuf_, fxBarBuf_; // 视口包复用缓冲（提取段零分配；
                                                   // Bake 消费完即弃，串行双视口单缓冲）
    GameUiLayerFn gameUi_; // 游戏 UI 层（批③a；Play 中 gameRT 块内调用）

    struct RT {
        uint32_t w = 0, h = 0;
        rhi::Texture tex{};
        void* imguiTexId = nullptr; // VkDescriptorSet（ImGui_Image 用）
    };
    RT rts_[2];
    uint32_t lastSceneVisible_ = 0;
    std::chrono::steady_clock::time_point lastFxTime_{}; // Fx Simulate 自计时（GameView；
                                                          // epoch 0 = 首帧/出 Play 复位）
    void* paletteIconTex_ = nullptr; // 调色板页 ImTextureID（图标源；设备丢失重注册）
    void* iconTex_ = nullptr;        // 形状页 ImTextureID（M4.7b；设备丢失重注册）
};

} // namespace lemon::editor
