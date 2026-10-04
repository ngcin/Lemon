// Lemon 引擎 — 场景 UIDocument 声明式装载实现（M7a 批③ 自 EditorAppUiBridge
// 搬家，逐行同源；日志字符串逐字节保留）
#include "Ui/UiMount.h"

#include <algorithm>
#include <vector>

#include "Components/UiComponents.h"
#include "Core/Log.h"
#include "ECS/Scene.h"

namespace lemon::ui {

uint32_t MountSceneDocuments(UiSubsystem& ui, ecs::Scene& scene, const UiDocSource& src) {
    // 进 Play 对账（形态 C 治愈位）：文件装载文档的 relPath 非健康 .rml 资产 → 逐出。
    // "本屏不装载"是画面层不变量——事件被裸 Rescan 吞掉的残留在此清场
    ReconcileDocuments(ui, src);
    uint32_t loaded = 0, missing = 0;
    std::vector<std::string> declared;
    std::vector<uint64_t> seen; // 同 GUID 去重（一屏两实体无意义；文档量小线性足够）
    scene.View<ecs::UIDocument>().each([&](auto raw, ecs::UIDocument& ud) {
        const ecs::Entity e = ecs::Scene::FromEntt(raw);
        if (ud.sourceAssetGuid == 0) return; // 未挂（合法；Inspector 提示）
        if (std::find(seen.begin(), seen.end(), ud.sourceAssetGuid) != seen.end()) {
            LEMON_WARN("UIDocument：实体 %llu 重复挂同一 .rml（guid %016llx）——"
                       "一屏两实体无意义，已去重装载",
                       (unsigned long long)e.id,
                       (unsigned long long)ud.sourceAssetGuid);
            return;
        }
        seen.push_back(ud.sourceAssetGuid);
        std::string relPath, absPath;
        if (!src.ResolveRml(ud.sourceAssetGuid, relPath, absPath)) {
            ++missing; // 响亮失败：绝不静默空屏（guid-chain 同款 per-entity resolve）
            LEMON_ERROR("UIDocument：资产缺失或非 .rml（guid %016llx，实体 %llu）——"
                        "本屏不装载；修复资产或重挂后重进 Play",
                        (unsigned long long)ud.sourceAssetGuid,
                        (unsigned long long)e.id);
            return;
        }
        if (ui.LoadDocumentFromFile(relPath.c_str(), absPath.c_str(),
                                    lemon::ui::UiDocOrigin::Scene)) {
            ++loaded;
            // 声明态归位：showOnStart=0 = 装载但隐藏（动态屏）；modal 初值入 Doc
            ui.ShowDocument(relPath.c_str(), ud.showOnStart != 0, ud.modal != 0);
            declared.push_back(relPath);
        }
    });
    ui.ResetDynamicDocuments(declared); // §3：未声明且 stale → Hide + 清 stale
    if (loaded || missing)
        LEMON_LOG("UIDocument 装载：%u 成功 / %u 缺失（声明态归位 + stale 清场）",
                  loaded, missing);
    return loaded;
}

void ReconcileDocuments(UiSubsystem& ui, const UiDocSource& src) {
    for (const std::string& name : ui.FileBackedDocumentNames()) {
        if (src.IsHealthyRml(name)) continue;
        if (ui.UnloadDocument(name.c_str()))
            LEMON_LOG("UI 文档对账逐出（资产不健康——事件可能被裸重扫吞噬）：%s",
                      name.c_str());
    }
}

} // namespace lemon::ui
