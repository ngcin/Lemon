// Lemon 引擎 — Team 势力系统（03 文档 §9；schema 照抄 yami teams.json，数据格式）
// 关系类型：hostile（命中判定）/ friendly / neutral / soft-collide（只分离不伤害）/
// ghost（互相穿透）。默认表 = 塔防与幸存者"玩家 vs 怪物海"全部敌我语义。
// M2 内联默认表 + 运行时覆写 API；JSON 资产加载（Data/teams.json）M4 资产库接入。
#pragma once

#include <cstdint>
#include <cstring>

#include "Core/Log.h"

namespace lemon::ecs {

// Ghost 为零值：未显式声明的势力对默认互不影响（幽灵穿透）——
// "未配置 = 万物互伤"是不可接受的失败模式
enum class TeamRelation : uint8_t { Ghost = 0, Friendly, Neutral, SoftCollide, Hostile };

class TeamTable {
public:
    static constexpr uint32_t kMaxTeams = 32;

    void SetRelation(uint32_t a, uint32_t b, TeamRelation r) {
        CheckTeam(a); CheckTeam(b);
        // 对称关系（yami schema 的 "0-1" 键即无序对）
        rel_[a * kMaxTeams + b] = (uint8_t)r;
        rel_[b * kMaxTeams + a] = (uint8_t)r;
    }

    TeamRelation Relation(uint32_t a, uint32_t b) const {
        if (a >= kMaxTeams || b >= kMaxTeams) return TeamRelation::Neutral;
        return (TeamRelation)rel_[a * kMaxTeams + b];
    }
    bool Hostile(uint32_t a, uint32_t b) const {
        return Relation(a, b) == TeamRelation::Hostile;
    }
    bool SoftCollide(uint32_t a, uint32_t b) const {
        return Relation(a, b) == TeamRelation::SoftCollide;
    }

    /// 默认表：0=player 1=monsters 2=neutral 3=playerBullets
    /// 玩家队 vs 怪物海 hostile；怪物互相同 soft-collide（分离不伤害）；
    /// 弹幕 ghost 穿透玩家、hostile 命中怪物。
    static TeamTable Default() {
        TeamTable t;
        t.SetRelation(0, 1, TeamRelation::Hostile);
        t.SetRelation(1, 3, TeamRelation::Hostile); // 怪物 vs 玩家弹幕
        t.SetRelation(0, 2, TeamRelation::Neutral);
        t.SetRelation(1, 1, TeamRelation::SoftCollide); // 怪群软碰撞（分离力）
        t.SetRelation(0, 3, TeamRelation::Ghost);       // 弹幕不挡玩家
        t.SetRelation(2, 2, TeamRelation::Ghost);       // 中立物互穿
        return t;
    }

private:
    void CheckTeam(uint32_t t) const {
        LEMON_ASSERT(t < kMaxTeams, "team id %u out of range", t);
    }
    uint8_t rel_[kMaxTeams * kMaxTeams] = {}; // 0 = Ghost（默认关系，见枚举注释）
};

} // namespace lemon::ecs
