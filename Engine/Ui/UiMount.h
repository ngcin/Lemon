// Lemon 引擎 — 场景 UIDocument 声明式装载（M7a 批③；M6b 批③d 前置通道 A 随迁）
// 自 EditorAppUiBridge 下沉（搬家非复制，批① D8 修复随装载走）：EnterPlay 扫
// 场景 View<UIDocument> → 去重 → guid 解析 → 装载 + 声明态归位 + stale 清场；
// 文件装载文档对账逐出（资产不健康 → Unload，防事件被裸重扫吞噬的残留屏）。
// 编辑器（AssetDatabase 源）与独立运行时（AssetIndex 源）共用；批④ lemon-game
// 装配期消费。无 UIDocument 的场景（bench/replay/基准场）装载调用恒 0——装载点
// 只在装配扫描处，不进任何通用路径（基准护栏 §5）。
#pragma once

#include <cstdint>
#include <string>

#include "Ui/UiSubsystem.h"

namespace lemon::ecs {
class Scene;
}

namespace lemon::ui {

/// .rml 资产解析源（SpriteRefSource 同款纪律：编辑器 AssetDatabase / 运行时
/// AssetIndex 双实现）
class UiDocSource {
public:
    virtual ~UiDocSource() = default;
    /// guid → 健康 .rml 资产（缺失/非 Rml 类型 = false——响亮失败语义在调用侧）
    virtual bool ResolveRml(uint64_t guid, std::string& relPath,
                            std::string& absPath) const = 0;
    /// relPath 是否健康 .rml（对账逐出判据；场景装载路径同判据）
    virtual bool IsHealthyRml(const std::string& relPath) const = 0;
};

/// EnterPlay 声明式装载（通道 A）：扫 View<ecs::UIDocument>，同 GUID 去重；
/// guid 悬空/非 Rml = 红字 + 本屏不装载（绝不静默空屏）；showOnStart=0 = 装载
/// 但隐藏（动态屏）、modal 初值入 Doc；装载来源 = Scene（清场判据 UiDocOrigin）。
/// 尾声 ResetDynamicDocuments：未声明且 stale → Hide + 清 stale。返回装载成功数。
uint32_t MountSceneDocuments(UiSubsystem& ui, ecs::Scene& scene, const UiDocSource& src);

/// 状态对账（2026-09-29 根因收口）：文件装载文档的 relPath 非健康 .rml 资产 →
/// 逐出。事件驱动路径保留（首拍即逐出 + 「已卸载」观测日志），对账是兜住
/// "事件被谁吃了"的不变量层——两者幂等共存。进 Play 不变量层，随装载头调用。
void ReconcileDocuments(UiSubsystem& ui, const UiDocSource& src);

} // namespace lemon::ui
