// Lemon 引擎 — 03 §4 17 系统实现 + World::InstallDefaultSystems
#include "Systems/Systems.h"

#include <algorithm>
#include <cmath>

#include "Components/AudioComponents.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Audio/AudioEngine.h"
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "ECS/Scene.h"
#include "ECS/SceneSwitcher.h"
#include "ECS/World.h"
#include "Physics2D/SpatialHash.h"
#include "Scripting/ScriptBox.h"

namespace lemon::ecs {

namespace {

} // namespace

// ------------------------------------------------------ TargetBoard ---------
void TargetBoard::DeclareTeams(const std::vector<uint32_t>& teamIds) {
    teams_.clear();
    for (uint32_t id : teamIds) teams_.push_back({id, {}});
}

void TargetBoard::Rebuild(Scene& scene, bool collectAll, JobSystem* jobs) {
    for (auto& t : teams_) t.list.clear();
    all_.clear();
    auto view = scene.View<Meta, Transform2D>();
    entt::registry& reg = scene.Registry();

    // 串行路径（存量/单线程档/小场）：view 迭代序收集 + 就地建桶
    const size_t est = std::min(scene.Pool<Meta>().size(),
                                scene.Pool<Transform2D>().size());
    if (!jobs || jobs->ThreadCount() <= 1 || est < kParallelMin) {
        for (auto [ent, meta, tf] : view.each()) {
            TargetEntry entry{Scene::FromEntt(ent), tf.pos};
            if (collectAll) all_.push_back(entry);
            for (auto& t : teams_)
                if (t.id == meta.team) t.list.push_back(entry);
        }
        for (auto& t : teams_) t.grid.Build(t.list); // many-vs-many 网格桶（小列表空建）
        return;
    }

    // 并行路径（2026-09-26 五万场：串行收集+双队排序是 AI 段余量大头）。确定性：
    // view 无随机访问 → 主线程先按 view 序收集实体；ParallelFor 块界 = grain 对齐
    // （JobSystem.cpp），chunk 缓冲按块索引分桶、按序归并 → list 序 = view 序，
    // 与串行路径逐位同构（等距平局语义不变）。建桶排序键 (cellKey, 池索引) 唯一
    // → 每队 Build 结果唯一确定；各队独立，逐队入队并行（任务内不嵌套，纪律见
    // JobSystem.h 文头），最后一队留给主线程参与执行。
    collectEnts_.clear();
    for (auto [ent, meta, tf] : view.each()) {
        (void)meta; (void)tf;
        collectEnts_.push_back(Scene::FromEntt(ent));
    }
    const uint32_t n = (uint32_t)collectEnts_.size();
    if (n < kParallelMin) { // view 实际量低于估算（组件缺失）：回串行
        for (uint32_t i = 0; i < n; ++i) {
            auto [meta, tf] = reg.get<Meta, Transform2D>(Scene::ToEntt(collectEnts_[i]));
            TargetEntry entry{collectEnts_[i], tf.pos};
            if (collectAll) all_.push_back(entry);
            for (auto& t : teams_)
                if (t.id == meta.team) t.list.push_back(entry);
        }
        for (auto& t : teams_) t.grid.Build(t.list);
        return;
    }
    const uint32_t teamCount = (uint32_t)teams_.size();
    const uint32_t chunks = (n + kParallelGrain - 1) / kParallelGrain;
    chunkTeams_.resize((size_t)chunks * teamCount); // 只增不减：稳态保容量
    for (auto& c : chunkTeams_) c.clear();
    if (collectAll) {
        chunkAlls_.resize(chunks);
        for (auto& c : chunkAlls_) c.clear();
    }
    jobs->ParallelFor(n, kParallelGrain, [&](uint32_t b, uint32_t e) {
        const size_t base = size_t(b / kParallelGrain) * teamCount;
        for (uint32_t i = b; i < e; ++i) {
            const entt::entity ent = Scene::ToEntt(collectEnts_[i]);
            const Meta& meta = reg.get<Meta>(ent);
            const Transform2D& tf = reg.get<Transform2D>(ent);
            const TargetEntry entry{collectEnts_[i], tf.pos};
            for (uint32_t t = 0; t < teamCount; ++t)
                if (teams_[t].id == meta.team) chunkTeams_[base + t].push_back(entry);
            if (collectAll) chunkAlls_[b / kParallelGrain].push_back(entry);
        }
    });
    for (uint32_t ci = 0; ci < chunks; ++ci)
        for (uint32_t t = 0; t < teamCount; ++t) {
            auto& src = chunkTeams_[size_t(ci) * teamCount + t];
            teams_[t].list.insert(teams_[t].list.end(), src.begin(), src.end());
        }
    if (collectAll)
        for (uint32_t ci = 0; ci < chunks; ++ci) {
            auto& src = chunkAlls_[ci];
            all_.insert(all_.end(), src.begin(), src.end());
        }
    if (teamCount > 0) { // 各队独立，逐队入队并行建桶（任务内不嵌套，JobSystem 纪律）
        std::vector<JobSystem::JobHandle> builds;
        builds.reserve(teamCount - 1);
        for (uint32_t t = 0; t + 1 < teamCount; ++t) {
            TeamList& tl = teams_[t];
            builds.push_back(jobs->Schedule([&tl] { tl.grid.Build(tl.list); }));
        }
        teams_.back().grid.Build(teams_.back().list); // 主线程干最后一队（不空等）
        for (auto& h : builds) JobSystem::Complete(h);
    }
}

const std::vector<TargetEntry>& TargetBoard::TeamEntries(uint32_t team) const {
    for (const auto& t : teams_)
        if (t.id == team) return t.list;
    static const std::vector<TargetEntry> kEmpty;
    return kEmpty;
}

// ------------------------------------------------- 目标板网格桶（2026-09-25）--
namespace {
inline uint64_t TargetCellKey(int cx, int cy) {
    return (uint64_t(uint32_t(cx)) << 32) | uint32_t(cy);
}
} // namespace

void TargetBoard::TeamList::Grid::Build(const std::vector<TargetEntry>& list) {
    keys.clear();
    offs.clear();
    items.clear();
    minX = minY = 0;
    maxX = maxY = -1;
    occ.clear();
    if (list.size() < kMinList) return; // 线性快径（存量场景行为逐位不变）
    // 坏坐标防御（review 2026-10-02 #19）：非有限坐标（脚本写 Transform、手改
    // 场景档可达）静默剪除——原实现对 NaN 的 float→int 是 UB、对 1e9 量级坐标
    // 会算出天量 bbox。剪后不足建格 → Nearest 线性兜底（occ 空 = 兜底路径）
    scratch_.clear();
    for (uint32_t i = 0; i < list.size(); ++i) {
        const Vec2 p = list[i].pos;
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        const int cx = (int)std::floor(p.x / kCell);
        const int cy = (int)std::floor(p.y / kCell);
        scratch_.push_back({TargetCellKey(cx, cy), i});
        minX = std::min(minX, cx); maxX = std::max(maxX, cx);
        minY = std::min(minY, cy); maxY = std::max(maxY, cy);
    }
    if (scratch_.size() < kMinList) return;
    const int64_t spanW = (int64_t)maxX - minX + 1, spanH = (int64_t)maxY - minY + 1;
    if (spanW * spanH > kMaxCells) return; // 跨度极端（坏数据特征）：弃格降级线性
    const int w = (int)spanW, h = (int)spanH;
    occ.assign((size_t(w) * h + 63) / 64, 0);
    const auto bitOf = [&](int cx, int cy) {
        const size_t i = size_t(cy - minY) * w + (cx - minX);
        return std::pair<size_t, uint64_t>(i / 64, 1ull << (i % 64));
    };
    // 键序排序（次键 = 池序 → 同 cell 内保池序 = 确定性扫描序）
    std::sort(scratch_.begin(), scratch_.end());
    items.resize(list.size());
    size_t k = 0;
    while (k < scratch_.size()) {
        const uint64_t key = scratch_[k].first;
        const uint32_t begin = (uint32_t)k;
        const int cx = int(int32_t(key >> 32)), cy = int(int32_t(key & 0xFFFFFFFFu));
        const auto [bi, bm] = bitOf(cx, cy);
        occ[bi] |= bm;
        while (k < scratch_.size() && scratch_[k].first == key) {
            items[k] = scratch_[k].second;
            ++k;
        }
        keys.push_back(key);
        offs.push_back(begin);
    }
    offs.push_back((uint32_t)scratch_.size());
}

Entity TargetBoard::TeamList::Grid::Nearest(const std::vector<TargetEntry>& list,
                                            Vec2 from, float range,
                                            Entity exclude, Vec2* outPos) const {
    Entity best = Entity::Null();
    float bestD2 = range * range;
    // 坏查询防御（review 2026-10-02 #19）：NaN 查询点的 float→int 网格换算是 UB
    if (!std::isfinite(from.x) || !std::isfinite(from.y)) return best;
    // 未建格（Build 坏坐标剪除 / 跨度弃格）：线性兜底——语义与快径同（严格
    // 小于 + 池序），坏数据只降性能不丢功能
    if (occ.empty()) {
        for (const TargetEntry& te : list) {
            if (te.e == exclude) continue;
            const float d2 = LengthSq(te.pos - from);
            if (d2 < bestD2) {
                bestD2 = d2;
                best = te.e;
                if (outPos) *outPos = te.pos;
            }
        }
        return best;
    }
    const int cx0 = (int)std::floor(from.x / kCell);
    const int cy0 = (int)std::floor(from.y / kCell);
    auto scanCell = [&](int cx, int cy) {
        if (cx < minX || cx > maxX || cy < minY || cy > maxY) return; // bbox 外恒空
        const size_t i = size_t(cy - minY) * (maxX - minX + 1) + (cx - minX);
        if (!(occ[i / 64] & (1ull << (i % 64)))) return;              // 空 cell 免键查找
        const uint64_t key = TargetCellKey(cx, cy);
        const auto it = std::lower_bound(keys.begin(), keys.end(), key);
        if (it == keys.end() || *it != key) return;
        const uint32_t b = offs[it - keys.begin()], e = offs[it - keys.begin() + 1];
        for (uint32_t i2 = b; i2 < e; ++i2) {
            const TargetEntry& te = list[items[i2]];
            if (te.e == exclude) continue;
            const float d2 = LengthSq(te.pos - from);
            if (d2 < bestD2) { // 严格小于（等距语义见 Systems.h 注释）
                bestD2 = d2;
                best = te.e;
                if (outPos) *outPos = te.pos;
            }
        }
    };
    // 环搜：ring r 只扫边缘 4 条（r=0 单格）；(r-1)*cell 为该环候选距查询点的
    // 保守下界，其平方 ≥ bestD2 即停（已见更近者）；ring 全界超 range 亦停。
    for (int r = 0;; ++r) {
        const float ringMin = r == 0 ? 0.0f : (float)(r - 1) * kCell;
        if (ringMin * ringMin >= bestD2) break;
        if ((float)(r - 1) * kCell > range) break;
        for (int dx = -r; dx <= r; ++dx) { // 上下边（dy=±r 全宽；r=0 同格只扫一次）
            scanCell(cx0 + dx, cy0 - r);
            if (r > 0) scanCell(cx0 + dx, cy0 + r);
        }
        for (int dy = -r + 1; dy <= r - 1; ++dy) { // 左右边（去角）
            scanCell(cx0 - r, cy0 + dy);
            scanCell(cx0 + r, cy0 + dy);
        }
    }
    return best;
}

Entity TargetBoard::Nearest(uint32_t team, Vec2 from, float range, Entity exclude,
                            Vec2* outPos) const {
    const TeamList* t = nullptr;
    for (const auto& cand : teams_)
        if (cand.id == team) {
            t = &cand;
            break;
        }
    if (!t) return Entity::Null();
    if (t->list.size() < TeamList::Grid::kMinList) {
        // 线性快径：严格小于 = 等距保留池序靠前者（确定性；存量场景逐位不变）
        Entity best = Entity::Null();
        float bestD2 = range * range;
        for (const TargetEntry& te : t->list) {
            if (te.e == exclude) continue;
            float d2 = LengthSq(te.pos - from);
            if (d2 < bestD2) {
                bestD2 = d2;
                best = te.e;
                if (outPos) *outPos = te.pos;
            }
        }
        return best;
    }
    return t->grid.Nearest(t->list, from, range, exclude, outPos);
}

Entity TargetBoard::NearestAny(Vec2 from, float range, Entity exclude) const {
    Entity best = Entity::Null();
    float bestD2 = range * range;
    for (const TargetEntry& te : all_) {
        if (te.e == exclude) continue;
        float d2 = LengthSq(te.pos - from);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = te.e;
        }
    }
    return best;
}

// ---------------------------------------------------------------- #1 输入 --
void InputSnapshotSystem::Tick(World& world, Scene& scene, float dt) {
    // M2：快照已由 World::ApplyInput 注入（录制/回放共用通道）；本系统为
    // 主线程采样投递保留锚位（M4 窗口接驳）。无每帧工作。
    (void)world; (void)scene; (void)dt;
}

// --------------------------------------------------------------- #2 导演 --
// M5 批②（03 §8 修订形态）：波次表 = WaveDirector 组件（数据驱动，数组段序列化）；
// 导演 = 第二条刷怪通道——直接经 World::GetSpawnFn() 出生（Spawner 保留常驻环境
// 刷怪语义，互不派发）。决策 M5.md §11.2 D2–D6：
//   * time 吃缩放 dt（批① D5：timeScale=0 冻结波次、RNG 不消耗）；
//   * 波重叠 = 后波接管（同 tick 多波到期按表序全部生效，仅最后一波持运行时）；
//   * capAlive 与 SpawnSystem 同款 30 tick 普查 + 乐观自增（"约"语义压测红线）；
//   * RNG 子流 1（导演注册序；Spawn 的 2 不受扰）。多导演共享子流按池序消费
//     （确定性但任意）；prefab 失败（工厂返 Null）即废止该条目，不逐 tick 重试。
void DirectorSystem::Census(Scene& scene) {
    // per-team 存量普查（O(n)，30 tick 一次；死亡滞后半秒级——闸门语义"约"）
    teamCounts_.assign(64, 0);
    auto view = scene.View<Meta>();
    for (auto [ent, meta] : view.each()) ++teamCounts_[meta.team & 63];
}

void DirectorSystem::Tick(World& world, Scene& scene, float dt) {
    if (censusCountdown_ == 0) {
        Census(scene);
        censusCountdown_ = kCensusInterval;
    } else {
        --censusCountdown_;
    }

    const World::SpawnFn& spawn = world.GetSpawnFn();
    if (!spawn) {
        if (!warnedNoFactory_ && scene.Pool<WaveDirector>().size() > 0) {
            LEMON_WARN("WaveDirector present but no spawn factory registered");
            warnedNoFactory_ = true;
        }
        return;
    }

    Rng& rng = world.SystemRng(1); // 子流 id = 本系统注册序（Spawn 持 2）
    const float minInterval = dt > 0.0f ? dt : (1.0f / 60.0f); // 每条目每 tick 至多 1 生

    // spawn 请求表（循环外统一执行，2026-09-24 审查）：工厂会向 WaveDirector/
    // Transform2D 等组件池追加（嵌套导演/prefab 带 Transform），池扩容搬移使
    // wd/tf/wave 引用与 view 迭代器在迭代中悬空。RNG 在请求时消耗、事件按请求序
    // 推 = 与旧实现逐位同序列（金回放不受影响）
    struct DeferredSpawn {
        Entity director; // 失败时回写该导演的 waveSpawned（重取，池可能已搬移）
        uint8_t entry;   // 波内条目下标
        uint32_t prefabId;
        uint32_t team;
        Vec2 pos;
    };
    std::vector<DeferredSpawn> deferred;
    deferred.reserve(16);

    for (auto [ent, wd, tf] : scene.View<WaveDirector, Transform2D>().each()) {
        // 容量防御钳（Inspector/JSON 手改超容；读档侧 ReadArraySeg 另有一道）
        const uint8_t waveCount = wd.waveCount > 16 ? 16 : wd.waveCount;

        wd.time += dt;
        // timeScale=0 冻结（review 2026-10-02 #61）：波推进/到期冷却全部停摆——
        // "冻结波次、RNG 不消耗"（批① D5）自述不变量落地。常规条目重置后冷却恒
        // >0 本就不触发；此闸防 time 恰卡 startTime 的边沿启动与 capAlive 持币
        //（冷却 0）+ 普查陈旧的组合边角
        if (dt <= 0.0f) continue;
        // 波推进：表序=生效序；后波接管（前波未完成条目废止——顺序相位语义）
        while (wd.waveIndex < waveCount &&
               wd.time >= wd.waves[wd.waveIndex].startTime) {
            const WaveDef& w = wd.waves[wd.waveIndex];
            uint8_t ec = w.entryCount > 4 ? 4 : w.entryCount;
            float planned = 0.0f;
            for (uint8_t i = 0; i < ec; ++i) planned += (float)w.entries[i].count;

            EventPacket ev{};
            ev.type = GameEvent::WaveStart;
            ev.src = Scene::FromEntt(ent);
            ev.payload[0] = (float)wd.waveIndex; // 波序号（0 起）
            ev.payload[1] = planned;             // 本波计划总数 Σcount（0 = 纯宣告波）
            ev.payload[2] = w.startTime;
            world.Events().Push(ev);

            for (uint8_t i = 0; i < 4; ++i) { // 运行时随波重置
                wd.waveCooldown[i] = 0.0f;
                wd.waveSpawned[i] = 0;
            }
            ++wd.waveIndex;
        }
        if (wd.waveIndex == 0) continue; // 首波未到点
        const WaveDef& wave = wd.waves[wd.waveIndex - 1];
        const uint8_t entryCount = wave.entryCount > 4 ? 4 : wave.entryCount;

        // capAlive 闸门（0 = 不限；普查计数 + 下方乐观自增）
        uint32_t& alive = teamCounts_[wd.spawnTeam & 63];
        const bool capped = wd.capAlive > 0 && (int32_t)alive >= wd.capAlive;

        for (uint8_t i = 0; i < entryCount; ++i) {
            const WaveEntry& e = wave.entries[i];
            if (wd.waveSpawned[i] >= e.count) continue; // 本条目已交货
            wd.waveCooldown[i] -= dt;
            if (wd.waveCooldown[i] > 0.0f) continue;
            if (capped) {
                wd.waveCooldown[i] = 0.0f; // 持币待发：腾位后下一 tick 补生
                continue;
            }
            Vec2 offset = e.range > 0.0f ? rng.UnitVec2() * e.range * rng.Float01()
                                         : Vec2::Zero();
            const Vec2 pos = tf.pos + offset;
            deferred.push_back({Scene::FromEntt(ent), i, e.prefabId, wd.spawnTeam, pos});
            ++wd.waveSpawned[i]; // 交货计数随请求（失败在执行段废止 + 回退配额）
            ++alive;             // 乐观自增（普查刷新前的本 tick 内闸门）

            const float interval =
                wave.rampMult > 0.0f ? e.interval / wave.rampMult : e.interval;
            wd.waveCooldown[i] += interval > minInterval ? interval : minInterval;
        }
    }

    for (const DeferredSpawn& d : deferred) {
        Entity spawned = world.SpawnPrefab(d.prefabId, d.pos, d.team);
        if (spawned.IsNull()) { // 工厂不认此 prefab：废止条目（不逐 tick 重试）
            --teamCounts_[d.team & 63]; // 请求段乐观自增的回退
            if (WaveDirector* dw = scene.TryGet<WaveDirector>(d.director))
                if (dw->waveIndex > 0 && d.entry < 4)
                    dw->waveSpawned[d.entry] =
                        dw->waves[dw->waveIndex - 1].entries[d.entry].count;
            continue;
        }
        EventPacket p{};
        p.type = GameEvent::Spawn; // 与 SpawnSystem 同口径
        p.src = spawned;
        p.payload[0] = d.pos.x;
        p.payload[1] = d.pos.y;
        world.Events().Push(p);
    }
}

// --------------------------------------------------------------- #3 出生 --
void SpawnSystem::Census(Scene& scene) {
    // per-team 存量普查（O(n)，30 tick 一次；死亡滞后半秒级——配额语义"约"）
    teamCounts_.assign(64, 0);
    auto view = scene.View<Meta>();
    for (auto [ent, meta] : view.each()) ++teamCounts_[meta.team & 63];
}

void SpawnSystem::Tick(World& world, Scene& scene, float dt) {
    if (censusCountdown_ == 0) {
        Census(scene);
        censusCountdown_ = kCensusInterval;
    } else {
        --censusCountdown_;
    }

    const World::SpawnFn& spawn = world.GetSpawnFn();
    if (!spawn) {
        // [ISSUE-4] 告警仅当场景确有 Spawner（原无条件触发：无 Spawner 的 World
        // 首帧也刷屏）；标志为成员——多 World 实例各自告警一次
        if (!warnedNoFactory_ && scene.Pool<Spawner>().size() > 0) {
            LEMON_WARN("Spawner present but no spawn factory registered");
            warnedNoFactory_ = true;
        }
        return;
    }

    // spawn 请求表（循环外统一执行，2026-09-24 审查，与 DirectorSystem 同修）：
    // 工厂向组件池追加会使 view 迭代器与 sp/tf 引用迭代中悬空。RNG 在请求时消耗、
    // 事件按请求序推 = 与旧实现逐位同序列
    struct DeferredSpawn {
        uint32_t prefabId;
        uint32_t team;
        Vec2 pos;
        int group; // 所属 Spawner（组号：工厂不认 prefab 时跳过该组余量 = 旧 break）
    };
    std::vector<DeferredSpawn> deferred;
    deferred.reserve(16);
    if (dt <= 0.0f) return; // timeScale=0 冻结（review 2026-10-02 #61）：连发型
    //（interval≤dt）Spawner 出生后冷却钳 0 → 冻结期每 tick 到期，原实现照走出生
    // 分支 = burst 全额泄漏 + RNG 逐 tick 消耗，违背"冻结不消耗"自述
    int group = 0;

    for (auto [ent, sp, tf] : scene.View<Spawner, Transform2D>().each()) {
        ++group;
        sp.cooldown -= dt;
        if (sp.cooldown > 0.0f) continue;
        sp.cooldown += sp.interval;
        if (sp.cooldown < 0.0f) sp.cooldown = 0.0f; // 补偿溢出

        // maxAlive 配额（0 = 不限；压测红线语义——怪海有界）
        if (sp.maxAlive > 0 && teamCounts_[sp.spawnTeam & 63] >= sp.maxAlive)
            continue;

        Rng& rng = world.SystemRng(2); // 子流 id = 本系统注册序
        for (uint16_t b = 0; b < sp.burst; ++b) {
            if (sp.maxAlive > 0 && teamCounts_[sp.spawnTeam & 63] >= sp.maxAlive)
                break;
            Vec2 offset = sp.range > 0.0f ? rng.UnitVec2() * sp.range * rng.Float01()
                                          : Vec2::Zero();
            deferred.push_back({sp.prefabId, sp.spawnTeam, tf.pos + offset, group});
            ++teamCounts_[sp.spawnTeam & 63]; // 请求时占额（失败/跳过在执行段回退）
        }
    }

    int failedGroup = -1;
    for (const DeferredSpawn& d : deferred) {
        if (d.group == failedGroup) {
            --teamCounts_[d.team & 63];
            continue;
        }
        Entity e = world.SpawnPrefab(d.prefabId, d.pos, d.team);
        if (e.IsNull()) { // 工厂不认 prefab：本 Spawner 余量跳过（旧 break 语义）
            failedGroup = d.group;
            --teamCounts_[d.team & 63];
            continue;
        }
        EventPacket p{};
        p.type = GameEvent::Spawn;
        p.src = e;
        p.payload[0] = d.pos.x;
        p.payload[1] = d.pos.y;
        world.Events().Push(p);
    }
}

// ------------------------------------------------------------------ #4 AI --
void AISystem::Tick(World& world, Scene& scene, float dt) {
    // 目标板重建：Chase/Shooter 声明的目标队 + Flee 需要的全表（一遍 O(n)）
    wantedTeams_.clear();
    bool hasFlee = scene.Pool<Flee>().size() > 0;
    for (auto [ent, ch] : scene.View<Chase>().each())
        if (std::find(wantedTeams_.begin(), wantedTeams_.end(), ch.targetTeam) ==
            wantedTeams_.end())
            wantedTeams_.push_back(ch.targetTeam);
    for (auto [ent, sh] : scene.View<Shooter>().each())
        if (std::find(wantedTeams_.begin(), wantedTeams_.end(), sh.targetTeam) ==
            wantedTeams_.end())
            wantedTeams_.push_back(sh.targetTeam);
    board_.DeclareTeams(wantedTeams_);
    board_.Rebuild(scene, hasFlee, &world.Jobs()); // 大场并行收集/建桶（阈值内串行）

    // Chase：目标板最近邻 + 朝目标写 Velocity（并行池切分）
    {
        auto& pool = scene.Pool<Chase>();
        const uint32_t n = (uint32_t)pool.size();
        world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
            for (uint32_t i = b; i < e; ++i) {
                entt::entity ent = pool[i];
                // 池切分不含伴生组件约束（与 Separation 同守卫，防缺件实体空引用）
                if (!scene.Registry().all_of<Transform2D, Velocity>(ent)) continue;
                Chase& ch = pool.get(ent);
                Transform2D& tf = *scene.Registry().try_get<Transform2D>(ent);
                Velocity& vel = *scene.Registry().try_get<Velocity>(ent);

                // #64（review 2026-10-02）：并行段跨实体读走目标板帧内快照
                //（outPos），不回源 registry 直读（03 §4 条款 2；快照与同帧
                // registry 值逐位相同——Rebuild 后无人改 Transform，行为零变化）
                Vec2 targetPos{};
                ch.target = board_.Nearest(ch.targetTeam, tf.pos, ch.aggroRange,
                                           Scene::FromEntt(ent), &targetPos);
                if (ch.target.IsNull()) {
                    vel.v = Vec2::Zero();
                    continue;
                }
                Vec2 toT = targetPos - tf.pos;
                float d = Length(toT);
                if (d <= ch.keepRange) {
                    vel.v = Vec2::Zero();
                    continue;
                }
                vel.v = Normalize(toT) * ch.speed;
            }
        });
    }

    // Flee：威胁进入范围则反向（目标板全表最近）
    {
        auto view = scene.View<Flee, Transform2D, Velocity>();
        for (auto [ent, fl, tf, vel] : view.each()) {
            // [ISSUE-5] 排除自身：否则自己 d²=0 恒为"最近威胁" → away=零向量 →
            // 速度被主动清零（Flee 实体完全冻结，且覆盖 Chase 写入的速度）
            Entity threat = board_.NearestAny(tf.pos, fl.range, Scene::FromEntt(ent));
            if (threat.IsNull()) continue; // 不覆写（保留其他行为的 Velocity）
            Vec2 away = tf.pos - scene.Get<Transform2D>(threat).pos;
            vel.v = Normalize(away) * fl.speed;
        }
    }

    // Shooter：冷却到点朝目标发射投射物（生成走工厂）。
    // 2026-09-26 拆两段：冷却/索敌并行（Nearest 对目标板只读、逐实体写——
    // 五万行军场此段是 AI 首位热点）；开火生成保持主线程按原 view 序串行
    // （工厂变更池 + 事件入队序 = 回放确定）——净行为与原单线程逐位同构。
    {
        {
            auto& pool = scene.Pool<Shooter>();
            const uint32_t n = (uint32_t)pool.size();
            world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
                for (uint32_t i = b; i < e; ++i) {
                    entt::entity ent = pool[i];
                    if (!scene.Registry().all_of<Transform2D>(ent)) continue;
                    Shooter& sh = pool.get(ent);
                    Transform2D& tf = *scene.Registry().try_get<Transform2D>(ent);
                    sh.cooldown -= dt;
                    sh.target = board_.Nearest(sh.targetTeam, tf.pos, sh.range,
                                               Scene::FromEntt(ent));
                }
            });
        }
        auto view = scene.View<Shooter, Transform2D>();
        for (auto [ent, sh, tf] : view.each()) {
            if (sh.cooldown > 0.0f || sh.target.IsNull()) continue;
            sh.cooldown = sh.interval;

            const World::SpawnFn& spawn = world.GetSpawnFn();
            if (!spawn) continue;
            Vec2 dir = Normalize(scene.Get<Transform2D>(sh.target).pos - tf.pos);
            // 投射物出生在自身前方 12px（防自伤/防抖动）；
            // 弹体势力继承射手（玩家弹=玩家队、怪弹=怪队，Team 判定才成立）
            const uint32_t bulletTeam = scene.TryGet<Meta>(Scene::FromEntt(ent))
                                            ? scene.Get<Meta>(Scene::FromEntt(ent)).team
                                            : 3u;
            if (Entity proj = world.SpawnPrefab(sh.projectileId, tf.pos + dir * 12.0f,
                                                bulletTeam);
                !proj.IsNull()) {
                // 工厂生成后写入初速（朝向）；工厂只管实体组装，弹道语义在此处
                if (Velocity* v = scene.TryGet<Velocity>(proj)) {
                    if (const Projectile* p = scene.TryGet<Projectile>(proj))
                        v->v = dir * p->speed;
                }
                EventPacket ev{};
                ev.type = GameEvent::Spawn;
                ev.src = proj;
                ev.dst = Scene::FromEntt(ent);
                world.Events().Push(ev);
            }
        }
    }

    // Patrol：往返（暂停逻辑随 clip/玩法层完善，M5）。并行池切分（2026-09-26
    // 五万行军场：单线程全量写是 AI 段巨耗源）——逐实体纯写、零跨实体访问，
    // 守卫同 Chase 段；结果与迭代序确定性不变（ParallelFor 按池索引切分）。
    {
        auto& pool = scene.Pool<Patrol>();
        const uint32_t n = (uint32_t)pool.size();
        world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
            for (uint32_t i = b; i < e; ++i) {
                entt::entity ent = pool[i];
                if (!scene.Registry().all_of<Transform2D, Velocity>(ent)) continue;
                Patrol& pt = pool.get(ent);
                Transform2D& tf = *scene.Registry().try_get<Transform2D>(ent);
                Velocity& vel = *scene.Registry().try_get<Velocity>(ent);
                Vec2 dest = pt.headingToB ? pt.b : pt.a;
                Vec2 toD = dest - tf.pos;
                if (LengthSq(toD) < 4.0f) {
                    pt.headingToB = !pt.headingToB;
                    // [ISSUE-6] 折返同帧改向：重算 dest 再写速度（原速度滞后一帧，
                    // 端点过冲 ~1px 后才回头）
                    dest = pt.headingToB ? pt.b : pt.a;
                    toD = dest - tf.pos;
                }
                vel.v = Normalize(toD) * 60.0f;
            }
        });
    }
}

// -------------------------------------------------------------- #5 寻路 ----
void NavigationSystem::Tick(World& world, Scene& scene, float dt) {
    // M6 FlowField（03 §7）。占位。
    (void)world; (void)scene; (void)dt;
}

// ------------------------------------------------------------- #6 分离力 --
void SeparationSystem::Tick(World& world, Scene& scene, float dt) {
    const TeamTable& teams = world.Teams();
    // soft-collide 队掩码预计算（表静态，每 tick 一次 32×32 查表）：查询按掩码
    // 进 SpatialHash = cell 位图整格早退 + 候选级内联拒（SpatialHash.h L125）。
    // 掩码 = 0 的队（互穿队，如 Battle 场蓝军 team 3）整体跳过查询——此前全量
    // 扫邻居再逐候选 SoftCollide 拒掉，密场下纯耗（Battle 场实测 5000 蓝军
    // ~7ms）。语义不变：掩码命中的候选 = 原 SoftCollide 判定恰会接受的候选
    // （无 Meta 实体恒放行不入位图，回调内 om==nullptr 分支保留同旧序）。
    uint32_t softMasks[TeamTable::kMaxTeams];
    for (uint32_t t = 0; t < TeamTable::kMaxTeams; ++t) {
        uint32_t m = 0;
        for (uint32_t u = 0; u < TeamTable::kMaxTeams; ++u)
            if (teams.SoftCollide(t, u)) m |= 1u << u;
        softMasks[t] = m;
    }
    auto& pool = scene.Pool<Meta>();
    const uint32_t n = (uint32_t)pool.size();
    const float r = radius, r2 = r * r;

    world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
        for (uint32_t i = b; i < e; ++i) {
            entt::entity ent = pool[i];
            entt::registry& reg = scene.Registry();
            if (!reg.all_of<Transform2D, Velocity>(ent)) continue;
            Meta& meta = pool.get(ent);
            Transform2D& tf = *reg.try_get<Transform2D>(ent);
            Velocity& vel = *reg.try_get<Velocity>(ent);

            const uint32_t softMask =
                meta.team < TeamTable::kMaxTeams ? softMasks[meta.team] : 0;
            if (softMask == 0) continue; // 无任何可分离关系：跳过查询

            // 密度截断（03 §14"密度上限"）：每实体只处理前 maxNeighbors 个
            // 有效邻居。哈希回调序 = cell 序 → id 升序 → 截断确定（回放安全）。
            // 万怪堆叠时若不截断，cell 内遍历退化为 O(n²)（实测 6ms@1k）。
            Vec2 push{};
            uint32_t count = 0;
            physics2d::QueryFilter f;
            f.teamMask = softMask;
            f.exclude = Scene::FromEntt(ent);
            scene.Spatial().OverlapCircle(
                scene, tf.pos, r, f, 0.0f,
                [&](Entity other, const Transform2D& otf) -> bool {
                    if (other == Scene::FromEntt(ent)) return true;
                    const Meta* om = scene.TryGet<Meta>(other);
                    if (!om) return true;
                    // 只有 soft-collide 关系才分离（03 §9；掩码预滤同语义）
                    if (!teams.SoftCollide(meta.team, om->team)) return true;
                    Vec2 d = tf.pos - otf.pos;
                    float d2 = LengthSq(d);
                    if (d2 >= r2 || d2 < 1e-6f) return true;
                    float dist = std::sqrt(d2);
                    // 距离越近权重越大（1 - d/R），方向为分离向
                    push += (d / dist) * (1.0f - dist / r);
                    return ++count < maxNeighbors; // 达上限即终止扫描
                });

            if (count == 0) continue;
            float scale = strength * dt;
            if (count > densityCap) scale *= (float)densityCap / count; // 密度衰减
            vel.v += push * scale;
        }
    });
}

// --------------------------------------------------------------- #7 移动 --
void MovementSystem::Tick(World& world, Scene& scene, float dt) {
    const Rect bounds = world.Bounds();
    const bool clamp = world.HasBounds();

    auto& pool = scene.Pool<Velocity>();
    const uint32_t n = (uint32_t)pool.size();
    world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
        for (uint32_t i = b; i < e; ++i) {
            entt::entity ent = pool[i];
            entt::registry& reg = scene.Registry();
            Velocity& vel = pool.get(ent);
            Transform2D* tf = reg.try_get<Transform2D>(ent);
            if (!tf) continue;

            Vec2 v = vel.v;
            // 击退：独立衰减的速度脉冲叠加（割草手感）
            if (Knockback* kb = reg.try_get<Knockback>(ent)) {
                v += kb->impulse;
                kb->impulse = kb->impulse * std::exp(-kb->decay * dt);
                if (LengthSq(kb->impulse) < 1e-4f) kb->impulse = Vec2::Zero();
            }
            tf->pos += v * dt;

            if (clamp && !bounds.Contains(tf->pos)) {
                tf->pos = {lemon::math::Clamp(tf->pos.x, bounds.min.x, bounds.max.x),
                           lemon::math::Clamp(tf->pos.y, bounds.min.y, bounds.max.y)};
            }
        }
    });
}

// ----------------------------------------------------------- #8 哈希重建 --
void SpatialHashRebuildSystem::Tick(World& world, Scene& scene, float dt) {
    (void)world; (void)dt;
    scene.Spatial().Rebuild(scene); // 单线程（03 §14 预算内；profile 触发再并行化）
}

// --------------------------------------------------------------- #9 拾取 ----
void PickupSystem::Tick(World& world, Scene& scene, float dt) {
    // M5 批①：磁吸触程双侧取大 = max(宝石 magnetRadius, 收集者 Stats.pickupRadius)
    // —— 两源各一段查询：A 收集者广播（玩家磁力升级侧）、B 宝石自检（宝石自带
    // 吸程侧）。收集者约定 = 持 XpProgress 的实体（VS 心智：唯玩家拾取）。
    // 磁吸直写 pos（不经 Velocity——无 Movement/Separation 竞争，宝石免挂 Velocity）；
    // 不用 RNG（子流零扰动）；拾取销毁两阶段，与战斗销毁同走 #17 统一提交。

    // 段 A：收集者广播——pickupRadius 覆盖内的地面宝石即吸
    {
        auto view = scene.View<XpProgress, Transform2D>();
        for (auto [ent, xp, tf] : view.each()) {
            (void)xp;
            const Stats* st = scene.TryGet<Stats>(Scene::FromEntt(ent));
            const float r = st ? st->pickupRadius : 0.0f;
            if (r <= 0.0f) continue; // 无 Stats/未开磁力：只剩宝石自程侧（段 B）
            physics2d::QueryFilter f;
            f.exclude = Scene::FromEntt(ent);
            scene.Spatial().OverlapCircle(
                scene, tf.pos, r, f, 0.0f,
                [&](Entity other, const Transform2D&) {
                    Collectible* c = scene.TryGet<Collectible>(other);
                    if (!c || c->state != 0) return true;
                    c->state = 1;
                    c->target = Scene::FromEntt(ent);
                    return true; // 全量标记，不短路
                });
        }
    }

    // 段 B：宝石自检——自带吸程覆盖到收集者即吸（A 覆盖不到的"宝石吸程 > 玩家
    // 磁力"半边）；首个命中者胜（哈希 cell 序 = 确定性）
    {
        auto view = scene.View<Collectible, Transform2D>();
        for (auto [ent, c, tf] : view.each()) {
            if (c.state != 0 || c.magnetRadius <= 0.0f) continue;
            physics2d::QueryFilter f;
            f.exclude = Scene::FromEntt(ent);
            scene.Spatial().OverlapCircle(
                scene, tf.pos, c.magnetRadius, f, 0.0f,
                [&](Entity other, const Transform2D&) {
                    if (!scene.Has<XpProgress>(other)) return true;
                    c.state = 1;
                    c.target = other;
                    return false; // 首个即止
                });
        }
    }

    // 段 C：飞行 + 触距入账。同 tick A/B 双磁吸（两收集者竞争）= 池序后写胜出——
    // 确定性但任意；同屏多人拾取公平性归玩法层（分区/分宝石队）
    {
        auto view = scene.View<Collectible, Transform2D>();
        for (auto [ent, c, tf] : view.each()) {
            if (c.state != 1) continue;
            if (!scene.Alive(c.target) || !scene.Has<Transform2D>(c.target)) {
                c.state = 0; // 目标失活：回落地面（宝石不丢，可再吸）
                c.target = Entity::Null();
                continue;
            }
            const Vec2 toT = scene.Get<Transform2D>(c.target).pos - tf.pos;
            const float dist = Length(toT);
            if (dist <= kPickupTouch) {
                // 入账按 kind 分发（引擎只入账，表现归 C#/模板层事件消费）
                switch (c.kind) {
                case 0: // gem → XP（同 tick 由 #12 升级环消费 → LevelUp 事件）
                    if (XpProgress* xp = scene.TryGet<XpProgress>(c.target))
                        xp->xp += c.value;
                    break;
                case 1: // coin → gold
                    if (Inventory* inv = scene.TryGet<Inventory>(c.target))
                        inv->gold += (uint32_t)c.value;
                    break;
                case 2: // heart → 治疗（上限钳制）
                    if (Health* hp = scene.TryGet<Health>(c.target))
                        hp->cur = std::min(hp->max, hp->cur + c.value);
                    break;
                default: break; // 未知 kind：只发事件不入账（自定义拾取走事件层）
                }
                EventPacket ev{};
                ev.type = GameEvent::Pickup;
                ev.src = Scene::FromEntt(ent);
                ev.dst = c.target;
                ev.payload[0] = (float)c.kind;
                ev.payload[1] = c.value;
                ev.payload[2] = tf.pos.x;
                ev.payload[3] = tf.pos.y;
                world.Events().Push(ev);
                scene.Destroy(Scene::FromEntt(ent));
                continue;
            }
            // 飞行：min 钳制防单步过冲穿越目标
            const float step = std::min(c.magnetSpeed * dt, dist);
            tf.pos += (toT / dist) * step;
        }
    }
}

// -------------------------------------------------------------- #10 命中 --
void HitboxSystem::Tick(World& world, Scene& scene, float dt) {
    TeamTable& teams = world.Teams();
    auto& events = world.Events();

    // 投射物命中（读新哈希：Movement 后重建）
    auto projView = scene.View<Projectile, Transform2D>();
    for (auto [ent, pr, tf] : projView.each()) {
        const Meta* pm = scene.TryGet<Meta>(Scene::FromEntt(ent));
        uint32_t projTeam = pm ? pm->team : 0;

        bool consumed = false;
        physics2d::QueryFilter f;
        f.exclude = Scene::FromEntt(ent);
        // teamMask 预过滤（方案 A）：hostile 掩码入查询层，非敌对候选在
        // SpatialHash 内联位/整格早退即拒——密团场景免逐候选 Meta 取
        f.teamMask = teams.HostileMask(projTeam);
        // hitRadius = 有效判定半径全量（SpatialHash reach = radius + probe，probe=0）
        scene.Spatial().OverlapCircle(
            scene, tf.pos, pr.hitRadius, f, 0.0f,
            [&](Entity hit, const Transform2D& htf) {
                (void)htf;
                const Meta* hm = scene.TryGet<Meta>(hit);
                if (!hm || !teams.Hostile(projTeam, hm->team)) return true;
                Health* hp = scene.TryGet<Health>(hit);
                if (!hp) return true;
                if (hp->cur <= 0.0f) return true;   // 当帧已死（防多源重复 Death 事件）
                if (pr.HasHit((uint32_t)hit.id)) return true; // 命中记忆：一弹一目标一次
                if (hp->iFrames > 0.0f) return true; // 无敌帧免疫（跨弹 rate limit）
                hp->cur -= pr.damage;
                hp->iFrames = hp->iframeWindow; // 窗内免疫（含同帧多弹去重）；递减在 StatSystem
                pr.RememberHit((uint32_t)hit.id);
                ++pr.hits;

                EventPacket ev{};
                ev.type = GameEvent::Hit;
                ev.src = Scene::FromEntt(ent);
                ev.dst = hit;
                ev.payload[0] = pr.damage;
                ev.payload[1] = htf.pos.x;
                ev.payload[2] = htf.pos.y;
                events.Push(ev);

                // 击退（割草手感）：沿弹道方向脉冲，强度 = 弹体配置
                if (Knockback* kb = scene.TryGet<Knockback>(hit)) {
                    if (const Velocity* pv = scene.Registry().try_get<Velocity>(ent);
                        pv && LengthSq(pv->v) > 0.0f)
                        kb->impulse += Normalize(pv->v) * pr.knockback;
                }

                if (hp->cur <= 0.0f) {
                    hp->cur = 0.0f;
                    EventPacket death{};
                    death.type = GameEvent::Death;
                    death.src = hit;
                    events.Push(death);
                    // 脚本实体生死处置归脚本（死亡→对话框复活/重开走脚本逻辑；
                    // 批④后修④：此前无条件销毁，模板玩家死后 Revive 读已毁实体
                    // 连续报错被异常隔离禁用）。无脚本数据实体照旧两阶段清场
                    if (!scene.TryGet<scripting::ScriptBox>(hit))
                        scene.Destroy(hit); // 两阶段：当帧仍可访问
                }

                if (pr.pierce > 0) {
                    --pr.pierce; // 继续穿透；同目标重复伤害由命中记忆挡
                } else {
                    consumed = true;
                    return false; // 终止查询
                }
                return true;
            });
        if (consumed) {
            EventPacket die{};
            die.type = GameEvent::Death;
            die.src = Scene::FromEntt(ent);
            events.Push(die);
            scene.Destroy(Scene::FromEntt(ent));
        }
    }

    // Hazard 持续伤害区（tick 节拍）。语义决策（M5 批⓪）：独立 tickInterval 节拍，
    // 不与 iFrames 联动（区域伤害自成拍，不挤占受击无敌窗；需联动时加 Hazard 侧字段）
    auto hzView = scene.View<Hazard, Transform2D>();
    for (auto [ent, hz, tf] : hzView.each()) {
        hz.tickPhase -= dt;
        if (hz.tickPhase > 0.0f) continue;
        hz.tickPhase += hz.tickInterval;

        const Meta* hm = scene.TryGet<Meta>(Scene::FromEntt(ent));
        uint32_t zoneTeam = hm ? hm->team : 0;
        physics2d::QueryFilter f;
        f.exclude = Scene::FromEntt(ent);
        f.teamMask = teams.HostileMask(zoneTeam); // 同弹幕侧：密团同队候选整格早退
        scene.Spatial().OverlapCircle(
            scene, tf.pos, hz.radius, f, 8.0f,
            [&](Entity hit, const Transform2D& htf) {
                (void)htf;
                const Meta* tm = scene.TryGet<Meta>(hit);
                if (!tm || !teams.Hostile(zoneTeam, tm->team)) return true;
                if (Health* hp = scene.TryGet<Health>(hit)) {
                    if (hp->cur <= 0.0f) return true; // 当帧已死（防重复 Death）
                    hp->cur -= hz.dps * hz.tickInterval;
                    EventPacket ev{};
                    ev.type = GameEvent::Hit;
                    ev.src = Scene::FromEntt(ent);
                    ev.dst = hit;
                    ev.payload[0] = hz.dps * hz.tickInterval;
                    events.Push(ev);
                    if (hp->cur <= 0.0f) {
                        hp->cur = 0.0f;
                        EventPacket death{};
                        death.type = GameEvent::Death;
                        death.src = hit;
                        events.Push(death);
                        // 同上（弹道侧）：脚本实体不自动销毁
                        if (!scene.TryGet<scripting::ScriptBox>(hit))
                            scene.Destroy(hit);
                    }
                }
                return true;
            });
    }
}

// ------------------------------------------------------------- #11 触发器 --
void TriggerSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    auto view = scene.View<Trigger2D, Transform2D>();
    for (auto [ent, tg, tf] : view.each()) {
        const uint32_t selfTeam = scene.TryGet<Meta>(Scene::FromEntt(ent)) ? scene.Get<Meta>(Scene::FromEntt(ent)).team : 0;
        bool anyInside = false;
        physics2d::QueryFilter f;
        f.exclude = Scene::FromEntt(ent);
        // 触发目标：非 ghost 关系的实体（拾取/传送语义由资产侧配 triggerId 过滤）
        scene.Spatial().OverlapCircle(
            scene, tf.pos, tg.radius, f, 4.0f,
            [&](Entity other, const Transform2D&) {
                // [ISSUE-7] 触发器互不触发：否则共置触发器开局互发假 Enter、
                // anyInside 恒真（M5 资产侧 triggerId 过滤落地前的短期守卫）
                if (scene.Has<Trigger2D>(other)) return true;
                const Meta* om = scene.TryGet<Meta>(other);
                if (!om) return true;
                if (world.Teams().Relation(selfTeam, om->team) == TeamRelation::Ghost)
                    return true;
                anyInside = true;
                return false; // 只需存在性
            });

        // once=1 时首触发后置 fired，不再重触发（字段登记为 runtime 不入档）
        if (anyInside && !tg.inside) {
            tg.inside = 1;
            if (!(tg.once && tg.fired)) {
                tg.fired = 1;
                EventPacket ev{};
                ev.type = GameEvent::TriggerEnter;
                ev.src = Scene::FromEntt(ent);
                ev.userArg = tg.triggerId;
                world.Events().Push(ev);
            }
        } else if (!anyInside && tg.inside) {
            tg.inside = 0;
            if (!(tg.once && tg.fired)) { // once 触发器不报 Exit
                EventPacket ev{};
                ev.type = GameEvent::TriggerExit;
                ev.src = Scene::FromEntt(ent);
                ev.userArg = tg.triggerId;
                world.Events().Push(ev);
            }
        }
    }
}

// ------------------------------------------------------------- #12 数值 ----
void StatSystem::Tick(World& world, Scene& scene, float dt) {
    // 状态效果：倒计时，到期压缩保序移除（保序 = 确定性哈希稳定）
    {
        auto view = scene.View<StatusEffects>();
        for (auto [ent, st] : view.each()) {
            uint8_t kept = 0;
            for (uint8_t i = 0; i < st.count; ++i) {
                st.active[i].remain -= dt;
                if (st.active[i].remain > 0.0f) st.active[kept++] = st.active[i];
            }
            st.count = kept;
        }
    }
    // iFrames 倒计时（M5 批⓪；此前只置不减 → 受击一次永久无敌，DevLog 2026-09-22 P0）。
    // Hitbox(#10) 同 tick 置窗在先、此处(#12) 递减在后 → 复拍间隔恰 ceil(窗/dt) tick
    // （60Hz、0.1s 窗 = 6 tick）；逐实体独立更新 = 确定性。
    {
        auto view = scene.View<Health>();
        for (auto [ent, hp] : view.each())
            if (hp.iFrames > 0.0f) hp.iFrames = std::max(0.0f, hp.iFrames - dt);
    }
    // 经验/升级（幂曲线；VS 曲线资产化 M5）。入账源 = #9 PickupSystem（gem 拾取）
    {
        auto view = scene.View<XpProgress>();
        for (auto [ent, xp] : view.each()) {
            while (xp.xp >= xp.xpToNext) {
                xp.xp -= xp.xpToNext;
                ++xp.level;
                // 下限 1：xpToNext 若被资产配成 0/极小，ceil 收敛会卡死升级环
                // （系数 = World::XpCurveK，M6a 批② T4 提参；默认 1.25 = 原硬编码）
                xp.xpToNext = std::max(1.0f, std::ceil(xp.xpToNext * world.XpCurveK()));
                EventPacket ev{};
                ev.type = GameEvent::LevelUp;
                ev.src = Scene::FromEntt(ent);
                ev.payload[0] = (float)xp.level;
                world.Events().Push(ev);
            }
        }
    }
}

// ------------------------------------------------------------- #13 动画 ----
void AnimatorSystem::Tick(World& world, Scene& scene, float dt) {
    // M5 批③：clip 表帧映射（03 §5）。有 clip = 纯函数帧号 time*fps 截断 + 写
    // curFrame/sr.spriteId（逐帧重写幂等，无逐帧累加状态机 → 回放确定）；
    // 无 clip（clipId=0/表未命中/未登记）= M2 旧路径逐位保留——既有场景零漂移
    // （金回放零重录的机制保证，M5.md §18）。
    // M6a 批①：换段队列（nextClipId/fadeRemain/nextLoop，FIELD_RT）——先推进后
    // 判定：Queue（fadeRemain<0）当前段收尾/回绕点切、CrossFade（>0）倒计时到零
    // 切（非 loop 段提前收尾即切）；切换 = 新段首帧当帧生效；暂停冻结整个队列。
    // 队列目标是显式指令，未命中 clip 表 = warn-once 丢队列（区别于 clipId 未命中
    // 走 M2 的宽容——那是档面数据，这是作者代码错误）。
    const ClipTable& clips = world.Clips();
    bool anyClip = clips.Count() > 0; // 空表 = 全体走 M2（省每实体 Find）
    auto view = scene.View<Animator2D>();
    for (auto [ent, an] : view.each()) {
        const ClipDef* clip = anyClip ? clips.Find(an.clipId) : nullptr;
        if (!clip) {
            // M2：时间推进（含 loop 回绕）；占位周期 1.0 保证 time 有界。
            // loop=2（PingPong）truthy 同 1——无 clip 无帧表，M2 语义不变（金档锚）
            an.time += an.speed * dt;
            if (an.loop) {
                float period = 1.0f; // 占位周期；无 clip 路径恒此值（勿动——金档锚）
                // fmod 一次到位 + 有限性护栏（#65）：档面/脚本可写 speed，JSON 大数
                // 经 float 即 inf——原 while 逐帧减对 inf 整帧死循环；正常值下 fmod
                // 与逐减位级同值（回放零漂移）
                an.time = std::isfinite(an.time) && period > 0.0f
                              ? std::fmod(an.time, period)
                              : 0.0f;
            }
            continue;
        }
        // playOnStart=0 = 暂停开关（time/curFrame/spriteId/换段队列全冻结）
        if (!an.playOnStart) continue;
        an.time += an.speed * dt; // 缩放 dt：timeScale=0 冻结动画（批① D5 同语义）
        const uint32_t n = (uint32_t)clip->frames.size();
        const float total = (float)n / clip->fps;
        // T3b-2：PingPong 周期 = 2(n-1)/fps（0→n-1→0 往返，回绕点 = 回到帧 0）；
        // n==1 防御钳 0。loop 旧值 0/1 路径逐位不变（金回放零重录）
        const float totalPP = n > 1 ? (2.0f * (float)(n - 1)) / clip->fps : 0.0f;
        bool wrapped = false; // 本 tick 发生回绕减法（Queue 的 loop 段切点）
        if (an.loop == 2) {
            if (totalPP > 0.0f) {
                // fmod 一次到位 + 有限性护栏（#65，与 M2 分支同款）：inf time 原逐减
                // while 整帧挂起；正常值一次回绕 Sterbenz 精确 = 与逐减位级同值
                if (an.time >= totalPP) {
                    wrapped = true;
                    an.time = std::isfinite(an.time) ? std::fmod(an.time, totalPP) : 0.0f;
                }
            } else if (an.time > 0.0f)
                an.time = 0.0f;
            if (an.time < 0.0f) an.time = 0.0f; // 负 speed 防御
        } else if (an.loop) {
            if (total > 0.0f && an.time >= total) { // total==0（空帧表）同护栏，防恒真循环
                wrapped = true;
                an.time = std::isfinite(an.time) ? std::fmod(an.time, total) : 0.0f;
            }
            if (an.time < 0.0f) an.time = 0.0f; // 负 speed 防御（回绕后仍负）
        } else {
            if (an.time < 0.0f) an.time = 0.0f;
            if (an.time > total) {
                an.time = total; // 钳末帧：M2"无界增长"随 clip 收口
                // T3d 批③：段末边沿（0→1 一次）→ AnimFinished 事件（帧末批量派发，
                // C# Events.Subscribe(GameEvent.AnimFinished) 消费；补 IsPlaying 判
                // 不了 Once 播完的缺口）。切段处（本系统队列/AnimGraph/SDK Play）归 0。
                if (!an.ended) {
                    an.ended = 1;
                    EventPacket fin{};
                    fin.type = GameEvent::AnimFinished;
                    fin.src = Scene::FromEntt(ent);
                    fin.userArg = an.clipId;
                    world.Events().Push(fin);
                }
            }
        }
        if (an.nextClipId != 0) { // 换段队列判定（无队列 = 零副作用，既有路径逐位不变）
            const bool atEnd = !an.loop && an.time >= total; // 非 loop 段收尾
            bool queueNow = false;
            if (an.fadeRemain > 0.0f) { // CrossFade：倒计时（过期判定在减之前取模——
                an.fadeRemain -= dt;    // 减到负值不是 Queue 语义，勿按符号分流）
                queueNow = an.fadeRemain <= 0.0f || atEnd;
            } else if (an.fadeRemain == 0.0f) {
                queueNow = true; // 零时长淡入 = 立即切（SDK CrossFade(0) 已归 Play，防御）
            } else {
                queueNow = atEnd || wrapped; // Queue：收尾/回绕点
            }
            if (queueNow) {
                if (const ClipDef* next = clips.Find(an.nextClipId)) {
                    an.clipId = an.nextClipId;
                    an.loop = an.nextLoop ? 1 : 0;
                    an.time = 0.0f;
                    an.ended = 0; // T3d 批③：新段从非收尾态起（段末边沿重置）
                    clip = next; // 本 tick 即按新段帧映射（首帧当帧生效）
                } else if (!warnedQueueMiss_) {
                    warnedQueueMiss_ = true;
                    LEMON_WARN("Animator 换段目标 clipId=%#x 未登记，丢弃队列",
                               an.nextClipId);
                }
                an.nextClipId = 0;
                an.fadeRemain = 0.0f;
            }
        }
        uint32_t f;
        if (an.loop == 2 && n > 1) {
            // T3b-2 PingPong 帧映射（纯函数）：pos ∈ [0,2(n-1))，前半正放
            // 后半反放——0..n-1..1..0，无逐帧累加状态（回放确定）
            const uint32_t period = 2 * (n - 1);
            const uint32_t pos = (uint32_t)(an.time * clip->fps) % period;
            f = pos < n ? pos : period - pos;
        } else {
            f = (uint32_t)(an.time * clip->fps);
            if (f >= n) f = n - 1; // total 边界
        }
        // T3d 批③：帧事件——进入新帧（f != 上 tick 写入的 curFrame）且该帧有打点
        // → AnimFrame 事件入队（帧末批量派发；user = 事件 id，userArg = clipId，
        // payload[0] = 帧号）。帧号纯函数 ⇒ 事件序确定；loop 回绕/pingpong 反放
        // 重进该帧会重发（Unity 循环重发同语义）；暂停（playOnStart=0）不推进不发。
        if (f != an.curFrame && !clip->events.empty()) {
            for (const ClipEventDef& ev : clip->events) {
                if (ev.frame != f) continue;
                EventPacket fev{};
                fev.type = GameEvent::AnimFrame;
                fev.src = Scene::FromEntt(ent);
                fev.user = ev.id;
                fev.userArg = an.clipId;
                fev.payload[0] = (float)f;
                world.Events().Push(fev);
            }
        }
        an.curFrame = (uint16_t)f;
        if (SpriteRenderer* sr = scene.TryGet<SpriteRenderer>(Scene::FromEntt(ent)))
            sr->spriteId = clip->frames[f]; // SpriteRenderer 可缺 = 纯计时推进
    }
}

// ---------------------------------------------------- #14 投射物回收 ----
void ProjectileLifetimeSystem::Tick(World& world, Scene& scene, float dt) {
    auto& pool = scene.Pool<Projectile>();
    const uint32_t n = (uint32_t)pool.size();
    // 并行销毁的稳定归并（03 §4 契约第 3 条；review 2026-10-02 #2）：worker 内
    // 直接 Destroy 的入队序 = 互斥锁获取序（随线程交错漂移）→ CommitDestroys
    // 提交序漂移 → 池 swap_and_pop 终态与实体槽回收序不定。改为 chunk 分桶收集
    // 意图、ParallelFor 返回后主线程按 chunk 序提交（块界 grain 对齐，JobSystem.cpp
    // 保证；桶内池索引升序）——归并序 = 串行迭代序，与单线程档逐位同构
    constexpr uint32_t kGrain = 256;
    chunkIntents_.resize((n + kGrain - 1) / kGrain);
    for (auto& c : chunkIntents_) c.clear();
    world.Jobs().ParallelFor(n, kGrain, [&](uint32_t b, uint32_t e) {
        std::vector<Entity>& out = chunkIntents_[b / kGrain];
        for (uint32_t i = b; i < e; ++i) {
            entt::entity ent = pool[i];
            Projectile& pr = pool.get(ent);
            pr.age += dt;
            if (pr.age < pr.lifetime) continue;
            out.push_back(Scene::FromEntt(ent));
        }
    });
    for (const auto& bucket : chunkIntents_)
        for (Entity ent : bucket) scene.Destroy(ent); // 两阶段：回收仍走统一提交
    // 越界回收（有界世界时）：单遍兜底（命中销毁同帧由 Commit 统一）
    if (world.HasBounds()) {
        Rect bounds = world.Bounds().Expanded(64.0f);
        auto view = scene.View<Projectile, Transform2D>();
        for (auto [ent, pr, tf] : view.each()) {
            (void)pr;
            if (!bounds.Contains(tf.pos)) scene.Destroy(Scene::FromEntt(ent));
        }
    }
}

// ---------------------------------------------------- #15 C# 批量（M3）----
void CSharpBatchSystem::Tick(World& world, Scene& scene, float dt) {
    // 桥后端（ScriptHost）构造块描述符（本线程）→ 域线程执行（ADR-010 D1）；未注入则空跑
    if (auto* backend = world.ScriptBackend()) backend->TickBatch(world, scene, dt);
}

// ------------------------------------------- #16 动画状态机评估（T3d）----
void AnimGraphSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    // 分层（ADR-013 D1）：本系统只做决策——评估出边、写切段（= SDK Play 的直写
    // 语义：clipId/time/loop/ended 置位 + 清在途队列）；帧映射与队列消费仍在 #13。
    // 空 controller 表 = 全体旁路（基准场零实例零成本，金回放零重录）。
    const ControllerTable& controllers = world.Controllers();
    if (controllers.Count() == 0) return;
    const ClipTable& clips = world.Clips();
    float zeroParams[8] = {};
    auto view = scene.View<AnimGraph, Animator2D>();
    for (auto [ent, graph, an] : view.each()) {
        const Entity e = Scene::FromEntt(ent);
        if (graph.setGuid == 0) continue; // 未绑集：图无从解析状态（批①纯绑定语义）
        const uint32_t setId = (uint32_t)graph.setGuid;
        const ControllerDef* ctrl = controllers.Find((uint32_t)graph.controllerGuid);
        AnimParams* params = scene.TryGet<AnimParams>(e);
        // 初始化边沿：参数槽从 controller 默认值播种 + 当前段不在集内（含 0）→
        // Play(entry)。缺绑 entry = warn-once 保持现状（ADR-013 D4 空绑哲学）。
        if (!graph.inited) {
            graph.inited = 1;
            if (ctrl && params)
                for (size_t i = 0; i < ctrl->params.size() && i < 8; ++i)
                    params->v[i] = ctrl->params[i].def;
            if (ctrl && !clips.NameOfClip(setId, an.clipId)) {
                const std::string& entry =
                    ctrl->states[(size_t)ctrl->entry < ctrl->states.size()
                                     ? (size_t)ctrl->entry : 0];
                const uint32_t cid = clips.FindByName(setId, entry.c_str());
                if (cid != 0) {
                    an.clipId = cid;
                    an.time = 0.0f;
                    an.ended = 0;
                } else if (!warnedBindingMiss_) {
                    warnedBindingMiss_ = true;
                    LEMON_WARN("AnimGraph 入口状态「%s」缺绑（集 %08x），保持当前段",
                               entry.c_str(), setId);
                }
            }
            continue; // 播种帧不评估出边（参数先就位，下一 tick 起评估）
        }
        if (!ctrl) continue; // 只绑集不绑图（批①纯绑定）= 不评估
        // 当前状态反推：clipId → 段名 → 词表下标。不在集/不在词表 = 脚本直写了
        // 图外段 → 图让位（脚本优先于图，ADR-013 D1）。
        const std::string* curName = clips.NameOfClip(setId, an.clipId);
        const int32_t cur = curName ? ctrl->StateIndex(*curName) : -1;
        if (cur < 0) {
            if (!warnedStateMiss_) {
                warnedStateMiss_ = true;
                LEMON_WARN("AnimGraph 当前段「%s」不在词表/集内，图让位（脚本直写优先）",
                           curName ? curName->c_str() : "(不在集)");
            }
            continue;
        }
        const float* pv = params ? params->v : zeroParams;
        // 出边评估：同 from 按文件序，首条命中即切（每实体每 tick 至多一条，无级联
        // ——确定性）。exitTime = 非 loop 段收尾边沿（an.ended，#13 钳 total 时置位；
        // loop 段 exitTime 不触发，挂起项）。缺绑目标 = warn-once 保持当前状态。
        for (const AnimTransitionDef& t : ctrl->transitions) {
            if (t.from != (uint16_t)cur) continue;
            if (!AnimCondsHold(*ctrl, t, pv)) continue;
            if (t.exitTime && !an.ended) continue;
            const uint32_t cid = t.to < ctrl->states.size()
                                     ? clips.FindByName(setId, ctrl->states[t.to].c_str())
                                     : 0;
            if (cid == 0) {
                if (!warnedBindingMiss_) {
                    warnedBindingMiss_ = true;
                    LEMON_WARN("AnimGraph 目标状态「%s」缺绑（集 %08x），保持当前状态",
                               t.to < ctrl->states.size() ? ctrl->states[t.to].c_str() : "?",
                               setId);
                }
            } else if (cid != an.clipId) {
                an.clipId = cid;
                an.time = 0.0f;
                an.ended = 0;
                an.nextClipId = 0; // 图切换 = Play 语义直写（清在途队列）
                an.fadeRemain = 0.0f;
                if (const ClipDef* c = clips.Find(cid)) an.loop = c->loop ? 1 : 0;
            }
            // trigger 消费即清（参与命中出边的槽；未命中出边不动——保守不误清）
            if (params)
                for (const AnimCondDef& c : t.conds)
                    if (c.op == AnimCondOp::Trigger && c.param < ctrl->params.size())
                        params->v[c.param] = 0.0f;
            break;
        }
    }
}

// ------------------------------------------- #16.5 属性补间推进（A 档 tween）----
void TweenSystem::Tick(World& world, Scene& scene, float dt) {
    world.Tweens().Advance(world, scene, dt); // 空表内部早退（基准场零成本零漂移）
}

// ------------------------------------- #16.7 音频通道提交 + 2D 声源空间化 --------
namespace {
// 源世界位（父链异常兜底本地位——相机跟随同款口径）
Vec2 AudioSourceWorldPos(Scene& scene, Entity e) {
    if (!scene.Has<Transform2D>(e)) return Vec2{0, 0};
    WorldTransform2D wt{};
    if (ComputeWorldTransform(scene, e, wt)) return wt.pos;
    return scene.Get<Transform2D>(e).pos;
}

// 起一个组件声源声部（起播/换片共用）：当前监听器快照定初始空间参数
uint32_t StartSourceVoice(World& world, Scene& scene, audio::AudioEngine& engine,
                          const audio::AudioListener& listener, Entity e,
                          const AudioSource& src) {
    const uint32_t clipId = world.ResolveAudioClip(src.clipGuid);
    if (clipId == 0) return 0;
    audio::PlayParams p;
    p.group = src.group < audio::kGroupCount ? static_cast<audio::Group>(src.group)
                                             : audio::Group::Sfx;
    p.loop = (src.flags & kAudioLoop) != 0;
    p.volume = src.volume;
    float gain = 1.0f, pan = 0.0f;
    audio::ComputeSpatial(AudioSourceWorldPos(scene, e), listener, src.refDist,
                          src.maxDist, gain, pan);
    p.volume = src.volume * gain;
    p.pan = pan;
    return engine.Play(clipId, p);
}
} // namespace

void AudioSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    // ① 命令提交：C# 当 tick staging 的播放/控制/BGM 槽/暂停统一落地
    //（engine null = 纯记账，无声宿主全降级——AudioChannel 头说明）
    world.Audio().Submit(world.AudioSink(), world.AudioListener());

    audio::AudioEngine* engine = world.AudioSink();
    const audio::AudioListener& listener = world.AudioListener();

    // ② AudioSource 扫描：起播（playOnStart 一次）/换片重绑/空间参数热更（只读）
    for (auto [ent, src] : scene.View<AudioSource>().each()) {
        const Entity e = Scene::FromEntt(ent);
        AudioSystem::SourceBinding* b = nullptr;
        for (auto& x : bindings_)
            if (x.e == e) {
                b = &x;
                break;
            }
        if (!b) {
            if (src.clipGuid == 0 || !(src.flags & kAudioPlayOnStart)) continue;
            const uint32_t clipId = world.ResolveAudioClip(src.clipGuid);
            if (clipId == 0) { // 坏 guid = 作者错误（音频装载先于首 tick）；不绑 = 修正后下一 tick 起播
                if (!warnedClipMiss_) {
                    warnedClipMiss_ = true;
                    LEMON_WARN("AudioSource clipGuid %016llx 未注册（资产未装载/GUID 手误），该声源静默",
                               (unsigned long long)src.clipGuid);
                }
                continue;
            }
            if (!engine) continue; // 无声宿主：不建绑定（引擎后接 = 下一 tick 起播）
            const uint32_t eid = StartSourceVoice(world, scene, *engine, listener, e, src);
            if (eid == 0) continue; // 池满等：下 tick 再试
            bindings_.push_back({e, eid, src.clipGuid});
            b = &bindings_.back();
        } else if (b->clipGuid != src.clipGuid) {
            // 换片：停旧起新（运行时改 guid = 明确意图，与 playOnStart 无关）
            if (engine && b->engineVoice) engine->Stop(b->engineVoice);
            b->engineVoice = 0;
            const uint64_t prevGuid = b->clipGuid;
            b->clipGuid = src.clipGuid;
            if (src.clipGuid != 0 && engine) {
                b->engineVoice = StartSourceVoice(world, scene, *engine, listener, e, src);
                if (b->engineVoice == 0) {
                    // review 2026-10-02 #5：换片起播失败不再永久静默——坏 guid 一次
                    // 性告警（与起播路径共用 warnedClipMiss_ 旗）；池满/节流等可恢复
                    // 拒绝则回滚 guid 记账制造失配，下 tick 重试（起播路径同语义）
                    if (world.ResolveAudioClip(src.clipGuid) == 0) {
                        if (!warnedClipMiss_) {
                            warnedClipMiss_ = true;
                            LEMON_WARN("AudioSource 换片 clipGuid %016llx 未注册（资产未装载/GUID 手误），该声源静默",
                                       (unsigned long long)src.clipGuid);
                        }
                    } else {
                        b->clipGuid = prevGuid;
                    }
                }
            }
        }
        // 空间参数热更：监听器移动/组件调参逐 tick 生效（D7：组件声源跟随实体）
        if (engine && b && b->engineVoice) {
            float gain = 1.0f, pan = 0.0f;
            audio::ComputeSpatial(AudioSourceWorldPos(scene, e), listener, src.refDist,
                                  src.maxDist, gain, pan);
            engine->SetVoiceParams(b->engineVoice, src.volume * gain, pan);
        }
    }

    // ③ 绑定回收：实体亡/组件摘 → 停声部解绑（绑定 = "已起播"记账，playOnStart
    // 不复活；一次性放完的声部留记账至实体消亡——重触发语义见类注）
    // review 2026-10-02 #20 交底：Entity 句柄无场景位（高 32 位保留，Entity.h），
    // 判定只对当前活动场景 registry——World 中途 SetActiveScene 切场景时，旧场景
    // 残留绑定理论上可被新场景同 (index,version) 实体撞号收养。当前宿主纪律 =
    // 每 Play 新建单场景 World + StopPlay 先 StopAll 再退 World（EditorContext/
    // EditorAppScripts），voice 清场由宿主保证；多场景 World 需先落句柄场景位。
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [&](SourceBinding& x) {
                                       if (scene.Alive(x.e) && scene.Has<AudioSource>(x.e))
                                           return false;
                                       if (engine && x.engineVoice)
                                           engine->Stop(x.engineVoice);
                                       return true;
                                   }),
                    bindings_.end());
}

// ---------------------------------------------------- #16 事件派发 --------
void ScriptEventDispatchSystem::Tick(World& world, Scene& scene, float dt) {
    (void)scene; (void)dt;
    auto& events = world.Events();
    // C# 桥（M3-4）：头部拉脚本 pending 入队（并入当帧批次）
    if (auto* backend = world.ScriptBackend()) backend->PullPendingEvents(world);
    const World::EventSink& sink = world.GetEventSink();
    // 派发前整体取走（2026-09-24 审查 P5）：旧实现 At(i) 引用 + 末尾 Clear()——
    // 回调（sink/C# 订阅者）内再 Push 的事件被整体清掉（静默丢失），Push 触发
    // Grow 扩容时 At 引用/桥侧段指针悬空（UAF）。取走后本帧从稳定快照派发，
    // 窗口内新入队事件留在队列、下帧派发（03 §11"帧末批量消费"语义不变）
    events.TakeAll(dispatchBuf_);
    if (auto* backend = world.ScriptBackend())
        backend->DispatchEvents(world, scene, dispatchBuf_.data(),
                                (uint32_t)dispatchBuf_.size());
    if (sink)
        for (const EventPacket& p : dispatchBuf_) sink(world, p);
}

// ---------------------------------------------------- #17 销毁提交 --------
void DestroyCommitSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    // M3-6：脚本结构命令帧首应用（建/删实体、增删组件、挂脚本；先于销毁提交——
    // Destroy 命令本批内随后的 CommitDestroys 直接生效）
    if (auto* backend = world.ScriptBackend()) {
        backend->ApplyStructural(world, scene);
        // F-08.2（2026-09-24）：C++ 系统路径入队的销毁在此补 OnDestroy 通知——
        // 与脚本命令路径在 NotifyPendingDestroys 内汇合（flag 去重，恰好一次）
        backend->NotifyPendingDestroys(world, scene);
    }
    scene.CommitDestroys(); // 两阶段销毁 + EnTT 实体回收（池语义）
}

// ---------------------------------------------------- 默认管线安装 --------
void World::InstallDefaultSystems() {
    auto& p = Pipeline();
    // 注册序 = 03 §4 表序 = 系统 RNG 子流 id（改动序号 = 破坏回放兼容，禁）。
    // PickupSystem（#9，M5 批①）不用 RNG——不占子流，中插不移位既有 id。
    // 子流占用：Director=1（M5 批②起）、Spawn=2；其余系统不消费
    p.AddSystem(std::make_unique<InputSnapshotSystem>());
    p.AddSystem(std::make_unique<DirectorSystem>());
    p.AddSystem(std::make_unique<SpawnSystem>());
    p.AddSystem(std::make_unique<AISystem>());
    p.AddSystem(std::make_unique<NavigationSystem>());
    {
        auto sep = std::make_unique<SeparationSystem>();
        separation_ = sep.get(); // World::Separation() 调参通道（重装刷新）
        p.AddSystem(std::move(sep));
    }
    p.AddSystem(std::make_unique<MovementSystem>());
    p.AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    p.AddSystem(std::make_unique<PickupSystem>());
    p.AddSystem(std::make_unique<HitboxSystem>());
    p.AddSystem(std::make_unique<TriggerSystem>());
    p.AddSystem(std::make_unique<StatSystem>());
    p.AddSystem(std::make_unique<AnimatorSystem>());
    p.AddSystem(std::make_unique<ProjectileLifetimeSystem>());
    p.AddSystem(std::make_unique<CSharpBatchSystem>());
    // T3d 批②：图评估尾插在 C# 批量之后（读当 tick 脚本参数）、事件派发之前。
    // 不消费 RNG——不占子流；其后系统（事件派发/销毁提交）本就不消费，id 语义零影响。
    p.AddSystem(std::make_unique<AnimGraphSystem>());
    // A 档补间（2026-09-28）：同位置尾插——脚本当 tick 发起的补间本 tick 即首写、
    // 完成事件当帧派发；不消费 RNG，其后系统（事件派发/销毁提交）不消费，零影响。
    p.AddSystem(std::make_unique<TweenSystem>());
    // M6c 批②（ADR-015 M3）：音频通道提交 + 2D 声源空间化。同位置尾插——C# 当
    // tick staging 的音频命令本 tick 落地；不消费 RNG、零 ECS 写（绑定表 = 系统
    // 局部）→ 自身零哈希漂移；基准场零 AudioSource/零音频调用 = 空转零成本。
    p.AddSystem(std::make_unique<AudioSystem>());
    p.AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    p.AddSystem(std::make_unique<DestroyCommitSystem>()); // Essential 阶段
    // M7c 批⑥b：换场编排执行壳（Essential，After DestroyCommitSystem）——单帧
    // 先清后装（SceneSwitcher.h 头注）；不消费 RNG 不占子流，尾插零重排 =
    // 既有系统 id 不动（金回放零重录）。无 pending = 一次 bool 短路。
    p.AddSystem(std::make_unique<SceneSwitchSystem>());
    p.ResolveOrder();
}

} // namespace lemon::ecs
