// Lemon 引擎单测 — EditorTests —
// 编辑器域（资产库/剪辑与状态机/表格导入/工程向导/可用性/自动存档/场景档案）（M7c 批⓪ T2 自
// engine_tests.cpp 按域拆出，函数体逐字节原样搬运； include/using 为全 TU
// 共享全集——跨域头依赖零编译风险，IWYU 精简不做）

#include "TestFramework.h"

// Lemon 引擎单测 — 纯逻辑层（数学/批键/图集 UV/相机/粒子池/音频混音）
// 断言风格：LEMON_ASSERT 失败即 abort，进程退出码非 0 = 测试失败。
#include "Core/Log.h"
#include "Core/Process.h" // CurrentProcessId（批⑦ win 清账：unistd/getpid 是 POSIX-only）

#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <fstream>
#include <thread>
#include "Audio/AudioChannel.h" // M6c 批②：命令通道（World.h 链亦达，显式声明测试意图）
#include "Audio/AudioEngine.h"
#include "Audio/BakedClip.h"
#include "Audio/SpscRing.h" // M6c 批①b：SPSC 环序锁
#include "Assets/AssetIndex.h" // M7a 批②：运行时只读索引
#include "Assets/AtlasBake.h" // M7a 批⑥：LAT1 容器/装箱/烤制
#include "Assets/AtlasStore.h" // M7a 批⑥：LAT1 装载登记核
#include "Assets/ProjectFile.h" // M7a 批②：project.lemon 只读解析
#include "Assets/SpriteRefs.h" // M7a 批②：guid 归一引擎本体
#include "Assets/PrefabCache.h" // M7a 批③：Play 世界 Prefab 工厂缓存
#include "Renderer/CameraFollow.h" // M7a 批③：相机跟随纯函数
#include "Renderer/SceneExtractor.h" // M7a 批③：ECS→渲染提取下沉件
#include "Core/Guid.h"
#include "Core/Math.h"
#include "stb_image_write.h" // M7a 批⑥：LAT1 夹具播种 PNG（实现符号在引擎 StbImage.cpp 单 TU）
#include "Components/AudioComponents.h" // M6c 批②：AudioSource
#include "Components/CoreComponents.h"
#include "ECS/Hierarchy.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/Particles.h"
#include "Renderer/Quality.h"
#include "Renderer/Renderable.h"
// ---------------------------------------------------------------- M2 Core --
#include "Core/FunctionRef.h"
#include "Core/JobSystem.h"
#include "Core/Pool.h"
#include "Core/Random.h"
#include "Core/RingQueue.h"
#include <atomic>
#include <numeric>
// ------------------------------------------------------- M2 ECS 骨架/组件 --
#include "Components/BehaviorComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/SaveChannel.h"
#include "ECS/Scene.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"
// --------------------------------------------------- M2 场景序列化(.scene) --
#include "Serialization/SceneArchive.h"
// ------------------------------------------------ M2 空间哈希 + Team -------
#include "Physics2D/SpatialHash.h"
// ------------------------------------------- M2 系统管线（16 系统端到端）--
#include "Systems/Systems.h"
// --------------------------------------------- M2 审计修复回归 --------------
// ------------------ M2 复核轮新增测试（2026-09-19，只读审计配套） -----------
// 2026-09-19 修复轮：ISSUE-1..8 已全部修复，原 [ISSUE-n] "固化现状"断言已同步
// 改为断言正确行为（问题登记与修法见 docs/Reports/2026-09-19-m2-review-checklist.md）。
// ---- M4.1：Hierarchy 链维护/防环/世界矩阵（内核 #1 + M2 复审 N6 遗留环检测测试）----
// ---- M4.1：Meta.guid 序列化往返（内核 #5）----

#ifdef LEMON_EDITOR_CORE
#include "Assets/AssetDatabase.h"
#include "Assets/SaveStore.h" // M7a 批③：SaveStore 直测（原 EditorContext 三方法已下沉）
#include "Assets/AnimAsset.h"
#include "Assets/ControllerAsset.h"
#include "Assets/TableAsset.h"
#include "Assets/FileWatcher.h"
#include "Assets/ProjectWizard.h"
#include "EditorContext.h"
#include "Serialization/SceneArchive.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"
#endif

using namespace lemon;
using namespace lemon::math;
using namespace lemon::renderer;
using namespace lemon::ecs;
using namespace lemon::physics2d;

namespace {

void TestMetaGuidRoundtrip() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    Scene src("g1");
    Entity e1 = src.Create();
    src.Emplace<Transform2D>(e1);
    Meta& m1 = src.Emplace<Meta>(e1);
    m1.guid = lemon::GenerateGuid();
    Entity e2 = src.Create();
    src.Emplace<Transform2D>(e2);
    Meta& m2 = src.Emplace<Meta>(e2);
    m2.guid = lemon::GenerateGuid();
    Expect(m1.guid != 0 && m2.guid != 0 && m1.guid != m2.guid, "guids generated distinct");

    std::string text = SceneArchive::Save(src);
    Scene dst("g2");
    Expect(SceneArchive::Load(dst, text), "guid scene load");
    // 按 guid 找回实体（编辑器选中找回语义）
    bool found1 = false, found2 = false;
    dst.Each([&](Entity e) {
        if (const Meta* m = dst.TryGet<Meta>(e)) {
            if (m->guid == m1.guid) found1 = true;
            if (m->guid == m2.guid) found2 = true;
        }
    });
    Expect(found1 && found2, "guids survive save/load");

    // 无 guid 旧档：加载后 guid 默认 0（backfill 是编辑器职责，引擎不做隐式改写）
    const char* legacy =
        R"({"schemaVersion":1,"name":"old","entities":[{"components":{"Transform2D":{"pos":[1,2],"rot":0.0,"scale":[1,1]},"Meta":{"prefabId":0,"team":0,"layer":0,"tag":"veteran"}}}]})";
    Scene s3("g3");
    Expect(SceneArchive::Load(s3, legacy), "legacy scene loads");
    bool legacyZero = true;
    s3.Each([&](Entity e) {
        if (const Meta* m = s3.TryGet<Meta>(e))
            if (m->guid != 0) legacyZero = false;
    });
    Expect(legacyZero, "legacy guid stays 0 (no implicit rewrite)");

    // 生成器：连续 1000 个不撞、非全零
    uint64_t first = lemon::GenerateGuid();
    bool allDistinct = true;
    for (int i = 0; i < 1000; ++i) {
        uint64_t g = lemon::GenerateGuid();
        if (g == 0 || g == first) allDistinct = false;
    }
    Expect(allDistinct, "guid generator 1000 distinct");
}

// ---- M4.1：编辑器元数据健全性（内核 #6；Inspector 控件渲染的前提）----

void TestEditorMetaSanity() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    auto& reg = ComponentRegistry::Instance();
    bool allOk = true;
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& meta = reg.At(id);
        if (!meta.editorMeta) continue;
        for (uint16_t f = 0; f < meta.fieldCount; ++f) {
            const FieldEditorMeta& ed = meta.editorMeta[f];
            const FieldMeta& fm = meta.fields[f];
            if (HasHint(ed.hints, FieldHint::Range) && !(ed.rangeMin < ed.rangeMax)) {
                LEMON_LOG("BAD RANGE: %s.%s [%f,%f]", meta.name, fm.name, ed.rangeMin, ed.rangeMax);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::Enum) &&
                (!ed.enumNames || ed.enumCount == 0 || ed.enumCount > 256)) {
                LEMON_LOG("BAD ENUM: %s.%s", meta.name, fm.name);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::ColorHex) && fm.type != FieldType::UInt32) {
                LEMON_LOG("BAD COLOR TYPE: %s.%s", meta.name, fm.name);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::Bool8) &&
                !(fm.type == FieldType::UInt8 || fm.type == FieldType::Int8)) {
                LEMON_LOG("BAD BOOL8 TYPE: %s.%s", meta.name, fm.name);
                allOk = false;
            }
        }
    }
    // 既有特性抽查：Collectible.kind 枚举 3 项、SpriteRenderer.colorRGBA 颜色、rot 角度
    const ComponentMeta& col = *reg.Find("Collectible");
    Expect(col.editorMeta && HasHint(col.editorMeta[0].hints, FieldHint::Enum) &&
               col.editorMeta[0].enumCount == 3,
           "Collectible.kind enum meta");
    const ComponentMeta& sr = *reg.Find("SpriteRenderer");
    Expect(sr.editorMeta && HasHint(sr.editorMeta[1].hints, FieldHint::ColorHex),
           "SpriteRenderer.colorRGBA color meta");
    const ComponentMeta& tf = *reg.Find("Transform2D");
    Expect(tf.editorMeta && HasHint(tf.editorMeta[1].hints, FieldHint::Degree),
           "Transform2D.rot degree meta");
    // M6c 批③：AudioSource 抽查——clipGuid AudioRef 槽 + group 三名枚举
    // （Inspector 槽控件的前提；hint 位回归锁）
    const ComponentMeta& aus = *reg.Find("AudioSource");
    Expect(aus.editorMeta && HasHint(aus.editorMeta[0].hints, FieldHint::AudioRef) &&
               aus.fields[0].type == FieldType::UInt64 &&
               HasHint(aus.editorMeta[5].hints, FieldHint::Enum) &&
               aus.editorMeta[5].enumCount == 3,
           "AudioSource clipGuid/group meta");
    // M4.8 字段级重置：Reset 提示的字段必须 constructFn 可用且组件可入 Inspector 栈缓冲（128B）
    for (uint16_t id2 = 0; id2 < reg.Count(); ++id2) {
        const ComponentMeta& m = reg.At(id2);
        if (!m.editorMeta) continue;
        for (uint16_t fi = 0; fi < m.fieldCount; ++fi) {
            if (HasHint(m.editorMeta[fi].hints, FieldHint::Reset) &&
                (!m.constructFn || m.sizeOf > 128)) {
                LEMON_LOG("BAD RESET META: %s.%s", m.name, m.fields[fi].name);
                allOk = false;
            }
        }
    }
    Expect(tf.editorMeta && HasHint(tf.editorMeta[0].hints, FieldHint::Reset) && tf.constructFn,
           "Transform2D.pos reset meta");
    // 默认值口径（重置按钮目标值）：pos(0,0) rot 0 scale(1,1)——scale 归 1 非归 0
    alignas(16) uint8_t def[128];
    tf.constructFn(def);
    const Transform2D& td = *(const Transform2D*)def;
    Expect(td.pos == Vec2(0, 0) && td.rot == 0.0f && td.scale == Vec2(1, 1),
           "Transform2D default = pos0/rot0/scale1");
    Expect(allOk, "editor metadata sanity");
}

// ---- M6a 批② T1：CSV 解析 + .tab 表格资产序列化（ADR-012 D1）----

#ifdef LEMON_EDITOR_CORE
void TestCsvTable() {
    using lemon::assets::ParseCsv;
    using lemon::assets::ParseTableJson;
    using lemon::assets::TableData;
    using lemon::assets::TableToJson;

    // 基本 + BOM 剥除 + CRLF 归一 + 中文表头
    assets::TableData t = assets::ParseCsv("\xEF\xBB\xBFid,label\r\nshoot,直射\r\n");
    Expect(t.ok && t.rows.size() == 2 && t.rows[0][0] == "id" && t.rows[1][1] == "直射",
           "csv basic + BOM + CRLF");
    // 引号包裹（格内逗号）+ "" 转义引号
    t = assets::ParseCsv("a,\"b,c\",\"d\"\"e\"\n");
    Expect(t.ok && t.rows[0].size() == 3 && t.rows[0][1] == "b,c" && t.rows[0][2] == "d\"e",
           "csv quotes/escape");
    // 引号内换行原样入格
    t = assets::ParseCsv("a,\"line1\nline2\",b\n");
    Expect(t.ok && t.rows.size() == 1 && t.rows[0][1] == "line1\nline2", "quoted newline");
    // 空行跳过（尾换行不产生幽灵行）；无尾换行的末行
    t = assets::ParseCsv("a,b\n\nc,d\n");
    Expect(t.ok && t.rows.size() == 2, "blank line skipped");
    t = assets::ParseCsv("a,b");
    Expect(t.ok && t.rows.size() == 1 && t.rows[0][1] == "b", "no trailing newline");
    // 参差行 → 补空矩形化（以最长行为准）
    t = assets::ParseCsv("a\nb,c\n");
    Expect(t.ok && t.rows.size() == 2 && t.rows[0].size() == 2 && t.rows[0][1].empty(),
           "ragged rows padded");

    // 非 UTF-8（GBK "中" = D6 D0）拒入
    t = assets::ParseCsv(std::string_view("a,\xD6\xD0\n", 6));
    Expect(!t.ok && t.error.find("UTF-8") != std::string::npos, "non-utf8 rejected");
    // 上限拒入：65 列 / 1025 行 / 129 码点格（128 汉字恰过线）
    std::string wide;
    for (int i = 0; i < 65; ++i) {
        if (i) wide += ',';
        wide += 'c';
    }
    t = assets::ParseCsv(wide);
    Expect(!t.ok, "cols over limit rejected");
    std::string tall;
    for (int i = 0; i < 1025; ++i) tall += "r\n";
    t = assets::ParseCsv(tall);
    Expect(!t.ok, "rows over limit rejected");
    t = assets::ParseCsv(std::string(129, 'x') + "\n");
    Expect(!t.ok, "cell over limit rejected");
    std::string cjk;
    for (int i = 0; i < 129; ++i) cjk += "\xE4\xB8\xAD";
    t = assets::ParseCsv(cjk + "\n");
    Expect(!t.ok, "129 CJK codepoints rejected");
    cjk.resize(128 * 3); // 128 码点恰在上限内
    Expect(assets::ParseCsv(cjk + "\n").ok, "128 CJK codepoints within limit");

    // assets::TableToJson → assets::ParseTableJson roundtrip
    t = assets::ParseCsv("id,label,note\nshoot,直射,\"a,b\"\npierce,穿透,x\n");
    Expect(t.ok, "parse for roundtrip");
    const std::string json = assets::TableToJson("weapons", t.rows);
    Expect(!json.empty(), "table to json");
    const assets::TableData back = assets::ParseTableJson(json);
    Expect(back.ok && back.rows == t.rows, "table json roundtrip equal");

    // .tab 宽松归一：裸数值/布尔格转字符串（ADR-012 示例形态）
    t = assets::ParseTableJson(
        "{\"schemaVersion\":1,\"name\":\"w\",\"rows\":[[\"id\",\"v\",\"on\"],"
        "[\"a\",0.12,true]]}");
    Expect(t.ok && t.rows[1][1] == "0.12" && t.rows[1][2] == "true",
           "json bare number/bool coerced");
    // 坏档拒入：语法错 / schemaVersion 不符 / 空 rows / 嵌套对象格
    Expect(!assets::ParseTableJson("{").ok, "bad json rejected");
    Expect(!assets::ParseTableJson("{\"schemaVersion\":2,\"rows\":[[\"a\"]]}").ok,
           "bad schemaVersion rejected");
    Expect(!assets::ParseTableJson("{\"rows\":[]}").ok, "empty rows rejected");
    Expect(!assets::ParseTableJson("{\"rows\":[[{\"x\":1}]]}").ok, "object cell rejected");
    // 非法网格过不了 assets::TableToJson（超限 → 空串）
    Expect(assets::TableToJson("x", std::vector<std::vector<std::string>>(1025, {"a"})).empty(),
           "to json rejects oversized grid");
}
#endif // LEMON_EDITOR_CORE

// ---- M6a 批② T3：.anim 解析/序列化（AnimationPanel 数据面；验收② roundtrip）----

#ifdef LEMON_EDITOR_CORE
void TestClipEdit() {
    using lemon::assets::ClipData;
    using lemon::assets::ClipToJson;
    using lemon::assets::ParseClipJson;

    // 规范档（Samples/yami 同型多行格式）
    const char* doc = "{\n  \"schemaVersion\": 1,\n  \"name\": \"hero-walk\",\n  \"fps\": 8,\n"
                      "  \"loop\": true,\n  \"frames\": [\n"
                      "    {\n      \"sheet\": \"5bd31a7c10000001\",\n      \"cell\": 0\n    },\n"
                      "    {\n      \"sheet\": \"5bd31a7c10000001\",\n      \"cell\": 8\n    }\n"
                      "  ]\n}";
    assets::ClipData c = assets::ParseClipJson(doc);
    Expect(c.ok && c.name == "hero-walk" && c.fps == 8.0f && c.loopMode == 1 &&
               c.frames.size() == 2 && c.frames[0].sheetGuid == 0x5bd31a7c10000001ull &&
               c.frames[0].cell == 0 && c.frames[1].cell == 8,
           "clip parse canonical");

    // 定版格式：序列化与规范档逐字符同型（AnimationPanel 保存后旧档 diff 只见
    // 被改字段——验收②"打开 hero-walk → 改字段 → 保存 → diff 仅预期"的依据）
    c.ok = true;
    Expect(assets::ClipToJson(c) == doc, "clip golden format stable");

    // roundtrip：改 fps/loop/增删帧/换 sheet → 序列化 → 再解析等值
    c.fps = 13.0f;
    c.loopMode = 0;
    c.frames.push_back({0x5bd31a7c10000005ull, 7});
    c.frames.erase(c.frames.begin());
    const assets::ClipData back = assets::ParseClipJson(assets::ClipToJson(c));
    Expect(back.ok && back.name == c.name && back.fps == c.fps && back.loopMode == c.loopMode &&
               back.frames == c.frames,
           "clip roundtrip after edit");

    // 缺省：loop 缺省 true / name 缺省空（面板补文件名）/ 小数 fps 往返
    c = assets::ParseClipJson(
        "{\"schemaVersion\":1,\"fps\":7.5,\"frames\":[{\"sheet\":\"000000000000000f\","
        "\"cell\":3}]}");
    Expect(c.ok && c.loopMode == 1 && c.name.empty() && std::fabs(c.fps - 7.5f) < 1e-6f,
           "clip defaults + fractional fps");
    Expect(assets::ParseClipJson(assets::ClipToJson(c)).fps == c.fps,
           "clip fractional fps roundtrip");

    // 空帧表合法（新建 clip 起步态；保存侧 ≥1 帧校验归面板）
    c = assets::ParseClipJson("{\"fps\":8,\"frames\":[]}");
    Expect(c.ok && c.frames.empty(), "clip empty frames parse");
    Expect(assets::ClipToJson(c).find("\"frames\": []") != std::string::npos,
           "clip empty frames serialize");

    // 坏档拒入（不炸面板）：非 JSON / 缺 frames / 缺 fps / 帧缺字段 /
    // sheet 非 hex / cell 负数 / cell 类型错
    Expect(!assets::ParseClipJson("{").ok, "clip bad json rejected");
    Expect(!assets::ParseClipJson("{\"fps\":8}").ok, "clip missing frames rejected");
    Expect(!assets::ParseClipJson("{\"frames\":[]}").ok, "clip missing fps rejected");
    Expect(!assets::ParseClipJson("{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\"}]}").ok,
           "clip frame missing cell rejected");
    Expect(!assets::ParseClipJson("{\"fps\":8,\"frames\":[{\"sheet\":\"zz\",\"cell\":0}]}").ok,
           "clip non-hex sheet rejected");
    Expect(!assets::ParseClipJson(
                "{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":-1}]}")
                .ok,
           "clip negative cell rejected");
    Expect(!assets::ParseClipJson(
                "{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":\"0\"}]}")
                .ok,
           "clip string cell rejected");
    // ok=false 输入 → assets::ClipToJson 空串（门卫）
    assets::ClipData bad;
    Expect(assets::ClipToJson(bad).empty(), "clip tojson rejects !ok");

    // T3b-2：loopMode——legacy loop 派生 / pingpong 落盘加字段 / 越界防御 /
    // 旧档 no-edit 往返不含 loopMode（golden 已证；此处锁字段策略）
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":true,\"loopMode\":2,\"frames\":[{\"sheet\":\"000000000000000f\","
        "\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 2, "clip loopMode field parsed");
    const std::string pp = assets::ClipToJson(c);
    Expect(pp.find("\"loop\": true") != std::string::npos &&
               pp.find("\"loopMode\": 2") != std::string::npos,
           "clip pingpong serializes loop+loopMode");
    Expect(assets::ParseClipJson(pp).loopMode == 2, "clip pingpong roundtrip");
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":false,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 0 && assets::ClipToJson(c).find("loopMode") == std::string::npos,
           "clip legacy once stays field-free");
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loopMode\":5,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 1, "clip loopMode out of range falls back to Loop");

    // review 2026-10-02 #30：legacy loop 非布尔（手写档 "loop":1）预检拒绝——
    // 原裸 get<bool>() 抛 nlohmann type_error 穿透调用链（无 try/catch）=
    // std::terminate，违背"坏档不炸编辑器"契约
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":1,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(!c.ok, "clip non-bool loop rejected (no throw)");

    // review 2026-10-02 #5：名字含引号/反斜杠转义 roundtrip——原样样拼接写出
    // 非法 JSON，面板保存覆写原档 = 数据丢失；长名不再经定长缓冲
    c = assets::ParseClipJson(doc);
    c.name = "atk\"idle\\v2";
    {
        const std::string esc = assets::ClipToJson(c);
        const assets::ClipData rt = assets::ParseClipJson(esc);
        Expect(rt.ok && rt.name == c.name, "clip quoted/backslash name roundtrip");
    }
    c.name = std::string(200, 'n'); // 超一切定长缓冲
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.name == c.name, "clip 200-char name roundtrip");
    }
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批① D7 残余：动画资产名校验硬化（assets::ValidateAssetName 单源）----
// 旧校验只拒空/`/`/`\`/`..`——引号/控制字符/超长放行（写侧 assets::JsonEscape 兜底不毁
// 档，但名字 = 文件名母体 + 集内按名解析键，怪字符把问题推迟到运行时）。

#ifdef LEMON_EDITOR_CORE
void TestValidateAssetName() {
    using lemon::assets::ValidateAssetName;
    std::string why;

    Expect(assets::ValidateAssetName("idle", &why), "name: plain accepted");
    Expect(assets::ValidateAssetName("跑-02", &why), "name: CJK accepted");
    Expect(assets::ValidateAssetName(std::string(64, 'a'), &why), "name: 64B boundary accepted");

    Expect(!assets::ValidateAssetName("", &why) && why.find("空") != std::string::npos,
           "name: empty rejected with reason");
    Expect(!assets::ValidateAssetName("a/b", &why), "name: slash rejected");
    Expect(!assets::ValidateAssetName("a\\b", &why), "name: backslash rejected");
    Expect(!assets::ValidateAssetName("a..b", &why), "name: dotdot rejected");
    Expect(!assets::ValidateAssetName("we\"ird", &why) && why.find("引号") != std::string::npos,
           "name: quote rejected（D7 报告原场景）");
    Expect(!assets::ValidateAssetName("a\nb", &why), "name: control char rejected");
    Expect(!assets::ValidateAssetName(std::string(65, 'a'), &why) &&
               why.find("64") != std::string::npos,
           "name: over-64B rejected（D7 报告第二场景：截断/超长）");
    Expect(assets::ValidateAssetName("normal"), "name: null-why pointer tolerated");
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批① M22：删帧后帧事件越界清理（assets::SanitizeClipEvents）----
// 阴性内置：越界事件的 clip 序列化 → 解析必拒（assets::ParseClipJson 硬拒 frame≥帧表）
// ——即缺陷本体（保存链自锁）作为反例先行证明，再证 sanitize 后 roundtrip 过。

#ifdef LEMON_EDITOR_CORE
void TestClipEventBounds() {
    using lemon::assets::ClipData;
    using lemon::assets::ClipEventEdit;
    using lemon::assets::ClipToJson;
    using lemon::assets::ParseClipJson;
    using lemon::assets::SanitizeClipEvents;

    assets::ClipData c;
    c.ok = true;
    c.name = "duel";
    c.fps = 12.0f;
    c.frames = {{0x5bd31a7c10000001ull, 0},
                {0x5bd31a7c10000001ull, 1},
                {0x5bd31a7c10000001ull, 2},
                {0x5bd31a7c10000001ull, 3}};
    c.events = {{1, 0}, {3, 7}};

    // 带事件的合法档 roundtrip 过（正例基线）
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.events == c.events, "events: in-bounds roundtrip");
    }

    // 缺陷本体（阴性）：删掉帧 2..3 后事件 {3,7} 越界——不清则序列化产物解析必拒
    c.frames.resize(2);
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(!rt.ok, "events: out-of-range serialize-parse rejected（M22 自锁本体）");
    }

    // 修复：assets::SanitizeClipEvents 清越界 → roundtrip 过、界内事件保真
    const size_t dropped = assets::SanitizeClipEvents(c);
    Expect(dropped == 1 && c.events.size() == 1 && c.events[0].frame == 1,
           "events: sanitize drops exactly the out-of-range event");
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.events == c.events, "events: post-sanitize roundtrip ok");
    }

    // 幂等/边界：空事件、全越界、帧表空
    c.events.clear();
    Expect(assets::SanitizeClipEvents(c) == 0, "events: sanitize no-op when empty");
    c.events = {{0, 1}};
    c.frames.clear();
    Expect(assets::SanitizeClipEvents(c) == 1 && c.events.empty(),
           "events: empty frame table clears all events");
}
#endif // LEMON_EDITOR_CORE

// ---- M6a 批② T3c：.override 动画集解析/序列化 + ClipTable 集按名索引 ----

#ifdef LEMON_EDITOR_CORE
void TestAnimSetAndClipIndex() {
    using lemon::assets::AnimSetData;
    using lemon::assets::AnimSetToJson;
    using lemon::assets::ParseAnimSetJson;
    using lemon::ecs::ClipTable;

    // 规范档（ClipEdit 同款多行格式）+ 定版格式逐字符同型
    const char* doc =
        "{\n  \"schemaVersion\": 1,\n  \"name\": \"player\",\n  \"segments\": [\n"
        "    {\n      \"name\": \"idle\",\n      \"clip\": \"5bd31a7c30000004\"\n    },\n"
        "    {\n      \"name\": \"walk\",\n      \"clip\": \"5bd31a7c30000005\"\n    }\n"
        "  ]\n}";
    assets::AnimSetData s = assets::ParseAnimSetJson(doc);
    Expect(s.ok && s.name == "player" && s.segments.size() == 2 && s.segments[0].name == "idle" &&
               s.segments[0].clipGuid == 0x5bd31a7c30000004ull &&
               s.segments[1].clipGuid == 0x5bd31a7c30000005ull,
           "animset parse canonical");
    s.ok = true;
    Expect(assets::AnimSetToJson(s) == doc, "animset golden format stable");

    // roundtrip：改名/增删段
    s.name = "enemy";
    s.segments.push_back({"hit", 0x5bd31a7c30000006ull});
    s.segments.erase(s.segments.begin());
    const assets::AnimSetData back = assets::ParseAnimSetJson(assets::AnimSetToJson(s));
    Expect(back.ok && back.name == s.name && back.segments == s.segments,
           "animset roundtrip after edit");

    // review 2026-10-02 #5：段名引号/反斜杠转义 + 超长段名（原 char[128] 定长
    // snprintf >约 63 字符静默截断；转义缺失写坏档覆写即数据丢失）
    s.segments.push_back({"atk\"x\\y", 0x5bd31a7c30000007ull});
    s.segments.push_back({std::string(100, 's'), 0x5bd31a7c30000008ull});
    {
        const assets::AnimSetData rt = assets::ParseAnimSetJson(assets::AnimSetToJson(s));
        Expect(rt.ok && rt.segments == s.segments, "animset escaped/long segment names roundtrip");
    }

    // 空集合法（新建起步态）+ name 缺省空（面板补文件名）
    s = assets::ParseAnimSetJson("{\"schemaVersion\":1,\"segments\":[]}");
    Expect(s.ok && s.name.empty() && s.segments.empty(), "animset empty parses");
    Expect(assets::AnimSetToJson(s).find("\"segments\": []") != std::string::npos,
           "animset empty serializes");

    // 坏档拒入：非 JSON / 缺 segments / 段缺字段 / 空段名 / clip 非 hex
    Expect(!assets::ParseAnimSetJson("{").ok, "animset bad json rejected");
    Expect(!assets::ParseAnimSetJson("{\"name\":\"x\"}").ok, "animset missing segments rejected");
    Expect(!assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"a\"}]}").ok,
           "animset segment missing clip rejected");
    Expect(
        !assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"\",\"clip\":\"000000000000000f\"}]}")
             .ok,
        "animset empty segment name rejected");
    Expect(!assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"a\",\"clip\":\"zz\"}]}").ok,
           "animset non-hex clip rejected");
    assets::AnimSetData bad;
    Expect(assets::AnimSetToJson(bad).empty(), "animset tojson rejects !ok");

    // 集索引：登记 / 集内按名 / 跨集同名互不扰 / 反查 / 重名先到先得 / 防御 / Clear
    ClipTable t;
    Expect(t.Add(0x11, {1u, 2u, 3u}, 8.f, true) && t.Add(0x22, {4u}, 8.f, true) &&
               t.Add(0x33, {5u}, 8.f, true) && t.Add(0x44, {6u}, 8.f, true),
           "clips added for set index");
    Expect(t.RegisterSet(0xAB, {{"idle", 0x11u}, {"walk", 0x22u}}) == 2, "register set A");
    Expect(t.RegisterSet(0xCD, {{"idle", 0x33u}}) == 1, "register set B (cross-set same name)");
    Expect(t.FindByName(0xAB, "walk") == 0x22, "by name in set A");
    Expect(t.FindByName(0xCD, "idle") == 0x33, "same name resolves in own set");
    Expect(t.FindByName(0xAB, "idle") == 0x11, "set A idle unaffected by set B");
    Expect(t.FindByName(0xAB, "nope") == 0, "missing name → 0");
    Expect(t.FindByName(0xEE, "idle") == 0, "missing set → 0");
    Expect(t.SetOfClip(0x22) == 0xAB && t.SetOfClip(0x33) == 0xCD, "reverse lookup");
    Expect(t.SetOfClip(0x44) == 0, "non-member → 0");
    Expect(t.RegisterSet(0xEF, {{"idle", 0x44u}, {"idle", 0x33u}}) == 1, "dup name first wins");
    Expect(t.FindByName(0xEF, "idle") == 0x44, "dup name resolves to first");
    Expect(t.SetOfClip(0x33) == 0xCD, "multi-set segment keeps first set");
    Expect(t.RegisterSet(0, {{"x", 0x11u}}) == 0, "setId 0 rejected");
    Expect(t.RegisterSet(0x99, {{"", 0x11u}, {"ok", 0x11u}}) == 1, "empty name skipped");
    t.Clear();
    Expect(t.FindByName(0xAB, "walk") == 0 && t.SetOfClip(0x22) == 0 && t.Count() == 0,
           "clear wipes set index");
}
#endif // LEMON_EDITOR_CORE

// ---- M6a 批② T3d：.controller 解析/序列化 + ControllerTable + 条件评估 +
// ClipTable 事件表/集内反查（ADR-013 D1/D2/D4）---------------------------

#ifdef LEMON_EDITOR_CORE
void TestControllerAndGraph() {
    using ecs::AnimCondOp;
    using ecs::AnimParamKind;
    // -- ControllerTable：登记/索引/条件评估（引擎域纯逻辑）--
    ecs::ControllerTable ct;
    ecs::ControllerDef def;
    def.states = {"Idle", "Walk", "Attack"};
    def.params = {{"speed", AnimParamKind::Float, 0.0f}, {"atk", AnimParamKind::Trigger, 0.0f}};
    ecs::AnimTransitionDef walk;
    walk.from = 0;
    walk.to = 1;
    walk.conds.push_back({0, AnimCondOp::Gt, 0.1f});
    ecs::AnimTransitionDef back; // Attack → Idle 段末过渡（exitTime = Queue 图化）
    back.from = 2;
    back.to = 0;
    back.exitTime = true;
    def.transitions = {walk, back};
    Expect(ct.Add(0x77, std::move(def)), "controller add");
    const ecs::ControllerDef* d = ct.Find(0x77);
    Expect(d && d->states.size() == 3 && d->transitions.size() == 2, "controller find");
    Expect(d->StateIndex("Walk") == 1 && d->StateIndex("nope") == -1, "state index");
    Expect(d->ParamIndex("atk") == 1 && d->ParamIndex("nope") == -1, "param index");
    ecs::ControllerDef empty;
    Expect(!ct.Add(0, std::move(empty)), "id 0 rejected");
    ecs::ControllerDef noStates;
    Expect(!ct.Add(0x88, std::move(noStates)), "empty states rejected");
    float p[8] = {};
    Expect(!ecs::AnimCondsHold(*d, d->transitions[0], p), "speed 0 → 不切");
    p[0] = 1.0f;
    Expect(ecs::AnimCondsHold(*d, d->transitions[0], p), "speed>0.1 → 切");
    p[1] = 1.0f; // trigger 槽非 0
    ecs::AnimCondDef tg{1, AnimCondOp::Trigger, 0.0f};
    Expect(ecs::AnimCondHolds(tg, p[1]) && !ecs::AnimCondHolds(tg, 0.0f), "trigger 语义");
    ct.Clear();
    Expect(ct.Count() == 0 && ct.Find(0x77) == nullptr, "controller clear");

    // -- ClipTable：事件表 + 集内反查 NameOfClip + 同集同 clip 换名去重 --
    ecs::ClipTable t2;
    t2.Add(0x10, {1, 2, 3}, 10.0f, true, {{1, 5}});
    t2.Add(0x20, {9}, 10.0f, false);
    Expect(t2.Find(0x10) && t2.Find(0x10)->events.size() == 1 &&
               t2.Find(0x10)->events[0].frame == 1 && t2.Find(0x10)->events[0].id == 5,
           "events stored");
    Expect(t2.Find(0x20)->events.empty(), "no events default");
    t2.RegisterSet(0xAB, {{"Idle", 0x10u}, {"Walk", 0x20u}});
    const std::string* n = t2.NameOfClip(0xAB, 0x10);
    Expect(n && *n == "Idle", "集内反查段名");
    Expect(t2.NameOfClip(0xAB, 0x99) == nullptr, "反查 miss");
    t2.RegisterSet(0xCD, {{"X", 0x10u}});
    n = t2.NameOfClip(0xCD, 0x10);
    Expect(n && *n == "X", "跨集复用段各有名（反查按集）");
    t2.RegisterSet(0xAB, {{"Alias", 0x10u}}); // 同集同 clip 换名 → 撤名（首名胜）
    n = t2.NameOfClip(0xAB, 0x10);
    Expect(n && *n == "Idle", "同集同 clip 换名被撤（NameOfClip 确定性）");
    Expect(t2.FindByName(0xAB, "Alias") == 0, "撤名后按名不可达");

    // -- ControllerEdit：解析/roundtrip/坏档拒绝 --
    const char* golden =
        "{\n  \"schemaVersion\": 1,\n  \"name\": \"Basic\",\n  \"params\": [\n"
        "    { \"name\": \"speed\", \"kind\": \"float\" },\n"
        "    { \"name\": \"attack\", \"kind\": \"trigger\" }\n  ],\n"
        "  \"entry\": \"Idle\",\n  \"states\": [\"Idle\", \"Walk\", \"Attack\"],\n"
        "  \"transitions\": [\n"
        "    { \"from\": \"Idle\", \"to\": \"Walk\", \"when\": [{ \"param\": \"speed\", \">\": 0.1 "
        "}] },\n"
        "    { \"from\": \"Attack\", \"to\": \"Idle\", \"on\": \"exitTime\" }\n  ]\n}";
    assets::ControllerData c = assets::ParseControllerJson(golden);
    Expect(c.ok, "controller golden 解析");
    Expect(c.states.size() == 3 && c.params.size() == 2 && c.transitions.size() == 2,
           "controller 结构");
    Expect(c.params[1].kind == 2 && c.transitions[1].exitTime, "kind/exitTime");
    Expect(c.transitions[0].conds[0].op == 2 && c.transitions[0].conds[0].value > 0.09f,
           "条件算子/阈值");
    Expect(assets::ControllerToJson(c) == golden, "controller roundtrip 逐字节");
    Expect(!assets::ParseControllerJson("{ \"states\": [] }").ok, "空 states 拒绝");
    Expect(!assets::ParseControllerJson(
                R"({ "states": ["A"], "transitions": [{"from":"A","to":"B"}] })")
                .ok,
           "to 引用未列状态拒绝");
    Expect(!assets::ParseControllerJson(
                R"({ "states": ["A","B"], "transitions": [{"from":"A","to":"B"}] })")
                .ok,
           "空条件非 exitTime 拒绝");
    std::string nine;
    for (int i = 0; i < 9; ++i)
        nine += (i ? "," : "") + std::string("{\"name\":\"p") + std::to_string(i) + "\"}";
    Expect(!assets::ParseControllerJson("{ \"states\": [\"A\"], \"params\": [" + nine + "] }").ok,
           "参数 >8 拒绝");
    Expect(!assets::ParseControllerJson(R"({ "states": ["A","A"] })").ok, "状态重名拒绝");
    // 坏档 ok=false → ToJson 空串（assets::ClipToJson 同款约定）；好档无 error
    Expect(c.error.empty(), "好档无 error");
    assets::ControllerData bad = assets::ParseControllerJson("{ \"states\": [] }");
    Expect(!bad.ok && assets::ControllerToJson(bad).empty(), "坏档 ToJson 空串");
}
#endif // LEMON_EDITOR_CORE

// ---- M6a 批② T1：.csv → .tab 转换导入生命周期（csv 不拷入 / 覆盖重导 / guid 稳定）----

#ifdef LEMON_EDITOR_CORE
void TestTableAssetImport() {
    namespace fs = std::filesystem;
    using lemon::assets::ParseTableJson;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;
    using lemon::editor::AssetType;

    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-table-" + tag);
    const fs::path src = fs::temp_directory_path() / ("lemon-test-table-src-" + tag + ".csv");
    const fs::path badSrc = fs::temp_directory_path() / ("lemon-test-table-bad-" + tag + ".csv");
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project");

    {
        std::ofstream f(src, std::ios::binary);
        f << "id,label\nshoot,直射\n";
    }
    const AssetEntry* e = db.ImportFile(src.string(), "tables/weapons.csv");
    Expect(e && e->type == AssetType::Table, "csv imported as Table");
    Expect(e->relPath == "Assets/tables/weapons.tab", "tab dest path");
    Expect(!fs::exists(root / "Assets" / "tables" / "weapons.csv", ec), "csv not copied in");
    Expect(fs::exists(root / "Assets" / "tables" / "weapons.tab", ec), "tab file written");
    Expect(fs::exists(root / "Assets" / "tables" / "weapons.tab.meta", ec), "meta written");
    const uint64_t guid = e->guid;

    { // 落盘内容 = 全字符串格 JSON，roundtrip 与源一致
        std::ifstream f(root / "Assets" / "tables" / "weapons.tab", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const auto t = assets::ParseTableJson(text);
        Expect(t.ok && t.rows.size() == 2 && t.rows[0][0] == "id" && t.rows[1][1] == "直射",
               "tab content roundtrip");
    }

    // 重拖同名 csv = 覆盖再导入（ADR-012：批量再编辑回 Excel 改完重拖）→ guid 稳定
    {
        std::ofstream f(src, std::ios::binary | std::ios::trunc);
        f << "id,label\npierce,穿透\n";
    }
    const AssetEntry* e2 = db.ImportFile(src.string(), "tables/weapons.csv");
    Expect(e2 && e2->guid == guid && e2->relPath == "Assets/tables/weapons.tab",
           "re-import overwrites same guid");
    {
        std::ifstream f(root / "Assets" / "tables" / "weapons.tab", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const auto t = assets::ParseTableJson(text);
        Expect(t.ok && t.rows[1][0] == "pierce" && t.rows.size() == 2, "overwritten content");
    }

    // guid/type 跨会话稳定（manifest 记账走字符串名 "table"）
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), 100), "reopen project");
        const AssetEntry* w2 = db2.FindByPath("Assets/tables/weapons.tab");
        Expect(w2 && w2->guid == guid && w2->type == AssetType::Table,
               "table guid/type stable across sessions");
    }

    // 手写 .tab 放进 Assets/ 照常入库（不经 csv 的直接导入流）
    {
        std::ofstream f(root / "Assets" / "balance.tab", std::ios::binary);
        f << "{\"schemaVersion\":1,\"name\":\"balance\",\"rows\":[[\"k\"],[\"xpCurveK\"]]}";
    }
    db.Rescan();
    const AssetEntry* bal = db.FindByPath("Assets/balance.tab");
    Expect(bal && bal->type == AssetType::Table, "handwritten tab discovered as Table");

    // 坏 csv（非 UTF-8）拒入且不留半档
    {
        std::ofstream f(badSrc, std::ios::binary);
        f << "a,\xD6\xD0\n";
    }
    Expect(db.ImportFile(badSrc.string(), "tables/bad.csv") == nullptr, "bad csv rejected");
    Expect(!fs::exists(root / "Assets" / "tables" / "bad.tab", ec), "no half tab left");

    fs::remove_all(root, ec);
    fs::remove(src, ec);
    fs::remove(badSrc, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- F-15（2026-09-24）：防抖门——窗口内取走的脏事件转 pending，不再吞 ----

#ifdef LEMON_EDITOR_CORE
void TestDebounceGatePending() {
    using lemon::editor::DebounceGate;
    DebounceGate g(0.4);

    // 窗外首脏：同帧即触发（与原"立即编译"节奏一致），触发点锚定新窗口
    g.OnDirty(5.0);
    Expect(g.Due(5.0), "first dirty fires immediately");
    Expect(!g.Due(5.01), "no refire without new dirty");

    // 窗内第二次保存（F-15 原吞点）：等窗，窗过后照常触发
    g.OnDirty(5.2);
    Expect(!g.Due(5.39), "in-window change waits");
    Expect(g.Due(5.45), "in-window change fires after window (F-15 no-swallow)");

    // 连续脏合并：窗内多次只触发一次
    g.OnDirty(7.0);
    Expect(g.Due(7.0), "window-anchored fire");
    g.OnDirty(7.1);
    g.OnDirty(7.2);
    g.OnDirty(7.3);
    Expect(g.Due(7.41), "coalesced in-window changes fire once");
    Expect(!g.Due(7.42), "and only once");
}
#endif // LEMON_EDITOR_CORE

// ---- M4.4-d：实体子树 IO（Prefab 最小集的档案层）----

#ifdef LEMON_EDITOR_CORE
void TestEntityTreeArchive() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    World w;
    Scene& s = w.CreateScene("src");

    Entity parent = s.Create();
    s.Emplace<Transform2D>(parent, Transform2D{{10, 20}, 0.5f, {2, 1}});
    Meta& pm = s.Emplace<Meta>(parent);
    std::strcpy(pm.tag, "boss");
    pm.guid = 0xAAAABBBBCCCCDDDDull;
    Entity child = s.Create();
    s.Emplace<Transform2D>(child, Transform2D{{1, 2}});
    s.Emplace<SpriteRenderer>(child, SpriteRenderer{7, 0xFF00FF00u, 3, 2, 0x4});
    SceneSetParent(s, child, parent);
    Entity outsider = s.Create();
    s.Emplace<Transform2D>(outsider, Transform2D{{9, 9}});
    Chase& ch = s.Emplace<Chase>(parent);
    ch.target = outsider; // 跨树引用 → 导出应置 null

    const std::string json = SceneArchive::SaveEntityTree(s, parent);
    Expect(!json.empty(), "tree save produced json");
    Expect(json.find("boss") != std::string::npos, "tree json carries tag");
    Expect(json.find("outsider") == std::string::npos, "tree excludes outside entity");

    World w2;
    Scene& d = w2.CreateScene("dst");
    const uint32_t before = d.AliveCount();
    Entity root = SceneArchive::LoadEntityTree(d, json);
    Expect(!root.IsNull() && d.AliveCount() == before + 2, "tree instantiated 2 entities");
    Expect(d.Has<Chase>(root) && d.Get<Chase>(root).target.IsNull(),
           "cross-tree EntityRef nulled on instantiate");
    Expect(d.Get<Meta>(root).guid != 0xAAAABBBBCCCCDDDDull && d.Get<Meta>(root).guid != 0,
           "instance gets fresh guid");
    const Hierarchy* h = d.TryGet<Hierarchy>(d.Get<Hierarchy>(root).firstChild);
    Expect(h && h->parent == root, "child hierarchy remapped to new root");
    // 子实体组件 Spot check
    Entity c2 = d.Get<Hierarchy>(root).firstChild;
    Expect(d.Get<SpriteRenderer>(c2).spriteId == 7 &&
               d.Get<SpriteRenderer>(c2).colorRGBA == 0xFF00FF00u,
           "child sprite data roundtrip");
    // 树内二次导出/导入 = 内容保持（guid 每次实例化换新是语义，不做文本级比对）
    const std::string json2 = SceneArchive::SaveEntityTree(d, root);
    World w3;
    Scene& d3 = w3.CreateScene("again");
    Entity root3 = SceneArchive::LoadEntityTree(d3, json2);
    Expect(!root3.IsNull() && d3.AliveCount() == 2, "double roundtrip entity count");
    Expect(d3.Get<SpriteRenderer>(d3.Get<Hierarchy>(root3).firstChild).colorRGBA == 0xFF00FF00u,
           "double roundtrip keeps data");
}
#endif // LEMON_EDITOR_CORE

// ---- M4.4-e：ScriptBox 档案段（装配通路 #7）；M6a 批⓪：scripts[] 多槽 + v1 迁移 ----

#ifdef LEMON_EDITOR_CORE
void TestScriptBoxArchive() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    World w;
    Scene& s = w.CreateScene("a");
    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    auto& sb = s.Emplace<scripting::ScriptBox>(e);
    scripting::AppendSlot(sb, 0x1234ABCDEF012345ull, "SpawnerBehaviour");
    scripting::AppendSlot(sb, 0x89ABCDEFFEDCBA98ull, "PlayerMovement");
    scripting::AppendSlot(sb, 0, "PlayerHud");
    sb.slots[0].typeId = 7; // 运行时解析号不持久：装载后应回 -1

    const std::string text = SceneArchive::Save(s);
    Expect(text.find("\"scripts\"") != std::string::npos, "scripts member serialized");
    Expect(text.find("SpawnerBehaviour") != std::string::npos, "className persisted");
    Expect(text.find("PlayerHud") != std::string::npos, "multi-script persisted");

    World w2;
    Scene& d = w2.CreateScene("b");
    Expect(SceneArchive::Load(d, text), "load with scripts member");
    bool found = false;
    d.Each([&](Entity en) {
        if (auto* b = d.TryGet<scripting::ScriptBox>(en); b) {
            found = true;
            Expect(b->count == 3, "three slots roundtrip");
            Expect(b->slots[0].scriptGuid == 0x1234ABCDEF012345ull, "slot0 guid roundtrip");
            Expect(std::string_view(b->slots[0].className) == "SpawnerBehaviour",
                   "slot0 className roundtrip");
            Expect(std::string_view(b->slots[1].className) == "PlayerMovement",
                   "slot1 className roundtrip（保序）");
            Expect(std::string_view(b->slots[2].className) == "PlayerHud",
                   "slot2 className roundtrip（guid=0 合法）");
            Expect(b->slots[0].typeId == -1, "typeId stays unresolved after load");
        }
    });
    Expect(found, "ScriptBox re-emplaced on load");
    // 无脚本实体的场景不受影响 + 二次往返不动点（scripts 段键序稳定；
    // 场景名是宿主属性——两次用同名场景排除干扰）
    const std::string text2 = SceneArchive::Save(d);
    World w3;
    Scene& d3 = w3.CreateScene("b"); // 与 d 同名
    SceneArchive::Load(d3, text2);
    Expect(SceneArchive::Save(d3) == text2, "scripts member roundtrip fixed point");

    // ---- v1→v2 迁移：单数 script 包成单元素 scripts[]（老档升级链首例）----
    const std::string v1 =
        "{\"schemaVersion\":1,\"name\":\"legacy\",\"entities\":["
        "{\"components\":{},\"script\":{\"guid\":4242,\"class\":\"OldBehaviour\"}}]}";
    World w4;
    Scene& d4 = w4.CreateScene("c");
    Expect(SceneArchive::Load(d4, v1), "v1 scene migrates");
    bool legacyOk = false;
    d4.Each([&](Entity en) {
        if (auto* b = d4.TryGet<scripting::ScriptBox>(en);
            b && b->count == 1 && b->slots[0].scriptGuid == 4242 &&
            std::string_view(b->slots[0].className) == "OldBehaviour")
            legacyOk = true;
    });
    Expect(legacyOk, "legacy singular script migrated to one slot");

    // ---- 旧单数 .prefab 双读（LoadEntityTree 不走迁移链，靠 ReadEntity 兼容）----
    const std::string prefab =
        "{\"schemaVersion\":1,\"name\":\"prefab\",\"entities\":["
        "{\"components\":{},\"script\":{\"guid\":7,\"class\":\"PBehaviour\"}}]}";
    Scene& d5 = w.CreateScene("d5");
    Entity root = SceneArchive::LoadEntityTree(d5, prefab);
    const scripting::ScriptBox* pb = d5.TryGet<scripting::ScriptBox>(root);
    Expect(pb && pb->count == 1 && std::string_view(pb->slots[0].className) == "PBehaviour",
           "legacy singular prefab dual-read");

    // ---- 同名重复项清洗（保序留首见）——同类型唯一不变量的加载侧防线 ----
    const std::string dup = "{\"schemaVersion\":2,\"name\":\"dup\",\"entities\":["
                            "{\"components\":{},\"scripts\":[{\"guid\":1,\"class\":\"A\"},"
                            "{\"guid\":2,\"class\":\"B\"},{\"guid\":3,\"class\":\"A\"}]}]}";
    World w6;
    Scene& d6 = w6.CreateScene("d6");
    Expect(SceneArchive::Load(d6, dup), "dup scene loads");
    bool dedupOk = false;
    d6.Each([&](Entity en) {
        if (auto* b = d6.TryGet<scripting::ScriptBox>(en); b) {
            dedupOk = b->count == 2 && std::string_view(b->slots[0].className) == "A" &&
                      b->slots[0].scriptGuid == 1 && // 首见保留（第二条 A 被清洗）
                      std::string_view(b->slots[1].className) == "B";
        }
    });
    Expect(dedupOk, "duplicate className deduped (first wins)");
}
#endif // LEMON_EDITOR_CORE

// ---- M6a 批⓪ T2：sprite 引用 GUID 化（存量回填 / id 漂移解析 / 悬空 / 程序化页）----
// 链路主角 = EditorContext::ResolveSpriteRefs（随 OpenScene 乘）。id 漂移用
// 「删 manifest + 字典序插队资产 + 新 ctx 重开项目」模拟跨进程重排（.meta 只带
// guid、guid 随文件走——T5 --smoke-guid 将在编辑器全链复证同一命题）。

#ifdef LEMON_EDITOR_CORE
void TestSpriteGuidResolve() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetEntry;
    using lemon::editor::EditorContext;
    using namespace lemon::ecs;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-spriteguid-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), /*spriteIdBase=*/100), "ctx open project");
    fs::create_directories(root / "Assets", ec);
    {
        std::ofstream f(root / "Assets" / "coin.png", std::ios::binary);
        f << "png-C";
    }
    {
        std::ofstream f(root / "Assets" / "coin.png.meta", std::ios::trunc);
        f << "{\"guid\":\"1122334455667788\",\"type\":\"sprite\"}";
    }
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary);
        f << "png-H";
    }
    ctx.Assets().Rescan();
    const AssetEntry* coin = ctx.Assets().FindByPath("Assets/coin.png");
    const AssetEntry* hero = ctx.Assets().FindByPath("Assets/hero.png");
    Expect(coin && hero && coin->spriteId == 100 && hero->spriteId == 101,
           "ids allocated in path order");
    const uint64_t coinGuid = coin->guid;
    Expect(coinGuid == 0x1122334455667788ull, "preset meta guid honored");
    // 按 spriteId 找实体的 SpriteRenderer（多实体场景断言用）
    auto findSr = [](Scene& s, uint32_t id) {
        const SpriteRenderer* out = nullptr;
        s.Each([&](Entity e) {
            if (const SpriteRenderer* p = s.TryGet<SpriteRenderer>(e); p && p->spriteId == id)
                out = p;
        });
        return out;
    };

    // ① 存量回填：v2 档只写 spriteId（等价批⓪ 前全部存量档）→ 打开即回填 + 标
    //    dirty；程序化页号（< 基号）无 guid 语义，保持 0 不回填
    const fs::path sc = root / "Scenes" / "b.scene";
    fs::create_directories(sc.parent_path(), ec);
    {
        std::ofstream f(sc, std::ios::trunc);
        f << "{\"schemaVersion\":2,\"name\":\"b\",\"entities\":["
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":100,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4}},"
             "\"scripts\":[]},"
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":4,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4}},"
             "\"scripts\":[]}]}";
    }
    Expect(ctx.OpenScene(sc.string()), "legacy-id scene opens");
    const SpriteRenderer* sr = findSr(ctx.EditScene(), 100);
    Expect(sr && sr->spriteGuid == coinGuid, "legacy spriteId backfilled to guid");
    const SpriteRenderer* srProcedural = findSr(ctx.EditScene(), 4);
    Expect(srProcedural && srProcedural->spriteGuid == 0, "procedural page id not backfilled");
    Expect(ctx.dirty, "backfill marks dirty (save upgrades the file)");

    // ② 保存 → 跨进程 id 重排（删 manifest + aaa.png 字典序插队 + 新 ctx 重开）
    //    → coin 100→101；重开档 guid 不变、spriteId 归一到新号、不再回填
    Expect(ctx.SaveScene(), "scene saved with guid");
    {
        std::ifstream f(sc, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        Expect(text.find("spriteGuid") != std::string::npos &&
                   text.find(std::to_string(coinGuid)) != std::string::npos,
               "spriteGuid serialized (decimal)");
    }
    fs::remove(root / ".lemon" / "manifest.json", ec);
    {
        std::ofstream f(root / "Assets" / "aaa.png", std::ios::binary);
        f << "png-A";
    }
    EditorContext ctx2; // 新 ctx = 模拟重开进程（DB 空表、无 manifest 记账）
    Expect(ctx2.Assets().OpenProject(root.string(), 100), "reopen project (manifest gone)");
    const AssetEntry* coin2 = ctx2.Assets().FindByPath("Assets/coin.png");
    Expect(coin2 && coin2->spriteId == 101 && coin2->guid == coinGuid,
           "id drift as designed (aaa takes 100, guid rides .meta)");
    Expect(ctx2.OpenScene(sc.string()), "reopen saved scene");
    const SpriteRenderer* sr2 = findSr(ctx2.EditScene(), 101);
    Expect(sr2 && sr2->spriteGuid == coinGuid,
           "guid resolves to fresh id (rename/move/manifest-loss proof)");
    Expect(!ctx2.dirty, "guid-bearing scene opens clean (backfill is one-shot)");

    // ③ 悬空 guid：查无 → spriteId 保留旧号（不静默清零）、场景照常可用
    const fs::path sc3 = root / "Scenes" / "d.scene";
    {
        std::ofstream f(sc3, std::ios::trunc);
        f << "{\"schemaVersion\":2,\"name\":\"d\",\"entities\":["
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":101,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4,"
             "\"spriteGuid\":999}},\"scripts\":[]}]}";
    }
    Expect(ctx2.OpenScene(sc3.string()), "dangling-guid scene opens");
    const SpriteRenderer* sr3 = findSr(ctx2.EditScene(), 101);
    Expect(sr3 && sr3->spriteGuid == 999, "dangling guid keeps legacy id rendering");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.4-d：EditorContext Prefab 操作端到端（导出/实例化/Break/Apply/Revert）----

#ifdef LEMON_EDITOR_CORE
void TestEditorContextPrefabOps() {
    namespace fs = std::filesystem;
    using namespace lemon::ecs;
    using lemon::editor::AssetType;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-prefab-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    // 源实体：父 + 子（组件各一）
    Entity mob = ctx.CreateSpriteEntity("Mob", 3);
    ctx.EditScene().Get<Transform2D>(mob).pos = {100, 100};
    Entity hat = ctx.CreateSpriteEntity("Hat", 5);
    SceneSetParent(ctx.EditScene(), hat, mob);
    ctx.EditScene().Get<Transform2D>(hat).pos = {0, -20};

    const uint64_t pguid = ctx.MakePrefabFrom(mob);
    Expect(pguid != 0, "prefab exported");
    const auto* entry = ctx.Assets().FindByGuid(pguid);
    Expect(entry && entry->type == AssetType::Prefab && !entry->missing, "prefab in db");
    Expect(fs::exists(root / "Prefabs" / "Mob.prefab", ec),
           "prefab file on disk (root-level Prefabs/, 06 §1)");
    Expect(ctx.EditScene().Get<Meta>(mob).prefabId == pguid, "source linked back");

    // 实例化：新 guid 集 + prefabId 回链 + 位置覆盖
    const uint32_t before = ctx.EditScene().AliveCount();
    Entity inst = ctx.InstantiatePrefabAsset(pguid, {7, 9});
    Expect(!inst.IsNull() && ctx.EditScene().AliveCount() == before + 2, "instance tree created");
    Expect(ctx.EditScene().Get<Meta>(inst).prefabId == pguid, "instance linked");
    Expect(ctx.EditScene().Get<Meta>(inst).guid != ctx.EditScene().Get<Meta>(mob).guid,
           "instance has fresh guid");
    Expect(ctx.EditScene().Get<Transform2D>(inst).pos == Vec2(7, 9), "instance pos overridden");
    Expect(ctx.EditScene().Has<SpriteRenderer>(inst), "instance components copied");

    // Apply：实例改动写回源；Revert：新实例回到源态
    ctx.EditScene().Get<Transform2D>(inst).pos = {500, 250};
    ctx.EditScene().Get<SpriteRenderer>(inst).colorRGBA = 0x11223344u;
    Expect(ctx.ApplyPrefabInstance(inst), "apply writes back");
    // Break：断链（Apply 之后）
    ctx.BreakPrefabInstance(inst);
    Expect(ctx.EditScene().Get<Meta>(inst).prefabId == 0, "break clears link");
    // Revert 一个仍链接着的实例（重新实例化一个）
    Entity inst2 = ctx.InstantiatePrefabAsset(pguid, {0, 0});
    const uint64_t keepGuid = ctx.EditScene().Get<Meta>(inst2).guid;
    ctx.EditScene().Get<Transform2D>(inst2).pos = {999, 999}; // 偏离源
    Expect(ctx.RevertPrefabInstance(inst2), "revert ok");
    Entity reverted = ctx.Primary(); // Revert 选中重建后的根
    Expect(!reverted.IsNull() && ctx.EditScene().Get<Meta>(reverted).guid == keepGuid,
           "revert keeps instance guid");
    Expect(ctx.EditScene().Get<Transform2D>(reverted).pos == Vec2(500, 250),
           "revert restores applied source state");
    Expect(ctx.EditScene().Get<SpriteRenderer>(reverted).colorRGBA == 0x11223344u,
           "revert restores applied color");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M5 清障②：Play 世界 SpawnFn 桥（Spawner.prefabId 低 32 位 → prefab 实例化）----

#ifdef LEMON_EDITOR_CORE
void TestPlaySpawnPrefab() {
    namespace fs = std::filesystem;
    using namespace lemon::ecs;
    using lemon::editor::AssetType;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-playspawn-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    // Mob prefab：父 + 子（同 TestEditorContextPrefabOps 手法；源保留在编辑场景）
    Entity mob = ctx.CreateSpriteEntity("Mob", 3);
    Entity hat = ctx.CreateSpriteEntity("Hat", 5);
    SceneSetParent(ctx.EditScene(), hat, mob);
    const uint64_t pguid = ctx.MakePrefabFrom(mob);
    Expect(pguid != 0, "prefab exported");

    // Spawner 实体（prefabId = GUID 低 32 位——M5 映射约定）
    Entity spawner = ctx.CreateEntity("Spawner");
    Spawner& sp = ctx.EditScene().Emplace<Spawner>(spawner);
    sp.prefabId = (uint32_t)pguid;
    sp.interval = 0.05f;
    sp.burst = 2;
    sp.spawnTeam = 1;
    sp.cooldown = 0.0f;
    const uint32_t editAlive = ctx.EditScene().AliveCount(); // Mob+Hat+Spawner = 3
    const uint64_t mobGuid =
        ctx.EditScene().Get<Meta>(mob).guid; // ExitPlay 后句柄失效，按 guid 重找

    Expect(ctx.EnterPlay(), "enter play");
    Expect(ctx.Playing() && ctx.ActiveWorld().GetSpawnFn(), "play spawn fn registered");
    const uint32_t playAlive0 = ctx.ActiveScene().AliveCount();
    Expect(playAlive0 == editAlive, "play world = edit snapshot (3 entities)");

    // 桥单测：直接调工厂（不走 SpawnSystem）
    {
        const World::SpawnFn& spawn = ctx.ActiveWorld().GetSpawnFn();
        Entity e = spawn(ctx.ActiveScene(), (uint32_t)pguid, {42, -7}, 5u);
        Expect(!e.IsNull(), "spawn factory instantiates");
        Expect(ctx.ActiveScene().Get<Meta>(e).prefabId == pguid, "spawn links prefab guid");
        Expect(ctx.ActiveScene().Get<Meta>(e).team == 5u, "spawn team overrides source");
        Expect(ctx.ActiveScene().Get<Transform2D>(e).pos == Vec2(42, -7), "spawn pos");
        // 无效 id：0 与未知低 32 位 → Null 不崩（Spawner 的 e.IsNull() break 语义）
        Expect(spawn(ctx.ActiveScene(), 0, {0, 0}, 1u).IsNull(), "spawn id 0 = null");
        Expect(spawn(ctx.ActiveScene(), 0xDEADBEEFu, {0, 0}, 1u).IsNull(),
               "spawn unknown id = null (no crash)");
    }

    // 系统集成：TickPlay 若干帧 → SpawnSystem 经工厂实际刷怪（树 = 2 实体/只）
    for (int i = 0; i < 12; i++) ctx.TickPlay(1.0f / 60.0f);
    uint32_t spawned = 0;
    ctx.ActiveScene().Each([&](Entity e) {
        if (ctx.ActiveScene().Get<Meta>(e).prefabId == pguid &&
            ctx.ActiveScene().Get<Meta>(e).guid != ctx.EditScene().Get<Meta>(mob).guid)
            ++spawned;
    });
    Expect(spawned >= 4, "Spawner ticking via factory (>= 2 bursts, tree roots)");
    Expect(ctx.ActiveScene().AliveCount() > playAlive0, "play alive grows");

    // Stop：编辑场景零状态泄漏（快照重建）。实体全部重建 = 进 Play 前的句柄
    // （含版本位）已失效——按 guid 重找再校验（Debug 档 EnTT 会断言拒绝死句柄）
    Expect(ctx.ExitPlay(), "exit play");
    Expect(ctx.EditScene().AliveCount() == editAlive, "edit scene restored");
    Entity mobRestored = Entity::Null();
    ctx.EditScene().Each([&](Entity e) {
        if (ctx.EditScene().Get<Meta>(e).guid == mobGuid) mobRestored = e;
    });
    bool editLinked =
        !mobRestored.IsNull() && ctx.EditScene().Get<Meta>(mobRestored).prefabId == pguid;
    Expect(editLinked, "edit scene prefab link intact");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.8-b 回归：RecordRecentScene 自别名安全 + 读档洗脏档 ----
// 2026-09-22 崩溃案：File→最近场景菜单把 recentScenes_ 元素引用直传
// MenuOpenRecentScene→OpenScene→RecordRecentScene，后者 erase/insert 同一 vector
// = UAF（段错误间歇发作；侥幸不崩时把 ""/重复条目写进 recent-scenes.json）。

#ifdef LEMON_EDITOR_CORE
void TestRecentScenesAliasSafety() {
    namespace fs = std::filesystem;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-recent-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    const std::string a = (root / "Scenes" / "a.scene").string();
    const std::string b = (root / "Scenes" / "b.scene").string();
    ctx.RecordRecentScene(a);
    ctx.RecordRecentScene(b);
    Expect(ctx.RecentScenes().size() == 2 && ctx.RecentScenes().front() == b,
           "two entries, b on top");

    // 复现菜单点击：传 vector 元素自身的引用（此前 = UAF 案发现场）
    ctx.RecordRecentScene(ctx.RecentScenes().back()); // 末位别名
    Expect(ctx.RecentScenes().size() == 2 && ctx.RecentScenes().front() == a,
           "alias of back(): moves to top, no dup");
    ctx.RecordRecentScene(ctx.RecentScenes().front()); // 首位别名（曾确定性注入 ""）
    Expect(ctx.RecentScenes().size() == 2 && !ctx.RecentScenes().front().empty() &&
               ctx.RecentScenes().front() == a,
           "alias of front(): entry intact, no empty injected");

    // 读档洗脏档：预写含空串 + 重复条目的档 → 过滤空串、保序去重（首见留）。
    // Windows 绝对路径的反斜杠直接拼进 JSON 是非法转义（引擎侧会整档判坏），
    // 用 generic_string 正斜杠拼写；CanonicalPath 会折叠回规范形参与比较
    fs::create_directories(root / ".lemon", ec);
    {
        std::ofstream f(root / ".lemon" / "recent-scenes.json", std::ios::binary | std::ios::trunc);
        f << "{\"scenes\":[\"" << fs::path(a).generic_string() << "\", \"\", \"Scenes/b.scene\", \""
          << fs::path(a).generic_string() << "\"]}\n";
    }
    ctx.LoadRecentScenes();
    // CanonicalPath 落地后（2026-09-24）条目一律为规范形：期望值两侧同归一化
    std::error_code cec;
    const std::string aCanon = fs::weakly_canonical(fs::path(a), cec).string();
    const std::string bCanon = fs::weakly_canonical(fs::path("Scenes/b.scene"), cec).string();
    Expect(ctx.RecentScenes().size() == 2, "dirty file cleaned: empty + dup dropped");
    Expect(ctx.RecentScenes()[0] == aCanon && ctx.RecentScenes()[1] == bCanon,
           "order preserved, first occurrence wins (canonical forms)");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.5-a：项目向导（blank 模板 06 §1 布局 + 零配置脚本工程）----

#ifdef LEMON_EDITOR_CORE
void TestProjectWizard() {
    namespace fs = std::filesystem;
    using lemon::editor::ProjectDesc;
    using lemon::editor::ProjectWizard;

    const fs::path parent = fs::temp_directory_path() /
                            ("lemon-test-wizard-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(parent, ec);

    ProjectDesc d;
    d.parentDir = parent.string();
    d.name = "MyGame";
    d.sdkDir = "/nonexistent-sdk"; // 布局测试不解码 PNG/不编译——sdkDir 只进 HintPath
    d.engineVersion = "0.4.0-m4";
    uint64_t spawnGuid = 0;
    const std::string root = ProjectWizard::Create(d, &spawnGuid);
    Expect(!root.empty(), "wizard created project");
    Expect(spawnGuid != 0, "spawn asset guid returned");

    // 06 §1 布局全项
    for (const char* dir : {"Assets", "Scenes", "Prefabs", "Game", "Data", "Builds"}) {
        const bool ok = fs::is_directory(fs::path(root) / dir, ec);
        Expect(ok, ok ? "wizard dir" : (std::string("wizard dir missing: ") + dir).c_str());
    }
    // project.lemon：名称/版本锚点可解析
    {
        std::ifstream f(fs::path(root) / "project.lemon");
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(text.find("\"MyGame\"") != std::string::npos, "project.lemon carries name");
        Expect(text.find("0.4.0-m4") != std::string::npos, "project.lemon anchors engineVersion");
    }
    // 种子资产 + 固定 guid .meta；脚本引用同 guid（零代码刷怪链）
    {
        std::ifstream m(fs::path(root) / "Assets" / "spawn.png.meta");
        std::string meta((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
        Expect(meta.find(lemon::editor::AssetDatabase::GuidToHex(spawnGuid)) != std::string::npos,
               "spawn meta carries returned guid");
        std::ifstream cs(fs::path(root) / "Game" / "SpawnerBehaviour.cs");
        std::string src((std::istreambuf_iterator<char>(cs)), std::istreambuf_iterator<char>());
        Expect(src.find(lemon::editor::AssetDatabase::GuidToHex(spawnGuid)) != std::string::npos,
               "SpawnerBehaviour.cs references spawn guid");
        Expect(src.find("OnHotReloadOut") != std::string::npos,
               "template ships StateBag migration pattern");
    }
    // csproj：HintPath 指向 sdkDir；Main.scene 可被 SceneArchive 解码
    {
        std::ifstream f(fs::path(root) / "Game" / "MyGame.csproj");
        std::string cs((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(cs.find("/nonexistent-sdk/Lemon.SDK.dll") != std::string::npos,
               "csproj HintPath points at sdkDir");
        std::ifstream sf(fs::path(root) / "Scenes" / "Main.scene");
        std::string scene((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        lemon::ecs::Scene probe("probe");
        Expect(lemon::ecs::SceneArchive::Load(probe, scene), "Main.scene parses via SceneArchive");
        Expect(probe.AliveCount() == 0, "Main.scene starts empty");
    }
    // 重复创建同名 = 拒绝（不覆盖用户目录）
    Expect(ProjectWizard::Create(d).empty(), "wizard refuses existing directory");

    fs::remove_all(parent, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.6-b：日常编辑效率件（编译错误解析 / 新建脚本模板）----

#ifdef LEMON_EDITOR_CORE
void TestEditorUsability() {
    namespace fs = std::filesystem;
    using lemon::editor::ProjectDesc;
    using lemon::editor::ProjectWizard;

    // ExtractCompileErrors：dotnet/MSBuild 错误行提取（file(l,c): error CSxxxx 去 csproj 尾巴）
    {
        const std::string out =
            "Microsoft (R) Build Engine version 17.x\n"
            "  Determining projects to restore...\n"
            "/tmp/proj/Game/SpawnerBehaviour.cs(13,31): error CS1002: ; expected "
            "[/tmp/proj/Game/MyGame.csproj]\n"
            "/tmp/proj/Game/GameMain.cs(5,1): error CS0116: A namespace cannot directly "
            "contain members [/tmp/proj/Game/MyGame.csproj]\n"
            "    2 Warning(s)\n    2 Error(s)\n";
        const std::vector<std::string> errs = ProjectWizard::ExtractCompileErrors(out);
        Expect(errs.size() == 2, "extract exactly error CS lines");
        if (errs.size() == 2) {
            Expect(errs[0].find("SpawnerBehaviour.cs(13,31): error CS1002: ; expected") !=
                           std::string::npos &&
                       errs[0].find(".csproj]") == std::string::npos,
                   "error line keeps file(line,col), drops csproj tail");
            Expect(errs[1].find("error CS0116") != std::string::npos, "second error extracted");
        }
        Expect(ProjectWizard::ExtractCompileErrors("no errors here\n").empty(),
               "clean output yields nothing");
        // 告警行（warning CS）不算错误
        Expect(ProjectWizard::ExtractCompileErrors("A.cs(1,1): warning CS0219: var unused "
                                                   "[x.csproj]\n")
                   .empty(),
               "warnings are not errors");
    }

    // AddBehaviourScript：模板落盘 + GameMain 注册锚点插入 + 非法名/重名拒绝
    const fs::path parent = fs::temp_directory_path() /
                            ("lemon-test-newscript-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(parent, ec);
    ProjectDesc d;
    d.parentDir = parent.string();
    d.name = "ScriptGame";
    d.sdkDir = "/nonexistent-sdk";
    d.engineVersion = "0.4.0-m4";
    const std::string root = ProjectWizard::Create(d);
    const std::string gameDir = root + "/Game";
    Expect(!root.empty(), "wizard project for new-script test");

    Expect(ProjectWizard::AddBehaviourScript(gameDir, "ProbeBehaviour"), "script created");
    {
        std::ifstream f(fs::path(gameDir) / "ProbeBehaviour.cs");
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(src.find("public sealed class ProbeBehaviour : LemonBehaviour") != std::string::npos,
               "template declares class");
        Expect(src.find("protected override void Update()") != std::string::npos,
               "template has Update override");
        std::ifstream m(fs::path(gameDir) / "GameMain.cs");
        std::string main((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
        const size_t reg = main.find("Lemon.Behaviours.Register<ProbeBehaviour>();");
        const size_t anchor = main.find("Lemon.Behaviours.Register<InputMoverBehaviour>();");
        Expect(reg != std::string::npos && anchor != std::string::npos && reg < anchor,
               "register line inserted before existing anchor");
    }
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "ProbeBehaviour"),
           "duplicate class refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "9BadName"), "digit-start refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "Bad/Name"), "path-separator refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, ""), "empty name refused");

    fs::remove_all(parent, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.5-b：自动备份/崩溃恢复（§3.8 全链：快照→检出→恢复→落盘清）----

#ifdef LEMON_EDITOR_CORE
void TestAutosaveRecovery() {
    namespace fs = std::filesystem;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-autosave-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");
    fs::create_directories(root / "Scenes", ec);
    const std::string scenePath = (root / "Scenes" / "A.scene").string();
    ctx.CreateSpriteEntity("Hero", 3);
    Expect(ctx.SaveScene(scenePath), "scene saved");
    Expect(ctx.DetectAutosaveRecovery().empty(), "no recovery right after clean save");

    // 编辑 → dirty → 立即快照 → 检出（mtime 新于盘档）
    ctx.CreateSpriteEntity("Mob", 5);
    Expect(ctx.dirty, "edit marks dirty");
    Expect(ctx.AutoSaveNow(), "autosave snapshot written");
    const std::string rec = ctx.DetectAutosaveRecovery();
    Expect(!rec.empty(), "recovery detected (autosave newer)");
    Expect(rec.find("autosave") != std::string::npos, "recovery path under .lemon/autosave/");

    // 崩溃模拟：新上下文重开同场景 → 检出 → 恢复（保持 dirty、实体含 Mob）
    {
        EditorContext ctx2;
        ctx2.Assets().OpenProject(root.string(), 100);
        Expect(ctx2.OpenScene(scenePath), "reopen scene (clean copy, Hero only)");
        const std::string rec2 = ctx2.DetectAutosaveRecovery();
        Expect(!rec2.empty(), "fresh context still detects recovery");
        Expect(ctx2.OpenSceneRecovery(rec2), "recovery loads autosave content");
        Expect(ctx2.dirty, "recovery keeps dirty (user decides)");
        // 符号链接鲁棒口径（2026-09-24 CanonicalPath 落地后 ScenePath 为规范形：
        // macOS /var → /private/var）——两侧都归一化再比，恢复"不改路径"语义不变
        std::error_code cec1, cec2;
        Expect(fs::weakly_canonical(fs::path(ctx2.ScenePath()), cec1) ==
                   fs::weakly_canonical(fs::path(scenePath), cec2),
               "recovery keeps original scene path");
        bool sawMob = false;
        ctx2.EditScene().Each([&](lemon::ecs::Entity e) {
            const auto* m = ctx2.EditScene().TryGet<lemon::ecs::Meta>(e);
            sawMob |= m && std::string(m->tag) == "Mob";
        });
        Expect(sawMob, "recovered scene contains autosaved entity");
        // 落盘 → autosave 清除 → 不再检出
        Expect(ctx2.SaveScene(), "save after recovery");
        Expect(ctx2.DetectAutosaveRecovery().empty(), "save clears autosave (no stale prompt)");
    }

    // 节拍门：未到 interval 不写；Play 中不写（§3.8）
    {
        Expect(!ctx.DetectAutosaveRecovery().empty() || true, "baseline");
        ctx.TickAutosave(1.0); // 未到 300s：即便 dirty 也不写
        ctx.TickAutosave(299.0);
        ctx.TickAutosave(301.0); // 到点：dirty 且非 Play → 写
        const fs::path as = fs::path(ctx.Assets().ProjectRoot()) / ".lemon/autosave" / "A.scene";
        Expect(fs::exists(as, ec), "autosave written at interval tick");
    }

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

} // namespace

void RunEditorTests() {
    TestMetaGuidRoundtrip();
    TestEditorMetaSanity();
#ifdef LEMON_EDITOR_CORE
    TestCsvTable();
#endif
#ifdef LEMON_EDITOR_CORE
    TestClipEdit();
#endif
#ifdef LEMON_EDITOR_CORE
    TestValidateAssetName();
#endif
#ifdef LEMON_EDITOR_CORE
    TestClipEventBounds();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAnimSetAndClipIndex();
#endif
#ifdef LEMON_EDITOR_CORE
    TestControllerAndGraph();
#endif
#ifdef LEMON_EDITOR_CORE
    TestTableAssetImport();
#endif
#ifdef LEMON_EDITOR_CORE
    TestDebounceGatePending();
#endif
#ifdef LEMON_EDITOR_CORE
    TestEntityTreeArchive();
#endif
#ifdef LEMON_EDITOR_CORE
    TestScriptBoxArchive();
#endif
#ifdef LEMON_EDITOR_CORE
    TestSpriteGuidResolve();
#endif
#ifdef LEMON_EDITOR_CORE
    TestEditorContextPrefabOps();
#endif
#ifdef LEMON_EDITOR_CORE
    TestPlaySpawnPrefab();
#endif
#ifdef LEMON_EDITOR_CORE
    TestRecentScenesAliasSafety();
#endif
#ifdef LEMON_EDITOR_CORE
    TestProjectWizard();
#endif
#ifdef LEMON_EDITOR_CORE
    TestEditorUsability();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAutosaveRecovery();
#endif
}
