// Lemon 引擎 — 03 §4 16 系统实现 + World::InstallDefaultSystems
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
void DirectorSystem::Tick(World& world, Scene& scene, float dt) {
    // M5：波次表/budget 曲线/capAlive 压测保护（03 §8）。M2 占位。
    (void)world; (void)scene; (void)dt;
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

// --------------------------------------------------------------- #9 命中 --
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
        scene.Spatial().OverlapCircle(
            scene, tf.pos, 6.0f, f, 6.0f,
            [&](Entity hit, const Transform2D& htf) {
                (void)htf;
                const Meta* hm = scene.TryGet<Meta>(hit);
                if (!hm || !teams.Hostile(projTeam, hm->team)) return true;
                Health* hp = scene.TryGet<Health>(hit);
                if (!hp) return true;
                if (hp->cur <= 0.0f) return true; // 当帧已死（防多源重复 Death 事件）
                if (hp->iFrames > 0.0f) return true; // 无敌帧免疫
                hp->cur -= pr.damage;
                hp->iFrames = 0.1f; // 帧内多弹去重（全量 iFrames 策略 M5 细化）

                EventPacket ev{};
                ev.type = GameEvent::Hit;
                ev.src = Scene::FromEntt(ent);
                ev.dst = hit;
                ev.payload[0] = pr.damage;
                ev.payload[1] = htf.pos.x;
                ev.payload[2] = htf.pos.y;
                events.Push(ev);

                // 击退（割草手感）：沿弹道方向脉冲
                if (Knockback* kb = scene.TryGet<Knockback>(hit)) {
                    if (const Velocity* pv = scene.Registry().try_get<Velocity>(ent))
                        kb->impulse += Normalize(pv->v) * 60.0f;
                }

                if (hp->cur <= 0.0f) {
                    hp->cur = 0.0f;
                    EventPacket death{};
                    death.type = GameEvent::Death;
                    death.src = hit;
                    events.Push(death);
                    scene.Destroy(hit); // 两阶段：当帧仍可访问
                }

                if (pr.pierce > 0) {
                    --pr.pierce; // 继续穿透（命中去重集 M5：iFrames 已挡同帧重复）
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

    // Hazard 持续伤害区（tick 节拍）
    auto hzView = scene.View<Hazard, Transform2D>();
    for (auto [ent, hz, tf] : hzView.each()) {
        hz.tickPhase -= dt;
        if (hz.tickPhase > 0.0f) continue;
        hz.tickPhase += hz.tickInterval;

        const Meta* hm = scene.TryGet<Meta>(Scene::FromEntt(ent));
        uint32_t zoneTeam = hm ? hm->team : 0;
        physics2d::QueryFilter f;
        f.exclude = Scene::FromEntt(ent);
        scene.Spatial().OverlapCircle(
            scene, tf.pos, 48.0f, f, 8.0f,
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
                        scene.Destroy(hit);
                    }
                }
                return true;
            });
    }
}

// ------------------------------------------------------------- #10 触发器 --
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

// ------------------------------------------------------------- #11 数值 ----
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
    // 经验/升级（幂曲线；VS 曲线资产化 M5）
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

// ------------------------------------------------------------- #12 动画 ----
void AnimatorSystem::Tick(World& world, Scene& scene, float dt) {
    (void)world;
    // M2：时间推进（含 loop 回绕）；帧号映射需 clip 资产表（M5 接入后写
    // SpriteRenderer.spriteId），此处推进至回绕保证 time 有界
    auto view = scene.View<Animator2D>();
    for (auto [ent, an] : view.each()) {
        an.time += an.speed * dt;
        if (an.loop) {
            float period = 1.0f; // 占位周期；clip 表接入后 = frames/fps
            while (an.time >= period) an.time -= period;
        }
    }
}

// ---------------------------------------------------- #13 投射物回收 ----
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

// ---------------------------------------------------- #14 C# 批量（M3）----
void CSharpBatchSystem::Tick(World& world, Scene& scene, float dt) {
    // 桥后端（ScriptHost）构造块描述符（本线程）→ 域线程执行（ADR-010 D1）；未注入则空跑
    if (auto* backend = world.ScriptBackend()) backend->TickBatch(world, scene, dt);
}

// ---------------------------------------------------- #15 事件派发 --------
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

// ---------------------------------------------------- #16 销毁提交 --------
void DestroyCommitSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    // M3-6：脚本结构命令帧首应用（建/删实体、增删组件、挂脚本；先于销毁提交——
    // Destroy 命令本批内随后的 CommitDestroys 直接生效）
    if (auto* backend = world.ScriptBackend()) backend->ApplyStructural(world, scene);
    scene.CommitDestroys(); // 两阶段销毁 + EnTT 实体回收（池语义）
}

// ---------------------------------------------------- 默认管线安装 --------
void World::InstallDefaultSystems() {
    auto& p = Pipeline();
    // 注册序 = 03 §4 表序 = 系统 RNG 子流 id（改动序号 = 破坏回放兼容，禁）
    p.AddSystem(std::make_unique<InputSnapshotSystem>());
    p.AddSystem(std::make_unique<DirectorSystem>());
    p.AddSystem(std::make_unique<SpawnSystem>());
    p.AddSystem(std::make_unique<AISystem>());
    p.AddSystem(std::make_unique<NavigationSystem>());
    p.AddSystem(std::make_unique<SeparationSystem>());
    p.AddSystem(std::make_unique<MovementSystem>());
    p.AddSystem(std::make_unique<SpatialHashRebuildSystem>());
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
