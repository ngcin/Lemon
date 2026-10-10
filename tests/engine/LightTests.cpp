// Lemon 引擎单测 — LightTests — 光照组件域（M8 批①：Light2D + BlobShadow2D
// 数据面——登记/默认值/生命周期/.scene 往返/哈希流；渲染消费归批② 不在本域）。
#include "TestFramework.h"

#include "Components/LightComponents.h"
#include "Core/Math.h"
#include "ECS/Scene.h" // 先于 ComponentRegistry.h：Entity 声明经此带入
#include "ECS/ComponentRegistry.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Serialization/SceneArchive.h"

#include <cmath>
#include <string>

using namespace lemon::ecs;

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

void RunLightTests() {
    TestLightCatalogRegistration();
    TestLightDefaultsAndLifecycle();
    TestLightSceneRoundtrip();
    TestLightStateHashStream();
}
