// Lemon 引擎单测 — LightTests — 光照组件域（M8 批①：Light2D + BlobShadow2D
// 数据面——登记/默认值/生命周期/.scene 往返/哈希流）。
// M8 批② 增渲染域四组：衰减/角向纯函数（02 §8 定形）、提取通道双缓冲、
// Compositor CPU 编排决策（Global 去重/预算截断）。
// M8 批③ 增：蒙版解析纯函数、blob 椭圆实例映射、blob opacity 滤除、
// SpriteMask 有效性把关与新 typeBlend 打包。
#include "TestFramework.h"

#include "Components/CoreComponents.h"
#include "Components/LightComponents.h"
#include "Core/Math.h"
#include "ECS/Scene.h" // 先于 ComponentRegistry.h：Entity 声明经此带入
#include "ECS/ComponentRegistry.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Renderer/Atlas.h"
#include "Renderer/LightCompositor.h"
#include "Renderer/Renderable.h"
#include "Renderer/SceneExtractor.h"
#include "Serialization/SceneArchive.h"

#include <cmath>
#include <string>

using namespace lemon::ecs;
using lemon::Vec2;

// 登记面：表尾追加（id 32/33，只增不改序）+ 字段表/编辑元数据/尺寸全对账
void TestLightCatalogRegistration() {
    RegisterAllComponents();
    auto& reg = ComponentRegistry::Instance();
    const ComponentMeta* light = reg.Find("Light2D");
    const ComponentMeta* blob = reg.Find("BlobShadow2D");
    Expect(light && light->id == 32, "Light2D registered at tail id 32");
    Expect(blob && blob->id == 33, "BlobShadow2D registered at tail id 33");
    Expect(std::string(reg.At(31).name) == "AudioSource",
           "AudioSource still id 31 (tail-append, no reorder)");
    Expect(light->sizeOf == 36 && light->fieldCount == 9,
           "Light2D 36B / 9 fields (pad_ unregistered)");
    Expect(blob->sizeOf == 16 && blob->fieldCount == 4,
           "BlobShadow2D 16B / 4 fields");
    Expect(light->fields && light->editorMeta && blob->fields && blob->editorMeta,
           "field tables + editor meta present (Inspector zero-code)");
    Expect(std::string(light->fields[0].name) == "type" &&
               std::string(light->fields[8].name) == "flags",
           "Light2D field order head/tail");
}

// 默认值 + 生命周期：C++ 侧默认 = 点光/白/Additive/启用（Unity URP 2D 同构）；
// C# default 零值三坑（type=Global/color=透明黑/flags=禁用）在镜像侧注释 + SDK 测试覆盖
void TestLightDefaultsAndLifecycle() {
    World world;
    Scene& s = world.CreateScene("light-life");
    Entity e = s.Create();
    const Light2D& l = s.Emplace<Light2D>(e);
    Expect(l.type == kLightTypePoint && l.blend == kLightBlendAdditive,
           "defaults: point light + additive");
    Expect(l.color == 0xFFFFFFFFu, "defaults: white");
    Expect(l.flags == kLightEnabled, "defaults: enabled bit set");
    ExpectNear(l.intensity, 1.0f, 1e-6f, "defaults: intensity 1");
    ExpectNear(l.range, 256.0f, 1e-6f, "defaults: range 256");
    ExpectNear(l.angle, 1.57079632f, 1e-6f, "defaults: 90 deg spot cone");
    ExpectNear(l.innerRatio, 0.5f, 1e-6f, "defaults: inner ratio 0.5");
    Expect(l.maskSprite == 0, "defaults: no mask sprite");
    const BlobShadow2D& b = s.Emplace<BlobShadow2D>(e);
    ExpectNear(b.radius, 32.0f, 1e-6f, "blob defaults: radius 32");
    ExpectNear(b.aspect, 1.0f, 1e-6f, "blob defaults: unit aspect");
    ExpectNear(b.offsetY, 0.0f, 1e-6f, "blob defaults: no offset");
    ExpectNear(b.opacity, 0.5f, 1e-6f, "blob defaults: half opacity");

    s.Remove<Light2D>(e);
    Expect(!s.TryGet<Light2D>(e), "light removed");
    const Light2D& again = s.Emplace<Light2D>(e);
    Expect(again.type == kLightTypePoint && again.flags == kLightEnabled,
           "re-emplace = fresh C++ defaults (constructFn/emplaceFn 同口径)");
}

// .scene 往返：全字段 + 关灯边缘（flags=0）+ blob 四字段 + 二次往返不动点
void TestLightSceneRoundtrip() {
    RegisterAllComponents();
    World world;
    Scene& src = world.CreateScene("LightScene");
    Entity torch = src.Create();
    Light2D spot{};
    spot.type = kLightTypeSpot;
    spot.blend = kLightBlendScreen;
    spot.color = 0x66AAFFFFu;
    spot.intensity = 2.5f;
    spot.range = 384.0f;
    spot.angle = 0.75f;
    spot.innerRatio = 0.25f;
    spot.maskSprite = 7;
    spot.flags = 0; // 关灯边缘值（bit0 清）
    src.Emplace<Light2D>(torch, spot);
    Entity mob = src.Create();
    src.Emplace<BlobShadow2D>(mob, BlobShadow2D{48.0f, 1.5f, -12.0f, 0.75f});

    const std::string text = SceneArchive::Save(src);
    World world2;
    Scene& dst = world2.CreateScene("reload");
    Expect(SceneArchive::Load(dst, text), "light scene load ok");
    bool foundLight = false, foundBlob = false;
    dst.Each([&](Entity e) {
        if (auto* l = dst.TryGet<Light2D>(e); l) {
            foundLight = true;
            Expect(l->type == kLightTypeSpot && l->blend == kLightBlendScreen,
                   "light.type/blend roundtrip");
            Expect(l->color == 0x66AAFFFFu, "light.color roundtrip");
            ExpectNear(l->intensity, 2.5f, 1e-6f, "light.intensity roundtrip");
            ExpectNear(l->range, 384.0f, 1e-6f, "light.range roundtrip");
            ExpectNear(l->angle, 0.75f, 1e-6f, "light.angle roundtrip (radians)");
            ExpectNear(l->innerRatio, 0.25f, 1e-6f, "light.innerRatio roundtrip");
            Expect(l->maskSprite == 7, "light.maskSprite roundtrip");
            Expect(l->flags == 0, "light.flags=0 edge roundtrip");
        }
        if (auto* b = dst.TryGet<BlobShadow2D>(e); b) {
            foundBlob = true;
            ExpectNear(b->radius, 48.0f, 1e-6f, "blob.radius roundtrip");
            ExpectNear(b->aspect, 1.5f, 1e-6f, "blob.aspect roundtrip");
            ExpectNear(b->offsetY, -12.0f, 1e-6f, "blob.offsetY roundtrip");
            ExpectNear(b->opacity, 0.75f, 1e-6f, "blob.opacity roundtrip");
        }
    });
    Expect(foundLight && foundBlob, "both light entities located after load");

    const std::string text2 = SceneArchive::Save(dst);
    World world3;
    Scene& third = world3.CreateScene("reload");
    Expect(SceneArchive::Load(third, text2), "second load ok");
    Expect(SceneArchive::Save(third) == text2, "light roundtrip is a fixed point");
}

// 哈希流：光照组件数据入 StateHash（场景级确定性回放的录放面）——加组件必重录
// 口径（09 §6.8 推论）的机械证据；销毁走两阶段提交（Essential 系统两步，
// TestAudioSourceLifecycle 同款）后回空场基准
void TestLightStateHashStream() {
    RegisterAllComponents();
    World world;
    Scene& s = world.CreateScene("light-hash");
    world.SetActiveScene(&s); // DestroyCommitSystem 只提交活动场景（Essential 语义）
    world.InstallDefaultSystems();
    world.Step(1.0f / 60.0f); // 空场一帧 = 基线口径稳定
    const uint64_t h0 = ComputeStateHash(s);
    Entity e = s.Create();
    s.Emplace<Light2D>(e);
    const uint64_t h1 = ComputeStateHash(s);
    Expect(h1 != h0, "Light2D data enters state hash (replay records light writes)");
    s.Emplace<BlobShadow2D>(e);
    Expect(ComputeStateHash(s) != h1, "BlobShadow2D data enters state hash too");
    Light2D mod = s.Get<Light2D>(e);
    mod.intensity = 3.0f;
    s.Get<Light2D>(e) = mod;
    Expect(ComputeStateHash(s) != h1, "field write changes hash (昼夜补间可入回放)");
    s.Destroy(e);
    world.Step(1.0f / 60.0f); // 两阶段销毁提交（Essential）
    world.Step(1.0f / 60.0f);
    Expect(ComputeStateHash(s) == h0, "scene back to empty baseline after destroy");
}

// ---- M8 批②：渲染域 ----

// 衰减曲线（02 §8 定形 2026-10-11 拍板）：t = clamp(1−dn)，att = t²(3−2t)——
// 端点零导数（range 边界无亮环/硬切）、中点 0.5（S 曲线对称）、单调递减
void TestLightAttenuationCurves() {
    using namespace lemon::renderer;
    ExpectNear(LightRadialAttenuation(0.0f), 1.0f, 1e-6f, "att(0)=1 (light center)");
    ExpectNear(LightRadialAttenuation(1.0f), 0.0f, 1e-6f, "att(1)=0 (range edge)");
    ExpectNear(LightRadialAttenuation(0.5f), 0.5f, 1e-6f, "att(0.5)=0.5 (S midpoint)");
    ExpectNear(LightRadialAttenuation(-0.25f), 1.0f, 1e-6f, "dn<0 clamps to 1");
    ExpectNear(LightRadialAttenuation(1.5f), 0.0f, 1e-6f, "dn>1 clamps to 0");
    float prev = 2.0f;
    for (int i = 0; i <= 20; ++i) {
        const float a = LightRadialAttenuation((float)i / 20.0f);
        Expect(a < prev + 1e-9f, "attenuation monotonically non-increasing");
        prev = a;
    }
}

// Spot 角向软边：外半角外归零、内半角内全亮、之间 smoothstep 单调升
void TestLightSpotAngular() {
    using namespace lemon::renderer;
    const float outerCos = std::cos(0.8f), innerCos = std::cos(0.4f); // 半角 0.8/0.4
    ExpectNear(LightSpotAngular(1.0f, outerCos, innerCos), 1.0f, 1e-6f,
               "angular full inside inner cone");
    ExpectNear(LightSpotAngular(outerCos, outerCos, innerCos), 0.0f, 1e-6f,
               "angular zero at outer edge");
    ExpectNear(LightSpotAngular(-1.0f, outerCos, innerCos), 0.0f, 1e-6f,
               "angular zero behind light");
    const float midCos = (outerCos + innerCos) * 0.5f;
    const float mid = LightSpotAngular(midCos, outerCos, innerCos);
    Expect(mid > 0.0f && mid < 1.0f, "angular soft transition in penumbra");
    float prevA = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float c = outerCos + (innerCos - outerCos) * (float)i / 20.0f;
        const float a = LightSpotAngular(c, outerCos, innerCos);
        Expect(a >= prevA - 1e-9f, "angular monotonic inner-ward");
        prevA = a;
    }
}

// 提取通道（RenderableManager 光/blob）：双缓冲插值、视口剔除（Global 恒可见）、
// 禁用灯不建槽、销毁差集释放——SceneExtractor 端到端（ECS 组件 → AliveCount）
void TestLightExtractionChannel() {
    using namespace lemon::renderer;
    World world;
    Scene& s = world.CreateScene("light-extract");
    const AtlasRegistry atlas; // 空图集（悬空 sprite 不建 renderable——灯通道无此依赖）
    RenderableManager rm;
    SceneExtractor ex;

    Entity lit = s.Create();
    s.Emplace<Transform2D>(lit).pos = Vec2{100, 100};
    Light2D& l = s.Emplace<Light2D>(lit); // 默认 Point/启用
    l.range = 64.0f;
    Entity dark = s.Create();
    s.Emplace<Transform2D>(dark).pos = Vec2{0, 0};
    Light2D& off = s.Emplace<Light2D>(dark);
    off.flags = 0; // 禁用灯：提取侧滤除（flags=0 落场 = smoke-light 基线路径）
    Entity blobEnt = s.Create();
    s.Emplace<Transform2D>(blobEnt).pos = Vec2{50, 50};
    s.Emplace<BlobShadow2D>(blobEnt);
    Entity blobOff = s.Create(); // 批③：opacity=0 = 关（无 flags 字段的关灯口径）
    s.Emplace<Transform2D>(blobOff).pos = Vec2{60, 60};
    BlobShadow2D& bs = s.Emplace<BlobShadow2D>(blobOff);
    bs.opacity = 0.0f;

    ex.Extract(s, atlas, rm);
    Expect(rm.AliveLightCount() == 1, "enabled light extracted, disabled skipped");
    Expect(rm.AliveBlobCount() == 1, "blob extracted; opacity=0 blob filtered (b3 toggle)");

    // 双缓冲插值：模拟 BeginSimTick 后移动（prev=100,100 → cur=200,100）
    s.Get<Transform2D>(lit).pos = Vec2{200, 100};
    ex.Extract(s, atlas, rm);
    // 剔除按 range 光圆（影响域）：视口放远处 → 光圆不相交 = 剔除
    rm.SetViewport(Vec2{2000, 2000}, 8, 8, 0.0f);
    auto items = rm.ExtractLights(0.5f);
    Expect(items.empty(), "point light culled when range circle misses viewport");
    rm.SetViewport(Vec2{150, 100}, 200, 200, 0.0f); // 覆盖插值点 (150,100)
    items = rm.ExtractLights(0.5f);
    Expect(items.size() == 1, "interpolated light visible at midpoint viewport");
    if (!items.empty())
        ExpectNear(items[0].posX, 150.0f, 1e-4f, "light position interpolated (alpha=0.5)");

    // Global 恒可见（ambient 底色源与视口无关）
    Entity g = s.Create();
    s.Emplace<Transform2D>(g).pos = Vec2{99999, 99999};
    Light2D& gl = s.Emplace<Light2D>(g);
    gl.type = kLightTypeGlobal;
    ex.Extract(s, atlas, rm);
    rm.SetViewport(Vec2{0, 0}, 8, 8, 0.0f);
    bool sawGlobal = false;
    for (const auto& it : rm.ExtractLights(1.0f)) sawGlobal |= it.type == kLightTypeGlobal;
    Expect(sawGlobal, "Global light always visible (ambient needs it)");

    // 组件摘除差集释放（s.Destroy 是两阶段提交——组件 Remove 即时，走差集口径）
    s.Remove<Light2D>(lit);
    s.Remove<Light2D>(g);
    s.Remove<Light2D>(dark);
    s.Remove<BlobShadow2D>(blobEnt);
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveLightCount() == 0 && rm.AliveBlobCount() == 0,
           "removed light/blob components released by epoch diff");
}

// Compositor CPU 编排（Prepare 纯 CPU）：Global 去重取首个（ambient=其色）、
// SpriteMask 有效性把关（批③：无效 maskSprite dropped、有效放行）、光/blob
// 预算独立截断计数
void TestLightCompositorPlan() {
    using namespace lemon::renderer;
    // 哑图集（RegisterAtlas 不校验纹理句柄——单测无设备可构造）：页 7 上登记
    // spriteId 1 = 有效蒙版；其余号全悬空
    AtlasRegistry atlas;
    atlas.RegisterAtlas(7, lemon::rhi::Texture{}, 128, 128);
    Expect(atlas.AddSpriteAt(1, 7, 0, 0, 64, 64), "seed valid mask sprite");

    LightCompositor comp;
    auto mkLight = [](uint32_t type, uint32_t blend, uint32_t color, float range,
                      uint32_t maskSprite) {
        LightItem it{};
        it.type = type;
        it.blend = blend;
        it.color = color;
        it.intensity = 1.0f;
        it.range = range;
        it.maskSprite = maskSprite;
        return it;
    };
    std::vector<LightItem> items;
    items.push_back(mkLight(kLightTypeGlobal, 0, 0xFFD6F4FFu, 0, 0)); // 首个 Global（胜出）
    items.push_back(mkLight(kLightTypeGlobal, 0, 0xFF0000FFu, 0, 0)); // 重复（取首）
    items.push_back(mkLight(kLightTypeSpriteMask, 0, 0xFFFFFFFFu, 64, 999)); // 悬空蒙版 → dropped
    items.push_back(mkLight(kLightTypeSpriteMask, 0, 0xFFFFFFFFu, 64, 1));   // 有效蒙版 → 放行
    items.push_back(mkLight(kLightTypePoint, 0, 0xFF0000FFu, 64, 0));
    items.push_back(mkLight(kLightTypePoint, 1, 0xFFFFFF00u, 64, 0));
    items.push_back(mkLight(kLightTypePoint, 2, 0xFFFFFFFFu, 64, 0));
    std::vector<BlobItem> blobs(2);
    blobs[0].radius = 48.0f;
    blobs[1].radius = 32.0f;
    // 可绘制候选 = 有效蒙版 + 3 点光 = 4，budget=3 → draw 3 / drop 1（超限）+1（悬空蒙版）
    const auto& plan = comp.Prepare(items, blobs, /*lightBudget=*/3, &atlas);
    Expect(plan.drawLights == 3, "budget truncates 4 drawable lights (incl valid mask) to 3");
    Expect(plan.droppedLights == 2, "dropped = invalid mask + over-budget");
    Expect(plan.ambient[0] == 1.0f && std::fabs(plan.ambient[1] - 244.0f / 255.0f) < 1e-5f,
           "ambient = FIRST Global color x intensity (dup ignored)");
    const auto& filtered = comp.FilteredItems();
    Expect(filtered.size() == 3 && filtered[0].type == kLightTypeSpriteMask &&
               filtered[0].maskSprite == 1 && filtered[1].blend == 0 && filtered[2].blend == 1,
           "filtered = drawable only, stable order (mask head, over-budget point tail-cut)");
    Expect(plan.drawBlobs == 2 && plan.droppedBlobs == 0 && comp.FilteredBlobs().size() == 2,
           "blobs pass within budget (independent counter)");

    // blob 独立截断：灯满员不得挤掉暗斑（bench-lights 64 灯 + 8 blob 形态互不侵扰）
    std::vector<BlobItem> many(5);
    const auto& plan2 = comp.Prepare({}, many, /*lightBudget=*/3, &atlas);
    Expect(plan2.drawBlobs == 3 && plan2.droppedBlobs == 2,
           "blob budget truncates independently of lights");

    // 无图集壳：所有 SpriteMask 判无效（防御口径）
    const std::vector<LightItem> maskOnly{
        mkLight(kLightTypeSpriteMask, 0, 0xFFFFFFFFu, 64, 1)};
    const auto& plan3 = comp.Prepare(maskOnly, {}, 64, nullptr);
    Expect(plan3.drawLights == 0 && plan3.droppedLights == 1,
           "null atlas = all SpriteMask lights dropped");
}

// 蒙版解析纯函数（批③）：有效号出 UV/页槽、悬空/零号/null 图集 false
void TestLightMaskResolve() {
    using namespace lemon::renderer;
    AtlasRegistry atlas;
    atlas.RegisterAtlas(7, lemon::rhi::Texture{}, 128, 128);
    Expect(atlas.AddSpriteAt(1, 7, 8, 16, 64, 32), "seed sprite at (8,16) 64x32");
    float uv[4] = {-1, -1, -1, -1};
    uint32_t page = 0xFFFFFFFFu;
    Expect(ResolveLightMask(&atlas, 1, uv, page), "valid sprite resolves");
    ExpectNear(uv[0], 8.0f / 128.0f, 1e-6f, "mask u0");
    ExpectNear(uv[1], 16.0f / 128.0f, 1e-6f, "mask v0");
    ExpectNear(uv[2], 72.0f / 128.0f, 1e-6f, "mask u1 (px+w)");
    ExpectNear(uv[3], 48.0f / 128.0f, 1e-6f, "mask v1 (py+h)");
    Expect(page == 7, "mask atlas page slot");
    Expect(!ResolveLightMask(&atlas, 999, uv, page), "dangling spriteId false");
    Expect(!ResolveLightMask(&atlas, 0, uv, page), "spriteId 0 false");
    Expect(!ResolveLightMask(nullptr, 1, uv, page), "null atlas false");
}

// blob → 光照实例映射（批③）：Point|Subtract 打包、白×opacity、椭圆仿射、
// offsetY 平移；typeBlend 新打包位段（type<<28|blend<<24|page）
void TestBlobInstanceMapping() {
    using namespace lemon::renderer;
    BlobItem b{};
    b.posX = 100.0f;
    b.posY = 200.0f;
    b.radius = 48.0f;
    b.aspect = 1.5f;
    b.offsetY = -6.0f;
    b.opacity = 0.6f;
    LightInstance inst{};
    FillBlobInstance(inst, b);
    ExpectNear(inst.a, 2.0f * 48.0f * 1.5f, 1e-6f, "blob affine edgeX = 2·radius·aspect");
    ExpectNear(inst.d, 2.0f * 48.0f, 1e-6f, "blob affine edgeY = 2·radius");
    Expect(inst.b == 0.0f && inst.c == 0.0f, "blob has no rotation");
    ExpectNear(inst.e, 100.0f, 1e-6f, "blob translate x = pos");
    ExpectNear(inst.f, 194.0f, 1e-6f, "blob translate y = pos + offsetY");
    Expect(inst.colorBits == 0xFFFFFFFFu, "blob color white (dst − white·op·att)");
    ExpectNear(inst.intensity, 0.6f, 1e-6f, "blob intensity = opacity");
    Expect((inst.typeBlend >> 28) == 1u && ((inst.typeBlend >> 24) & 0xFu) == 2u,
           "blob packs Point-path | Subtract");

    // 新打包位段：type/blend/page 三段各自无损往返
    const uint32_t packed = PackLightTypeBlend(3u, 1u, 249u);
    Expect((packed >> 28) == 3u && ((packed >> 24) & 0xFu) == 1u && (packed & 0xFFFFFFu) == 249u,
           "PackLightTypeBlend roundtrip type/blend/atlasIdx");
    Expect(PackLightTypeBlend(1u, 2u, 0u) >> 28 == 1u, "non-mask lights page bits zero-safe");
}

void RunLightTests() {
    TestLightCatalogRegistration();
    TestLightDefaultsAndLifecycle();
    TestLightSceneRoundtrip();
    TestLightStateHashStream();
    TestLightAttenuationCurves();
    TestLightSpotAngular();
    TestLightExtractionChannel();
    TestLightCompositorPlan();
    TestLightMaskResolve();
    TestBlobInstanceMapping();
}
