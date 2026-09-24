// Lemon 引擎 — 03 §4 17 系统实现 + World::InstallDefaultSystems
#include "Systems/Systems.h"

#include <algorithm>
#include <cmath>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "ECS/Scene.h"
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

void TargetBoard::Rebuild(Scene& scene, bool collectAll) {
    for (auto& t : teams_) t.list.clear();
    all_.clear();
    auto view = scene.View<Meta, Transform2D>();
    for (auto [ent, meta, tf] : view.each()) {
        TargetEntry entry{Scene::FromEntt(ent), tf.pos};
        if (collectAll) all_.push_back(entry);
        for (auto& t : teams_)
            if (t.id == meta.team) t.list.push_back(entry);
    }
}

Entity TargetBoard::Nearest(uint32_t team, Vec2 from, float range,
                            Entity exclude) const {
    const std::vector<TargetEntry>* list = nullptr;
    for (const auto& t : teams_)
        if (t.id == team) {
            list = &t.list;
            break;
        }
    if (!list) return Entity::Null();
    Entity best = Entity::Null();
    float bestD2 = range * range;
    for (const TargetEntry& te : *list) {
        if (te.e == exclude) continue;
        float d2 = LengthSq(te.pos - from);
        if (d2 < bestD2) { // 严格小于：等距保留池序靠前者（确定性）
            bestD2 = d2;
            best = te.e;
        }
    }
    return best;
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

    for (auto [ent, wd, tf] : scene.View<WaveDirector, Transform2D>().each()) {
        // 容量防御钳（Inspector/JSON 手改超容；读档侧 ReadArraySeg 另有一道）
        const uint8_t waveCount = wd.waveCount > 16 ? 16 : wd.waveCount;

        wd.time += dt;
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
            Entity spawned = spawn(scene, e.prefabId, pos, wd.spawnTeam);
            if (spawned.IsNull()) { // 工厂不认此 prefab：废止条目（不逐 tick 重试）
                wd.waveSpawned[i] = e.count;
                continue;
            }
            ++wd.waveSpawned[i];
            ++alive; // 乐观自增（普查刷新前的本 tick 内闸门）

            EventPacket p{};
            p.type = GameEvent::Spawn; // 与 SpawnSystem 同口径
            p.src = spawned;
            p.payload[0] = pos.x;
            p.payload[1] = pos.y;
            world.Events().Push(p);

            const float interval =
                wave.rampMult > 0.0f ? e.interval / wave.rampMult : e.interval;
            wd.waveCooldown[i] += interval > minInterval ? interval : minInterval;
        }
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

    auto view = scene.View<Spawner, Transform2D>();

    for (auto [ent, sp, tf] : view.each()) {
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
            Entity e = spawn(scene, sp.prefabId, tf.pos + offset, sp.spawnTeam);
            if (e.IsNull()) break;
            ++teamCounts_[sp.spawnTeam & 63];
            EventPacket p{};
            p.type = GameEvent::Spawn;
            p.src = e;
            p.payload[0] = tf.pos.x + offset.x;
            p.payload[1] = tf.pos.y + offset.y;
            world.Events().Push(p);
        }
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
    board_.Rebuild(scene, hasFlee);

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

                ch.target = board_.Nearest(ch.targetTeam, tf.pos, ch.aggroRange,
                                           Scene::FromEntt(ent));
                if (ch.target.IsNull()) {
                    vel.v = Vec2::Zero();
                    continue;
                }
                Vec2 toT = scene.Get<Transform2D>(ch.target).pos - tf.pos;
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

    // Shooter：冷却到点朝目标发射投射物（生成走工厂）
    {
        auto view = scene.View<Shooter, Transform2D>();
        for (auto [ent, sh, tf] : view.each()) {
            sh.cooldown -= dt;
            sh.target = board_.Nearest(sh.targetTeam, tf.pos, sh.range,
                                       Scene::FromEntt(ent));
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
            if (Entity proj = spawn(scene, sh.projectileId, tf.pos + dir * 12.0f,
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

    // Patrol：往返（暂停逻辑随 clip/玩法层完善，M5）
    {
        auto view = scene.View<Patrol, Transform2D, Velocity>();
        for (auto [ent, pt, tf, vel] : view.each()) {
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

            // 密度截断（03 §14"密度上限"）：每实体只处理前 maxNeighbors 个
            // 有效邻居。哈希回调序 = cell 序 → id 升序 → 截断确定（回放安全）。
            // 万怪堆叠时若不截断，cell 内遍历退化为 O(n²)（实测 6ms@1k）。
            Vec2 push{};
            uint32_t count = 0;
            scene.Spatial().OverlapCircle(
                scene, tf.pos, r, physics2d::QueryFilter{}, 0.0f,
                [&](Entity other, const Transform2D& otf) -> bool {
                    if (other == Scene::FromEntt(ent)) return true;
                    const Meta* om = scene.TryGet<Meta>(other);
                    if (!om) return true;
                    // 只有 soft-collide 关系才分离（03 §9；默认表：怪群同队）
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
                xp.xpToNext = std::max(1.0f, std::ceil(xp.xpToNext * xpCurveK));
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
            // M2：时间推进（含 loop 回绕）；占位周期 1.0 保证 time 有界
            an.time += an.speed * dt;
            if (an.loop) {
                float period = 1.0f; // 占位周期；无 clip 路径恒此值（勿动——金档锚）
                while (an.time >= period) an.time -= period;
            }
            continue;
        }
        // playOnStart=0 = 暂停开关（time/curFrame/spriteId/换段队列全冻结）
        if (!an.playOnStart) continue;
        an.time += an.speed * dt; // 缩放 dt：timeScale=0 冻结动画（批① D5 同语义）
        const float total = (float)clip->frames.size() / clip->fps;
        bool wrapped = false; // 本 tick 发生回绕减法（Queue 的 loop 段切点）
        if (an.loop) {
            while (an.time >= total) {
                an.time -= total;
                wrapped = true;
            }
            if (an.time < 0.0f) an.time = 0.0f; // 负 speed 防御（回绕后仍负）
        } else {
            if (an.time < 0.0f) an.time = 0.0f;
            if (an.time > total) an.time = total; // 钳末帧：M2"无界增长"随 clip 收口
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
        uint32_t f = (uint32_t)(an.time * clip->fps);
        if (f >= clip->frames.size()) f = (uint32_t)clip->frames.size() - 1; // total 边界
        an.curFrame = (uint16_t)f;
        if (SpriteRenderer* sr = scene.TryGet<SpriteRenderer>(Scene::FromEntt(ent)))
            sr->spriteId = clip->frames[f]; // SpriteRenderer 可缺 = 纯计时推进
    }
}

// ---------------------------------------------------- #14 投射物回收 ----
void ProjectileLifetimeSystem::Tick(World& world, Scene& scene, float dt) {
    auto& pool = scene.Pool<Projectile>();
    const uint32_t n = (uint32_t)pool.size();
    world.Jobs().ParallelFor(n, 256, [&](uint32_t b, uint32_t e) {
        for (uint32_t i = b; i < e; ++i) {
            entt::entity ent = pool[i];
            Projectile& pr = pool.get(ent);
            pr.age += dt;
            if (pr.age < pr.lifetime) continue;
            scene.Destroy(Scene::FromEntt(ent)); // 两阶段：回收仍走统一提交
        }
    });
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

// ---------------------------------------------------- #16 事件派发 --------
void ScriptEventDispatchSystem::Tick(World& world, Scene& scene, float dt) {
    (void)scene; (void)dt;
    auto& events = world.Events();
    // C# 桥（M3-4）：头部拉脚本 pending 入队（当帧派发）+ 两段零拷贝转发 C# 订阅者
    if (auto* backend = world.ScriptBackend()) backend->DispatchEvents(world, scene);
    const World::EventSink& sink = world.GetEventSink();
    const uint32_t count = events.Size();
    for (uint32_t i = 0; i < count; ++i) {
        const EventPacket& p = events.At(i);
        if (sink) sink(world, p);
    }
    events.Clear(); // 帧末清空（03 §11）
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
    p.AddSystem(std::make_unique<SeparationSystem>());
    p.AddSystem(std::make_unique<MovementSystem>());
    p.AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    p.AddSystem(std::make_unique<PickupSystem>());
    p.AddSystem(std::make_unique<HitboxSystem>());
    p.AddSystem(std::make_unique<TriggerSystem>());
    p.AddSystem(std::make_unique<StatSystem>());
    p.AddSystem(std::make_unique<AnimatorSystem>());
    p.AddSystem(std::make_unique<ProjectileLifetimeSystem>());
    p.AddSystem(std::make_unique<CSharpBatchSystem>());
    p.AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    p.AddSystem(std::make_unique<DestroyCommitSystem>()); // Essential 阶段
    p.ResolveOrder();
}

} // namespace lemon::ecs
