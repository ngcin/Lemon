// Lemon 引擎 — 游戏存档三通道落盘（M7a 批③；M5 批④ D1 / M6a 批② T5 随迁）
// 自 EditorContext 下沉（搬家非复制）：路径 = <项目根>/.lemon/saves/，三档三文件
//（slot_0 / settings / meta），06 §10 防损坏三件套 = 版本头（SaveChannel.Encode）+
// 原子改名（WriteFileAtomic）+ .bak；坏档兜底按档隔离。编辑器 EditorContext 与
// 独立运行时（M7a 批④ lemon-game）共用；包形态写位（便携 vs OS 用户目录）= ADR-016
// D5 便携口径（与开发态同相对路径零分支）。
#pragma once

#include <cstdint>
#include <string>

#include "ECS/SaveChannel.h"

namespace lemon::assets {

/// 三档存档宿主（纯静态——状态在 World::Saves(ch) 通道内，此处只有 IO）。
class SaveStore {
public:
    /// 存档路径 = root/.lemon/saves/{slot_0,settings,meta}.sav（root 空 / ch 越界
    /// 钳 slot；无项目调用方传空 root = 全链 no-op 的既有口径）
    static std::string FilePath(const std::string& projectRoot, uint8_t ch);

    /// 通道 → 文件（旧档转 .bak → tmp 写 → 原子改名；空通道/无 root = false）
    static bool Write(const std::string& projectRoot, uint8_t ch,
                      const ecs::SaveChannel& chn);

    /// 文件 → 通道（EnterPlay 载入；坏档红字后试 .bak，再坏 = 空通道开局）。
    /// slot 档含旧 game.sav 惰性迁移：新档（含 .bak）不存在且旧名在 → 读旧路径
    ///（写恒写新名，免 rename 竞态；旧文件保留不删）
    static void Load(const std::string& projectRoot, uint8_t ch, ecs::SaveChannel& dst);
};

} // namespace lemon::assets
