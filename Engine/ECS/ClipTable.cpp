// Lemon 引擎 — 帧动画 clip 表实现（M5 批③；ClipTable.h 说明）
#include "ECS/ClipTable.h"

namespace lemon::ecs {

bool ClipTable::Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop) {
    return Add(clipId, std::move(frames), fps, loop, {});
}

bool ClipTable::Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop,
                    std::vector<ClipEventDef> events) {
    if (clipId == 0 || frames.empty()) return false;
    if (fps <= 0.0f) fps = 1.0f; // 防御：0/负帧率 = 除零路径，钳到 1
    ClipDef def;
    def.fps = fps;
    def.loop = loop;
    def.frames = std::move(frames);
    def.events = std::move(events);
    clips_.insert_or_assign(clipId, std::move(def));
    return true;
}

const ClipDef* ClipTable::Find(uint32_t clipId) const {
    if (clipId == 0) return nullptr;
    auto it = clips_.find(clipId);
    return it == clips_.end() ? nullptr : &it->second;
}

// ---- T3c 动画集按名索引（确定性：只依赖登记序 = 编辑器按 relPath 升序）----

size_t ClipTable::RegisterSet(uint32_t setId,
                              const std::vector<std::pair<std::string, uint32_t>>& segments) {
    if (setId == 0) return 0;
    auto& names = setsByName_[setId];
    size_t n = 0;
    for (const auto& [name, clipId] : segments) {
        if (name.empty() || clipId == 0) continue;
        // 同集重名先到先得（调用方预检告警）；一段入多集 = 首集胜（反查归首集，
        // 后集的按名解析仍可达——T3c 既有语义，engine-tests 4201-4206 锚）。
        // T3d 批②新增：同集内同 clip 多段名去重（输入序首个胜）——NameOfClip
        // 反查依赖"集内 clipId ↔ 段名一一对应"，否则首个匹配依赖 unordered 迭代序。
        auto [nameIt, nameNew] = names.emplace(name, clipId);
        if (!nameNew) continue;
        auto [segIt, segNew] = setOfSeg_.emplace(clipId, setId);
        if (segNew || segIt->second != setId)
            ++n; // 新段 / 跨集复用段（集内名单可解析，反查归首集）
        else
            names.erase(nameIt); // 同集同 clip 换名：撤名保反查确定性
    }
    return n;
}

uint32_t ClipTable::FindByName(uint32_t setId, const char* name) const {
    if (setId == 0 || !name) return 0;
    auto it = setsByName_.find(setId);
    if (it == setsByName_.end()) return 0;
    auto id = it->second.find(name);
    return id == it->second.end() ? 0 : id->second;
}

uint32_t ClipTable::SetOfClip(uint32_t clipId) const {
    if (clipId == 0) return 0;
    auto it = setOfSeg_.find(clipId);
    return it == setOfSeg_.end() ? 0 : it->second;
}

const std::string* ClipTable::NameOfClip(uint32_t setId, uint32_t clipId) const {
    if (setId == 0 || clipId == 0) return nullptr;
    auto it = setsByName_.find(setId);
    if (it == setsByName_.end()) return nullptr;
    // 值匹配扫描：集内 clipId 唯一（RegisterSet 去重）⇒ 结果与迭代序无关
    for (const auto& [name, cid] : it->second)
        if (cid == clipId) return &name;
    return nullptr;
}

} // namespace lemon::ecs
