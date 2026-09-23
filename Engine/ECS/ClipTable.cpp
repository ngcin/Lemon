// Lemon 引擎 — 帧动画 clip 表实现（M5 批③；ClipTable.h 说明）
#include "ECS/ClipTable.h"

namespace lemon::ecs {

bool ClipTable::Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop) {
    if (clipId == 0 || frames.empty()) return false;
    if (fps <= 0.0f) fps = 1.0f; // 防御：0/负帧率 = 除零路径，钳到 1
    ClipDef def;
    def.fps = fps;
    def.loop = loop;
    def.frames = std::move(frames);
    clips_.insert_or_assign(clipId, std::move(def));
    return true;
}

const ClipDef* ClipTable::Find(uint32_t clipId) const {
    if (clipId == 0) return nullptr;
    auto it = clips_.find(clipId);
    return it == clips_.end() ? nullptr : &it->second;
}

} // namespace lemon::ecs
