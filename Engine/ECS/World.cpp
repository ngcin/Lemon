// Lemon 引擎 — World 实现（17 系统安装移入 Systems/，见 Systems.cpp）
#include "ECS/World.h"

#include <cstdio>
#include <cstring>

#include "ECS/ComponentRegistry.h"

namespace lemon::ecs {

bool RtUiChannel::Set(const char* key, const char* text, float frac, uint32_t color) {
    if (!key || !key[0] || !text) return false;
    uint32_t found = count_;
    for (uint32_t i = 0; i < count_; ++i)
        if (std::strcmp(slots_[i].key, key) == 0) {
            found = i;
            break;
        }
    if (found == count_) {
        if (count_ >= 8) return false; // 满槽忽略（8 行 HUD 上限，文档声明）
        ++count_;
    }
    RtUiSlot& s = slots_[found];
    std::snprintf(s.key, sizeof s.key, "%s", key);
    std::snprintf(s.text, sizeof s.text, "%s", text); // 截断 47 字符
    s.frac = frac;
    s.color = color; // 0 = 默认色（M5 批④）
    return true;
}

// 删单行（M5 批④）：命中即删、尾槽前移（保持呈现序 = 写入序）
bool RtUiChannel::Clear(const char* key) {
    if (!key) return false;
    for (uint32_t i = 0; i < count_; ++i)
        if (std::strcmp(slots_[i].key, key) == 0) {
            for (uint32_t j = i + 1; j < count_; ++j) slots_[j - 1] = slots_[j];
            --count_;
            slots_[count_] = RtUiSlot{}; // 尾槽清零（复占不读旧值）
            return true;
        }
    return false;
}

void RtUiCards::Show(const char* title, const char* a, const char* b, const char* c) {
    if (!title || !a || !b || !c) return;
    std::snprintf(this->title, sizeof this->title, "%s", title);
    std::snprintf(labels[0], sizeof labels[0], "%s", a); // 截断 47 字符 ×4
    std::snprintf(labels[1], sizeof labels[1], "%s", b);
    std::snprintf(labels[2], sizeof labels[2], "%s", c);
    pick = -1; // 新卡片清旧选择
    active = true;
}

World::World(const WorldDesc& desc)
    : desc_(desc),
      jobs_(std::make_unique<JobSystem>(desc.threadCount)),
      systemRngs_(kMaxSystemRngs),
      events_(desc.initialEventCapacity),
      teams_(TeamTable::Default()) { // 默认敌我表（03 §9）；资产侧加载后覆写
    // 组件目录自动登记（幂等）。StateHash/SceneArchive/桥都遍历注册表，
    // 宿主漏调即静默空转——M2 的 bench-sim 回放因此恒真过验（ISSUE-9），
    // 自此登记随首个 World 构造发生，不再依赖调用方纪律。
    RegisterAllComponents();
}

World::~World() = default;

Rng& World::SystemRng(uint32_t systemId) {
    auto& slot = systemRngs_[systemId & (systemRngs_.size() - 1)];
    if (!slot) slot = std::make_unique<Rng>(desc_.seed, kRngStreamBase + systemId);
    return *slot;
}

Scene& World::CreateScene(const char* name) {
    scenes_.push_back(std::make_unique<Scene>(name));
    return *scenes_.back();
}

// ---- 场景档案（ADR-017 D1；M7c 批⑥，头文件口径说明）----
uint32_t World::CreateSceneRecord(const char* name, const char* path) {
    SceneRecord rec;
    rec.handle = nextSceneHandle_++;
    rec.name = name ? name : "";
    rec.path = path ? path : "";
    sceneRecords_.push_back(std::move(rec));
    return sceneRecords_.back().handle;
}

const World::SceneRecord* World::SceneRecordAt(uint32_t index) const {
    return index < sceneRecords_.size() ? &sceneRecords_[index] : nullptr;
}

World::SceneRecord* World::FindSceneRecord(uint32_t handle) {
    for (SceneRecord& r : sceneRecords_)
        if (r.handle == handle) return &r;
    return nullptr;
}

const World::SceneRecord* World::FindSceneRecord(uint32_t handle) const {
    for (const SceneRecord& r : sceneRecords_)
        if (r.handle == handle) return &r;
    return nullptr;
}

// ---- 音频后端（M6c 批②；头文件口径说明）----
void World::SetAudioBackend(audio::AudioEngine* engine,
                             uint32_t (*resolveClip)(uint64_t guid, void* ctx), void* ctx) {
    audioSink_ = engine;
    audioResolve_ = resolveClip;
    audioResolveCtx_ = ctx;
}

uint32_t World::ResolveAudioClip(uint64_t guid) const {
    return audioResolve_ ? audioResolve_(guid, audioResolveCtx_) : 0;
}

void World::Step(float fixedDt) {
    if (!active_) return; // 无活动场景 = 空步（不崩；tick 不推进）
    // 时间缩放（M5 批①）：全 FixedTick 系统吃缩放 dt（C# Time.DeltaTime 同值）；
    // Essential（销毁提交）不消费 dt，缩放无语义差
    const float dt = fixedDt * timeScale_;
    pipeline_.RunStage(*this, *active_, SystemStage::Essential, dt);
    pipeline_.RunStage(*this, *active_, SystemStage::FixedTick, dt);
    // M7a 批③：Extract（渲染提取）阶段补跑——零注册系统时为 no-op（编辑器视口维持
    // 每渲染帧直调；管线驱动消费方 = 批④ lemon-game 装配 RenderExtractSystem）。
    // 提取不写 ECS 模拟态 → StateHash/金回放零影响
    pipeline_.RunStage(*this, *active_, SystemStage::Extract, dt);
    ++tick_;
}

} // namespace lemon::ecs
