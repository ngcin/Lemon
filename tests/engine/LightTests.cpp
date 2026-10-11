// Lemon 引擎单测 — LightTests — 光照组件域（M8 批①：Light2D + BlobShadow2D
// 数据面——登记/默认值/生命周期/.scene 往返/哈希流；渲染消费归批② 不在本域）。
// M8 批② 增渲染域四组：衰减/角向纯函数（02 §8 定形）、提取通道双缓冲、
// Compositor CPU 编排决策（Global 去重/SpriteMask 滤除/预算截断）。
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

    ex.Extract(s, atlas, rm);
    Expect(rm.AliveLightCount() == 1, "enabled light extracted, disabled skipped");
    Expect(rm.AliveBlobCount() == 1, "blob extracted (render lands in b3)");

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
// SpriteMask 滤除（批③ 前跳过）、预算截断计数
void TestLightCompositorPlan() {
    using namespace lemon::renderer;
    LightCompositor comp;
    auto mkLight = [](uint32_t type, uint32_t blend, uint32_t color, float range) {
        LightItem it{};
        it.type = type;
        it.blend = blend;
        it.color = color;
        it.intensity = 1.0f;
        it.range = range;
        return it;
    };
    std::vector<LightItem> items;
    items.push_back(mkLight(kLightTypeGlobal, 0, 0xFFD6F4FFu, 0));   // 首个 Global（胜出）
    items.push_back(mkLight(kLightTypeGlobal, 0, 0xFF0000FFu, 0));   // 重复（取首）
    items.push_back(mkLight(kLightTypeSpriteMask, 0, 0xFFFFFFFFu, 64)); // 批③ 前滤除
    items.push_back(mkLight(kLightTypePoint, 0, 0xFF0000FFu, 64));
    items.push_back(mkLight(kLightTypePoint, 1, 0xFFFFFF00u, 64));
    items.push_back(mkLight(kLightTypePoint, 2, 0xFFFFFFFFu, 64));
    const auto& plan = comp.Prepare(items, /*lightBudget=*/2);
    Expect(plan.drawLights == 2, "budget truncates 3 drawable lights to 2");
    Expect(plan.droppedLights == 1, "one light dropped over budget");
    Expect(plan.ambient[0] == 1.0f && std::fabs(plan.ambient[1] - 244.0f / 255.0f) < 1e-5f,
           "ambient = FIRST Global color x intensity (dup ignored)");
    const auto& filtered = comp.FilteredItems();
    Expect(filtered.size() == 2 && filtered[0].blend == 0 && filtered[1].blend == 1,
           "filtered = drawable only, stable order, mask excluded");
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
}
