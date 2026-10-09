// Lemon 引擎 — Play 三缓存构建实现（M7a 批④；见 PlayCaches.h 契约注记）。
// clip 帧解析沿用装载期口径（sheet GUID + cell → spriteId 的解析需要资产源；
// JSON 解析全部下沉 AnimAsset/TableAsset/ControllerAsset 的 Parse*Json 唯一实现
// ——批⑪ H2（review 2026-10-09）：本件原内联 clip 解析器是弱拷贝，坏字段类型
// 抛 type_error 穿透 = terminate，违反「坏档红字跳过不炸 Play」契约，已删除）。
#include "Assets/PlayCaches.h"

#include <fstream>
#include <unordered_set>
#include <vector>

#include "Assets/AnimAsset.h"
#include "Assets/ControllerAsset.h"
#include "Assets/TableAsset.h"
#include "Core/Log.h"
#include "ECS/World.h"

namespace lemon::assets {
namespace {

bool ReadFileText(const std::string& absPath, std::string& out) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}
} // namespace

// ---- M5 批③：Play 世界 clip 表（.anim JSON → ClipTable；06 §2.2 / 03 §5）----
// 格式（M5.md §16.2 D2）：
//   { "schemaVersion": 1, "fps": 8, "loop": true,
//     "frames": [ {"sheet": "<guidHex>", "cell": 0}, ... ] }
// 帧引用 = 精灵表资产 GUID + 切片序号（行优先）——不直接存 spriteId（manifest 重排
// 不断链）。进 Play 时刻快照（同 PrefabCache 语义）。坏 clip 红字跳过：
// 实体 Animator2D.clipId 未命中表 → M2 纯计时回退（不炸）。
void BuildClipCache(ecs::World& world, const PlayCacheSource& src) {
    world.Clips().Clear();
    // 低 32 位碰撞告警（2026-09-29 复审 3b/3c）：ClipTable::Add 为 insert_or_assign
    // 静默后者胜——先在本侧去重告警（prefab 映射同款纪律），命中即提示作者
    // 改用手写模板外的生成 GUID 或等 M7 dense id 烘焙。
    std::unordered_set<uint32_t> seenClipIds;
    src.Each(AssetType::Clip, [&](uint64_t guid, const std::string& relPath,
                                  const std::string& absPath) {
        std::string text;
        if (!ReadFileText(absPath, text)) return;
        // 批⑪ H2（review 2026-10-09）：解析走 AnimAsset::ParseClipJson 唯一实现
        //（review 2026-10-02 #30 加固的类型预检版）——坏字段类型红字跳过不炸
        // Play。语义收紧两处随源解析器：坏 events 帧号越界 = 拒整 clip（原内联
        // 静默跳过该事件）；sheet 非 hex GUID = 拒（原由 FindSprite miss 兜住）。
        const ClipData clip = ParseClipJson(text);
        if (!clip.ok) {
            LEMON_WARN("clip 解析失败（%s）：%s——跳过", relPath.c_str(),
                       clip.error.c_str());
            return;
        }
        const float fps = clip.fps;
        const bool loop = clip.loopMode != 0; // ClipTable::Add 是 bool 面（PingPong
                                               // 档面按循环收；运行时权威在实体）
        std::vector<uint32_t> frames;
        bool ok = true;
        for (const ClipFrame& fr : clip.frames) {
            const IndexedEntry* sheet = src.FindSprite(fr.sheetGuid);
            // M6a 批② T3b-1：整图引用——未切片 sheet 的 cell 0 = 本体号（文件夹
            // 多单图动画，一帧一图）；切片表照旧 cell 界内连号
            uint32_t spriteId = 0;
            if (sheet && sheet->type == AssetType::Sprite) {
                if (sheet->Sliced())
                    spriteId = sheet->SliceSpriteId(fr.cell);
                else if (fr.cell == 0)
                    spriteId = sheet->spriteId;
            }
            if (spriteId == 0) {
                LEMON_WARN("clip 帧悬空（sheet 缺失/未切片且 cell≠0/cell 越界 %u）：%s 帧 %zu——跳过该 clip",
                           fr.cell, relPath.c_str(), frames.size());
                ok = false;
                break;
            }
            frames.push_back(spriteId);
        }
        if (!ok) return;
        // T3d 批③：帧事件表（ParseClipJson 已保证帧号界内、id 数值合法）
        std::vector<ecs::ClipEventDef> events;
        events.reserve(clip.events.size());
        for (const ClipEventEdit& ev : clip.events) {
            ecs::ClipEventDef d;
            d.frame = (uint16_t)ev.frame;
            d.id = (uint16_t)ev.id;
            events.push_back(d);
        }
        const uint32_t clipId = (uint32_t)guid; // 低 32 位（映射约定同 prefabId）
        if (!seenClipIds.insert(clipId).second)
            LEMON_WARN("Play clip 表低 32 位碰撞：%s（guid %016llx）与先登记 clip 同 id "
                       "%08x——后者胜，先登记档被覆盖",
                       relPath.c_str(), (unsigned long long)guid, clipId);
        if (!world.Clips().Add(clipId, std::move(frames), fps, loop, std::move(events)))
            LEMON_WARN("clip 登记失败（空帧/fps 非法）：%s", relPath.c_str());
        else
            LEMON_LOG("Play clip 表：'%s' → id %08x（%zu 帧 @%.1ffps）", relPath.c_str(),
                      clipId, world.Clips().Find(clipId)->frames.size(), fps);
    });

    // ---- T3c 动画集（.override → 集按名索引；EnterPlay 快照同语义，Play 中改不生效）。
    // Each() = relPath 升序 → 同段入多集/同集重名一律"路径序先到先得"，可复现。
    // 悬空段（clip 缺失/未登记）跳过不炸 Play；空集合法（脚本按名 miss = 报错）。
    src.Each(AssetType::AnimSet, [&](uint64_t guid, const std::string& relPath,
                                     const std::string& absPath) {
        std::string text;
        if (!ReadFileText(absPath, text)) return;
        const AnimSetData set = ParseAnimSetJson(text);
        if (!set.ok) {
            LEMON_WARN("animset 解析失败：%s——%s", relPath.c_str(), set.error.c_str());
            return;
        }
        const uint32_t setId = (uint32_t)guid; // 低 32 位（clipId/prefabId 同款映射）
        std::vector<std::pair<std::string, uint32_t>> segs;
        std::unordered_set<std::string> seenNames;
        for (const AnimSetSeg& sg : set.segments) {
            if (!seenNames.insert(sg.name).second) {
                LEMON_WARN("animset 重名段（按名解析取先者）：%s「%s」", relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            if (!src.HasClip(sg.clipGuid)) {
                LEMON_WARN("animset 段悬空（clip 缺失/非 clip）：%s「%s」", relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            const uint32_t cid = (uint32_t)sg.clipGuid;
            if (!world.Clips().Find(cid)) {
                LEMON_WARN("animset 段未登记（clip 内容坏/空帧）：%s「%s」", relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            if (const uint32_t prevSet = world.Clips().SetOfClip(cid); prevSet != 0) {
                LEMON_WARN("animset 段已属其他集（按名归先集）：%s「%s」", relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            segs.emplace_back(sg.name, cid);
        }
        const size_t n = world.Clips().RegisterSet(setId, segs);
        LEMON_LOG("Play 动画集：'%s' → id %08x（%zu/%zu 段）", relPath.c_str(), setId, n,
                  set.segments.size());
    });
}

// ---- M6a 批② T3d：Play 世界状态机表（.controller JSON → ControllerTable；
// ADR-013 D1 决策层。进 Play 时刻快照（同 BuildClipCache 语义，Play 中改
// .controller 不生效）。坏 controller 红字跳过不炸 Play——实体 AnimGraph 绑定
// 未命中表 = AnimGraphSystem 旁路（不绑图的纯集绑定不受影响）。字符串形态
//（assets::ControllerData）编译为下标形态（ControllerDef：from/to/param 全部定序槽位，
// 运行时零字符串查找）。
void BuildControllerCache(ecs::World& world, const PlayCacheSource& src) {
    world.Controllers().Clear();
    std::unordered_set<uint32_t> seenControllerIds; // 低 32 位碰撞告警（复审 3b/3c）
    src.Each(AssetType::Controller, [&](uint64_t guid, const std::string& relPath,
                                        const std::string& absPath) {
        std::string text;
        if (!ReadFileText(absPath, text)) return;
        const ControllerData c = ParseControllerJson(text);
        if (!c.ok) {
            LEMON_WARN("controller 解析失败：%s——%s", relPath.c_str(), c.error.c_str());
            return;
        }
        ecs::ControllerDef def;
        def.states = c.states;
        def.entry = c.entry.empty() ? 0 : def.StateIndex(c.entry);
        if (def.entry < 0) def.entry = 0; // 解析已拦，防御手改档
        for (const ControllerParamEdit& p : c.params)
            def.params.push_back({p.name, (ecs::AnimParamKind)p.kind, p.def});
        for (const ControllerTransitionEdit& t : c.transitions) {
            ecs::AnimTransitionDef td;
            td.from = (uint16_t)def.StateIndex(t.from);
            td.to = (uint16_t)def.StateIndex(t.to);
            td.exitTime = t.exitTime;
            for (const ControllerCondEdit& cd : t.conds)
                td.conds.push_back({(uint16_t)def.ParamIndex(cd.param),
                                    (ecs::AnimCondOp)cd.op, cd.value});
            def.transitions.push_back(std::move(td));
        }
        const uint32_t controllerId = (uint32_t)guid; // 低 32 位（同款映射约定）
        if (!seenControllerIds.insert(controllerId).second)
            LEMON_WARN("Play 状态机表低 32 位碰撞：%s（guid %016llx）与先登记 controller "
                       "同 id %08x——后者胜，先登记档被覆盖",
                       relPath.c_str(), (unsigned long long)guid, controllerId);
        if (!world.Controllers().Add(controllerId, std::move(def)))
            LEMON_WARN("controller 登记失败（空 states）：%s", relPath.c_str());
        else
            LEMON_LOG("Play 状态机：'%s' → id %08x（%zu 状态/%zu 过渡/%zu 参数）",
                      relPath.c_str(), controllerId,
                      world.Controllers().Find(controllerId)->states.size(),
                      world.Controllers().Find(controllerId)->transitions.size(),
                      world.Controllers().Find(controllerId)->params.size());
    });
}

// ---- M6a 批② T2：Play 世界配置表（.tab JSON → TableStore；ADR-012 D1）----
// 格式（Assets/TableAsset.h）：{ schemaVersion:1, name, rows[[]...] 全字符串格，第 0 行 =
// 列头 }。键 = 资产 GUID 低 32 位（clipId/prefabId 同款映射约定）。进 Play 时刻
// 快照（BuildClipCache 同语义）；坏表红字跳过不炸 Play（行×列×格字符上限归
// assets::ParseTableJson——超限即坏表）。Play 中改 .tab 不生效（表格区提示行已交代）。
void BuildTableCache(ecs::World& world, const PlayCacheSource& src) {
    world.Tables().Clear();
    std::unordered_set<uint32_t> seenTableIds; // 低 32 位碰撞告警（复审 3b/3c）
    src.Each(AssetType::Table, [&](uint64_t guid, const std::string& relPath,
                                   const std::string& absPath) {
        std::string text;
        if (!ReadFileText(absPath, text)) return;
        TableData t = ParseTableJson(text);
        if (!t.ok) {
            LEMON_WARN("表解析失败（%s）：%s——跳过", t.error.c_str(), relPath.c_str());
            return;
        }
        const uint32_t id = (uint32_t)guid; // 低 32 位（映射约定同 clipId）
        if (!seenTableIds.insert(id).second)
            LEMON_WARN("Play 表低 32 位碰撞：%s（guid %016llx）与先登记表同 id %08x"
                       "——后者胜，先登记档被覆盖",
                       relPath.c_str(), (unsigned long long)guid, id);
        const size_t rowCount = t.rows.size();
        const uint32_t colCount = t.Cols();
        if (!world.Tables().Add(id, std::move(t.rows))) {
            LEMON_WARN("表登记失败（空网格）：%s", relPath.c_str());
            return;
        }
        LEMON_LOG("Play 表：'%s' → id %08x（%zu 行 × %u 列）", relPath.c_str(), id,
                  rowCount, colCount);
    });
}

} // namespace lemon::assets
