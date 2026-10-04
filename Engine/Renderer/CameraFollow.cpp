// Lemon 引擎 — 游戏相机跟随实现（M7a 批③ 自 EditorApp 搬家，逐行同源；日志
// 字符串逐字节保留）
#include "Renderer/CameraFollow.h"

#include <cctype>

#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "ECS/Scene.h"
#include "Scripting/ScriptBox.h"

namespace lemon::renderer {
namespace {
// 标签大小写不敏感比较（Meta.tag 固定 24B，无终止符风险由调用方保证）
bool TagEquals(const char* tag, const char* want) {
    if (!tag) return false;
    while (*tag && *want) {
        if (std::tolower((unsigned char)*tag) != std::tolower((unsigned char)*want))
            return false;
        ++tag;
        ++want;
    }
    return *tag == *want;
}
} // namespace

bool UpdateCameraFollow(ecs::Scene& s, Camera2D& cam, CameraFollowState& st,
                        Vec2* outTargetPos) {
    auto stillValid = [&s](ecs::Entity& cached, bool wantScriptBox, const char* tag) {
        if (cached.IsNull() || !s.Alive(cached) || !s.Has<ecs::Transform2D>(cached)) {
            cached = ecs::Entity::Null();
            return false;
        }
        if (wantScriptBox && !s.Has<scripting::ScriptBox>(cached)) {
            cached = ecs::Entity::Null();
            return false;
        }
        if (!wantScriptBox &&
            !TagEquals(s.TryGet<ecs::Meta>(cached) ? s.TryGet<ecs::Meta>(cached)->tag
                                                   : nullptr, tag)) {
            cached = ecs::Entity::Null();
            return false;
        }
        return true;
    };
    const bool allValid = stillValid(st.camEnt, false, "Camera") &&
                          stillValid(st.playerEnt, false, "Player") &&
                          stillValid(st.scriptedEnt, true, nullptr);
    if (!allValid) {
        st.camEnt = st.playerEnt = st.scriptedEnt = ecs::Entity::Null();
        s.Each([&](ecs::Entity e) {
            const bool hasTf = s.Has<ecs::Transform2D>(e);
            if (!hasTf) return;
            const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
            const char* tag = m ? m->tag : nullptr;
            if (st.camEnt.IsNull() && TagEquals(tag, "Camera")) st.camEnt = e;
            if (st.playerEnt.IsNull() && TagEquals(tag, "Player")) st.playerEnt = e;
            if (st.scriptedEnt.IsNull() && s.Has<scripting::ScriptBox>(e))
                st.scriptedEnt = e;
        });
    }
    const ecs::Entity camEnt = st.camEnt, playerEnt = st.playerEnt,
                      scriptedEnt = st.scriptedEnt;
    const ecs::Entity target = !camEnt.IsNull()     ? camEnt
                               : !playerEnt.IsNull() ? playerEnt
                                                     : scriptedEnt;
    if (target.IsNull()) return false; // 无目标：保持现位
    ecs::WorldTransform2D wt{};
    Vec2 pos = s.Get<ecs::Transform2D>(target).pos; // 父链异常兜底本地位
    if (ecs::ComputeWorldTransform(s, target, wt)) pos = wt.pos;
    if (outTargetPos) *outTargetPos = pos;
    if (!st.active) {
        st.active = true;
        const char* tag = "脚本实体";
        if (!camEnt.IsNull()) tag = "Camera";
        else if (!playerEnt.IsNull()) tag = "Player";
        LEMON_LOG("游戏相机跟随：%s", tag);
    }
    cam.center = pos;
    return true;
}

bool ResetCameraFollow(Camera2D& cam, CameraFollowState& st) {
    if (!st.active) return false;
    cam.center = {640, 360};
    st = CameraFollowState{};
    return true;
}

} // namespace lemon::renderer
