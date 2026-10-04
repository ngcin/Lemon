// Lemon 引擎 — 游戏相机跟随（M7a 批③；M4.7/M4.3 随迁）
// 自 EditorApp::UpdateGameCameraFollow 纯函数化下沉（搬家非复制）：目标优先级
//   ① tag "Camera"——显式相机位实体（进阶：也可作空场景的固定取景）；
//   ② tag "Player"——默认跟随玩家；
//   ③ 首个挂脚本实体——blank 模板默认名"Sprite"+InputMover 的兜底。
// 首帧吸附（不从旧位滑过去），之后刚性跟随（center = 目标，零滞后——阻尼版的
// 稳态滞后在走/停切换时反演成 ~28px 往返滑移 = 「抖动」观感，手测第十轮定案；
// 电影感阻尼留给 C# 相机门面按需启用 Camera2D::Follow）。
// EditorApp（GameView）与 lemon-game 主循环（批④）两薄壳消费——杜绝双实现漂移。
#pragma once

#include "ECS/Entity.h"
#include "Renderer/Camera2D.h"

namespace lemon::ecs {
class Scene;
}

namespace lemon::renderer {

/// 跟随会话态（消费方逐帧持有；#27：目标缓存 + 逐帧轻校验——原每帧全池线性扫
///（Scene::Each 无早退机制），万实体场景 = 每帧上万迭代；校验（活着 + 有
/// Transform + 判据仍成立）失败才重扫。校验语义与每帧重扫一致：换世界/目标
/// 死亡/换 tag 即失校验；同 id 重生同 tag（director 重挂 prefab）= 同一逻辑
/// 目标，继续跟）
struct CameraFollowState {
    bool active = false;                    // 已吸附（首帧起；退出 Play 复位）
    ecs::Entity camEnt = ecs::Entity::Null();
    ecs::Entity playerEnt = ecs::Entity::Null();
    ecs::Entity scriptedEnt = ecs::Entity::Null();
};

/// 每帧推进。outTargetPos（可空）= 本次目标世界位（LEMON_PLAY_DIAG 回传等诊断
/// 消费）。返回 true = 本帧有目标并吸附。无目标：保持现位。
bool UpdateCameraFollow(ecs::Scene& s, Camera2D& cam, CameraFollowState& st,
                        Vec2* outTargetPos = nullptr);

/// 退出 Play 复位：编辑态默认位 (640,360)（ViewportRenderer 口径）+ 态清零。
/// 返回 true = 本次实际复位（active 在场时）。
bool ResetCameraFollow(Camera2D& cam, CameraFollowState& st);

} // namespace lemon::renderer
