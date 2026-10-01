// Lemon 编辑器 — --smoke-uirml 冒烟族（批③c-5 自 EditorApp.cpp Run 外迁：
// 独立进 Play / 帧链（双通道·层序·三局往返·僵尸三形态·热重载）/ 末帧裁决。
// 模式同批③c-1..4：T5 断言位收敛 UirmlSmokeState（本 TU 匿名 ns——状态仅
// Frame/Verdict 两面访问）+ 挂点原位、帧号锚定/执行时序逐位不变。
// SeedSmokeUiRmlProject/SeedSmokeUiDocument（批③a）仍在 EditorAppSmoke.cpp。

#include "App/EditorApp.h"
#include "App/EditorApp.h"
#include "App/EditorAppSmoke.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include "stb_image_write.h"
#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Components/UiComponents.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）

namespace lemon::editor {

namespace {

// 批③d 前置 T5：UIDocument 双通道/层序/stale 断言位（帧钩子写、裁决读；
// 批③c-5 自 Run 局部收敛——smokeUiEvText_ 是 EditorApp 成员，不在此列）
struct UirmlSmokeState {
    int smokeUiLayerBTopN = -1, smokeUiLayerATopN = -1; // 帧中点两捕获的 #802040 计数
    bool smokeUiStaleOk = false, smokeUiKeepCOk = false; // 第二局动态屏 Hide / Edit 双击保持
    uint32_t smokeUiLoadsP2 = 0; // 第二局装载增量（恰 1 = 只重装声明文档）
    bool smokeUiExitP2 = false; // Play→Stop→Play 往返完成
    bool smokeUiHasDocB = false; // 220 帧前 dyn 存量（删除播种后终帧恒 false，不能终帧读）
    bool smokeUiDelEvictOk = false; // 真人验收②回灌：删 .rml → 已装载文档逐出
    char smokeUiDynText[64] = {}; // 220 帧前 dyntitle 快照（同上——逐出后终帧读恒空）
    // 真人验收②二轮回灌：僵尸渲染双防线（簿记清 ≠ 画面清）。形态 A = Play 中删
    // 正在显示的文档（editprev，212 删 → 253 活画面像素须清零）；形态 B = 编辑态
    // 删（302 双击重装载 → 304 删 → 345 重进 Play 须无残留）。seed = 删前确在显示
    bool smokeUiEvictSeedOk = false;
    int smokeUiZombiePixN = -1, smokeUiEvictPixN = -1; // 253 / 348 帧 #604080 计数
    // 形态 D（2026-09-29 用户实报收口）：删场景 UIDocument 实体 → 重进 Play 不
    // 得残留显示。stopHide = 405 Stop 后 Scene 文档即被清场（HideNonEditDocuments）
    // 且 Edit 预览豁免；delEnt = 410 第四局（无脚本模式全量断言——脚本模式 C# 帧 1
    // 会经通道 B 合法拉回，非僵尸）：uirml 装载保留 + 不显示 + 装载增量 0；pix =
    // 411 帧渲染块 #802040 计数（<5）。420 重建实体进终局，终帧画面回归原口径
    bool smokeUiExitHideOk = false, smokeUiDelEntOk = false;
    int smokeUiDelEntPixN = -1;
    uint32_t smokeUiLoadsP4Base = 0; // 405 Stop 时装载计数（第四局增量断言基线）
    // 形态 D 二段：resetSeed = 421 直调 ExitPlay（绕过清场）后 uirml 仍 shown
    //（防线一确不在场）；resetOnly = 424 第六局 Reset origin 判据独自完成清场
    bool smokeUiResetSeedOk = false, smokeUiResetOnlyOk = false;
    int smokeUiNegP2 = -1; // 251 帧负面行容器快照（终帧在三局——通道 A 重装已清）
    bool smokeUiEvictEditOk = false; // 三局入 Play 后 editprev 已不在 docs
    // 竞速修（2026-10-01，M6c 批④ 回归期发现）：复种复活靠 500ms watcher 轮询，
    // 帧锚定单发装载（302/394）在快机上竞速落空 → 405 清场断言连锁红。两装载
    // 改重试窗（成功即停；FindByPath 须过 !missing——墓碑命中会假成功静默跳载）
    bool smokeUiFormBSeeded = false, smokeUiFinalSeeded = false;
};
UirmlSmokeState g_uirmlSmoke;

} // namespace

// ---- --smoke-uirml 独立进 Play（批③a ADR-014；批③c-5 自 Run 外迁：守卫
// launchCopy_.smokeUirml && !ctx_.Playing() 留挂点原位）----
bool EditorApp::SmokeUirmlEnterPlay() {
    if (PlayBlockedByScripts()) {
        LEMON_ERROR("smoke-uirml 已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）");
        return false;
    }
    // 批③c：--script 时挂 UI 全链探针（此处场景已定型——Init 期创建会被后续
    // 场景装配块 OpenScene/SeedSmoke 换掉，快照 0 实体实测教训）
    if (ctx_.Scripts()) {
        ecs::Entity probe = ctx_.ActiveScene().Create();
        ctx_.AttachScript(probe, 0, "UiProbeBehaviour");
        LEMON_LOG("uirml 播种：UiProbeBehaviour 挂载（实体 %llu）",
                  (unsigned long long)probe.id);
    }
    // 批③d 前置 T5：夹具场景声明主文档（通道 A 验收面）——UIDocument 进快照，
    // EnterPlay 声明式装载 + showOnStart 归位；dyn/editprev 不声明（通道 B /
    // Edit 双击装载两块独立断言面）
    {
        ecs::Entity ue = ctx_.CreateEntity("UIDocument");
        ecs::UIDocument& ud = ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue);
        if (const AssetEntry* en = ctx_.Assets().FindByPath("Assets/UI/uirml.rml"))
            ud.sourceAssetGuid = en->guid;
        else
            LEMON_ERROR("uirml 夹具缺 Assets/UI/uirml.rml——UIDocument 播种失败");
    }
    if (!ctx_.EnterPlay()) return false;
    MountSceneUiDocuments(); // 批③d 前置（通道 A）：夹具主文档声明装载
    // 批③b 补：Play 按钮/菜单路径都设的翻页标志——③a 独立进 Play 分支漏了它，
    // 中央区标签页停在 Scene，--screenshot（交换链）只见 Scene 不见 UI（用户
    // 走查 2026-09-28 报；gameRT 本身有 UI，像素断言不受影响）
    tabFocusPending_ = 1;
    return true;
}

// ---- --smoke-uirml 帧链（M6b 批③~d：合成点击/负面契约/通道 B 兜底/层序两拍/
// 三局往返/删除逐出/僵尸三形态/形态 D 两段/热重载两段；批③c-5 自 Run 外迁）----
void EditorApp::SmokeUirmlFrame(uint64_t frame) {
    if (launchCopy_.smokeUirml && !launchCopy_.projectDir.empty()) {
        namespace fs = std::filesystem;
        const fs::path uiDir = fs::path(launchCopy_.projectDir) / "Assets" / "UI";
        auto rewriteFile = [](const fs::path& p, const std::string& from, const std::string& to) {
            std::ifstream in(p, std::ios::binary);
            std::string t((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
            const size_t at = t.find(from);
            if (at == std::string::npos) return false;
            t.replace(at, from.size(), to);
            std::ofstream out(p, std::ios::trunc);
            out << t;
            return true;
        };
        // 批③c：M7 合成点击全链（帧 60 按下/61 释放，经 ImGui 注入 →
        // FeedGameUiInput → RmlUi → UiEvent → 下一帧 #16 → C# 回执 → RtUi uiev）
        if ((frame == 60 || frame == 61) && gameUi_) {
            float cx, cy;
            if (gameUi_->TryGetItemCenter("Assets/UI/uirml.rml", "cards", "opt0", &cx,
                                          &cy)) {
                const float sx = gvCanvasX_ + cx * gvCanvasW_ / (float)gvRtW_;
                const float sy = gvCanvasY_ + cy * gvCanvasH_ / (float)gvRtH_;
                ui_->SetInputOverride(sx, sy, frame == 60 ? 1 : 0);
            } else {
                LEMON_ERROR("uirml-smoke: 点击注入失败——cards/opt0 未克隆（帧 %llu，"
                            "items=%d neg=%d pending=%u）",
                            (unsigned long long)frame,
                            gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "cards"),
                            gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "negbox"),
                            gameUi_->PendingEventCount());
            }
        }
        // 批③c：负面契约 op 直灌（原 frame 160；批③d 前置移 210 = 第二局内——
        // EnterPlay 通道 A 重装主文档会 InvalidateContainers，160 时点的负面容器
        // 活不到终帧）：field 'nope' 不在模板集（has: lab）→ 响亮失败恰 1 +
        // 负面容器克隆 1 行（不渲染）。行块用八进制转义（十六进制 \x 会贪婪
        // 连吃后续 hex 字母——C 词法坑）
        if (frame == 210 && gameUi_ && ctx_.Playing()) {
            static const char kNegArena[] =
                "Assets/UI/uirml.rml\0"               // s0=0（19 字符 + NUL）
                "negbox\0"                             // s1=20
                "nrow\0"                               // s2=27
                "\001b\001\000\004nope\001\000x";      // s3=32 起：1 行 b/{nope=x}
            const ::lemon::ui::UiOpC op = {
                (uint8_t)::lemon::ui::UiOpType::SetItems, 0, 4, 0,
                0, 20, 27, 32, 1, 0, 0};
            gameUi_->ApplyOps(&op, 1, kNegArena, (uint32_t)sizeof(kNegArena));
            LEMON_LOG("uirml-smoke: 负面契约 op 直灌（field 'nope'）");
        }
        // 批③d 前置 T5：通道 B 无脚本路径——dyn.rml 未装载，Show op 落空 →
        // resolver 现载（脚本会话由 C# UiRefill 的 UI.Show 走同一通道，本钩让位）
        if (frame == 20 && gameUi_ && !ctx_.Scripts()) {
            static const char kDynArena[] = "Assets/UI/dyn.rml"; // s0=0（NUL 终止）
            const ::lemon::ui::UiOpC op = {
                (uint8_t)::lemon::ui::UiOpType::Show, 0, 1, 0,
                0, 0, 0, 0, 0, 0, 0};
            gameUi_->ApplyOps(&op, 1, kDynArena, (uint32_t)sizeof(kDynArena));
            LEMON_LOG("uirml-smoke: 通道 B 直灌 Show（dyn.rml 落空兜底）");
        }
        // 批③d 前置 T5：层序断言两段中点捕获（D1 甲-轻量：层级序 = 最近 Show 序）。
        // 170/190 渲染块录 gameRT，171/191 钩子取回数 #802040：段① B 后 Show 在上
        // → 计数 > 500；175 显式重 Show A → 段② A 在上 → 计数 < 5
        if ((frame == 171 || frame == 191) && gameUi_) {
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh)) {
                const int n = CountPixelsNear(rt, rw, rh, 128, 32, 64, 30); // #802040
                if (frame == 171) g_uirmlSmoke.smokeUiLayerBTopN = n;
                else g_uirmlSmoke.smokeUiLayerATopN = n;
            }
        }
        if (frame == 175 && gameUi_)
            gameUi_->ShowDocument("Assets/UI/uirml.rml", true); // 重 Show = 提层
        // 批③c：uiev 终值捕获（Play 中的 RtUi 槽；退 Play 后 play world 即毁，
        // 终帧 VERDICT 只能读快照）。批③d 前置：移至 199——200 起 Play→Stop→Play
        // 往返，第二局 RtUi 是新世界（无点击/无重载 → 槽恒空）
        if (frame == 199 && ctx_.Playing()) {
            const lemon::ecs::RtUiChannel& rt = ctx_.ActiveWorld().RtUi();
            for (uint32_t i = 0; i < rt.Count(); ++i)
                if (std::strcmp(rt.At(i).key, "uiev") == 0)
                    std::snprintf(smokeUiEvText_, sizeof(smokeUiEvText_), "%s",
                                  rt.At(i).text);
        }
        // 批③d 前置 T5：Play→Stop→Play 往返（§3 归位机断言）。200 Stop（快照
        // 逐字节校验）；203 重进——装载增量恰 1（只重装声明文档，dyn 装载保留）；
        // dyn（上局 C# Show 过 = stale）被 Hide；editprev（Edit 双击装载，非
        // stale）保持可见；A 回声明态 shown。断言须在 TickPlay 前（C# 本局
        // UiRefill 的 Show 尚未到达）
        if (frame == 200 && ctx_.Playing()) {
            g_uirmlSmoke.smokeUiExitP2 = StopPlay();
            if (!g_uirmlSmoke.smokeUiExitP2) LEMON_ERROR("uirml-smoke: 第二局前 ExitPlay 失败");
        }
        // 上步 Stop 失败（不该发生）时跳过第二局断言——终帧 FAIL 兜底
        if (frame == 203 && !ctx_.Playing() && g_uirmlSmoke.smokeUiExitP2) {
            const uint32_t loadsBefore =
                gameUi_ ? gameUi_->DocumentLoadCount() : 0;
            if (TryEnterPlay()) {
                tabFocusPending_ = 1;
                g_uirmlSmoke.smokeUiStaleOk =
                    gameUi_ && gameUi_->HasDocument("Assets/UI/dyn.rml") &&
                    !gameUi_->IsDocumentShown("Assets/UI/dyn.rml") &&
                    gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
                g_uirmlSmoke.smokeUiKeepCOk =
                    gameUi_ && gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
                g_uirmlSmoke.smokeUiLoadsP2 =
                    gameUi_ ? gameUi_->DocumentLoadCount() - loadsBefore : 0;
            }
        }
        // 批③d 前置 T5 补（真人验收②回灌）：删 .rml → 已装载文档逐出。212 删
        // dyn.rml 后**不直调重扫**——完全走 watcher（500ms 快照轮询 → ConsumeDirty
        // → RescanAssets removed 分支 UnloadDocument，= 真人删除路径）；252 断言
        // docs map 已无该文档。红字另证（本钩在 Play 中且场景未声明 dyn）
        if (frame == 212 && gameUi_) {
            g_uirmlSmoke.smokeUiHasDocB = gameUi_->HasDocument("Assets/UI/dyn.rml");
            gameUi_->TryGetElementText("Assets/UI/dyn.rml", "dyntitle",
                                       g_uirmlSmoke.smokeUiDynText, sizeof(g_uirmlSmoke.smokeUiDynText));
            std::error_code ecd;
            std::filesystem::remove(uiDir / "dyn.rml", ecd);
            LEMON_LOG("uirml-smoke: dyn.rml 删除播种（等 watcher 驱动逐出）");
        }
        // 真人验收②二轮·形态 A：Play 中删**正在显示**的文档（editprev 非 stale
        // 保持可见 = 用户滞留形态）→ watcher 逐出 → 253 帧活画面上面板色必须清零
        // （僵尸渲染防线——docs 簿记清不等于 RmlUi 上下文真摘除）
        if (frame == 212 && gameUi_ && ctx_.Playing()) {
            g_uirmlSmoke.smokeUiEvictSeedOk = gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
            std::error_code ecd;
            std::filesystem::remove(uiDir / "editprev.rml", ecd);
            LEMON_LOG("uirml-smoke: editprev.rml Play 中删除播种（等 watcher 逐出）");
        }
        if (frame == 251 && gameUi_) // 负面行容器快照（210 负面 op 后；三局重装即清）
            g_uirmlSmoke.smokeUiNegP2 = gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "negbox");
        if (frame == 253 && gameUi_) { // 252 帧渲染块已录 gameRT（midUirmlCapture）
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh))
                g_uirmlSmoke.smokeUiZombiePixN =
                    CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080
        }
        if (frame == 252 && gameUi_)
            g_uirmlSmoke.smokeUiDelEvictOk = !gameUi_->HasDocument("Assets/UI/dyn.rml");
        // 真人验收②二轮·形态 B：编辑态删 → 重进 Play。256 Stop；258 复种 dyn
        // （三局 C# UiRefill 依赖，否则 resolver 落空记契约错）+ 复种 editprev；
        // 302 双击通道重装载（= ③b Edit 双击形态，等 watcher 复活墓碑后）→ 304
        // 编辑态删除 → watcher 逐出 → 345 重进 Play 断言无残留（簿记 + 348 像素）；
        // 352 复种 + 394 重装载 → 终帧 hasDocC/cPrevN 回归原口径
        if (frame == 256 && ctx_.Playing()) {
            if (!StopPlay()) LEMON_ERROR("uirml-smoke: 三局前 ExitPlay 失败");
        }
        if (frame == 258) {
            {
                std::ofstream f(uiDir / "dyn.rml", std::ios::trunc);
                f << "<rml>\n<head><title>dyn</title>\n"
                     "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                     "</head>\n<body>\n<div id=\"dynpanel\">通道 B 动态屏\n"
                     "  <div id=\"dyntitle\">未装载</div>\n"
                     "</div>\n</body>\n</rml>\n";
                std::ofstream g(uiDir / "editprev.rml", std::ios::trunc);
                g << "<rml>\n<head><title>edit preview</title>\n"
                     "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                     "</head>\n<body>\n<div id=\"editpanel\">Edit 预览</div>\n"
                     "</body>\n</rml>\n";
            }
            LEMON_LOG("uirml-smoke: dyn/editprev 复种（等 watcher 复活墓碑）");
        }
        // 形态 B 种子（竞速修：原 302 单发帧。254 起重试——253 僵尸像素断言前
        // 不得装载（editprev 画面须保持清零），窗口 254..303 ≈ 50 帧 ≥ 500ms
        // watcher 轮询周期；窗尽仍是墓碑 = 原红字语义保留）
        if (frame >= 254 && frame < 304 && gameUi_ && !g_uirmlSmoke.smokeUiFormBSeeded) {
            const AssetEntry* e = ctx_.Assets().FindByPath("Assets/UI/editprev.rml");
            if (e && !e->missing) {
                LoadUiDocument(e->guid); // 双击通道重装载 + Show（形态 B 种子）
                g_uirmlSmoke.smokeUiFormBSeeded = true;
            } else if (frame == 303)
                LEMON_ERROR("uirml-smoke: 形态 B 种子失败（复种未被 watcher 处理）");
        }
        if (frame == 304) {
            std::error_code ecd;
            std::filesystem::remove(uiDir / "editprev.rml", ecd);
            // 形态 C（事件吞噬）：删除后**同帧**裸 Rescan（= ImportFile/
            // MakePrefabFrom 的"文件操作后立即重扫"形态）——立墓碑但**不经过**
            // RescanAssets 的 UI 逐出半边 → removed 事件被吃，后续 watcher 重扫
            // prev.missing 已真、cs.removed 恒空——事件驱动逐出从此失效，残留
            // 文档成僵尸。状态对账（ReconcileUiDocuments）在此形态下必须仍治愈
            ctx_.Assets().Rescan();
            LEMON_LOG(
                "uirml-smoke: editprev.rml 编辑态删除 + 同帧裸 Rescan（事件吞噬）");
        }
        if (frame == 345 && !ctx_.Playing()) {
            if (TryEnterPlay()) {
                g_uirmlSmoke.smokeUiEvictEditOk =
                    gameUi_ && !gameUi_->HasDocument("Assets/UI/editprev.rml");
                tabFocusPending_ = 1;
            } else
                LEMON_ERROR("uirml-smoke: 三局 EnterPlay 失败");
        }
        if (frame == 348 && gameUi_) { // 347 帧渲染块已录 gameRT
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh))
                g_uirmlSmoke.smokeUiEvictPixN =
                    CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080
        }
        if (frame == 352) {
            std::ofstream g(uiDir / "editprev.rml", std::ios::trunc);
            g << "<rml>\n<head><title>edit preview</title>\n"
                 "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                 "</head>\n<body>\n<div id=\"editpanel\">Edit 预览</div>\n"
                 "</body>\n</rml>\n";
            LEMON_LOG("uirml-smoke: editprev 终局复种（终帧断言复位）");
        }
        // 终局复位装载（竞速修同上：353..404 重试窗 ≈ 50 帧 ≥ 轮询周期；
        // 405 Stop 清场断言前须已装载——cPrevN/hasDocC 终帧口径）
        if (frame >= 353 && frame < 405 && gameUi_ && !g_uirmlSmoke.smokeUiFinalSeeded) {
            const AssetEntry* e = ctx_.Assets().FindByPath("Assets/UI/editprev.rml");
            if (e && !e->missing) {
                LoadUiDocument(e->guid); // 终帧 hasDocC/cPrevN 复位（Play 中装载）
                g_uirmlSmoke.smokeUiFinalSeeded = true;
            } else if (frame == 404)
                LEMON_ERROR("uirml-smoke: 终局复种未被 watcher 处理");
        }
        // 形态 D（2026-09-29 用户实报：删场景 UIDocument 实体 → 重进 Play 仍
        // 显示——Scene 来源非 stale，旧 ResetDynamicDocuments 漏放行）。405
        // Stop（StopPlay 内清场）→ 断言 uirml 即刻不显示 + editprev 豁免保持；
        // 407 编辑态删声明实体；410 第四局——无脚本模式断言装载保留/不显示/
        // 零装载增量；412 像素（411 渲染块录）。脚本模式 C# 帧 1 会经通道 B
        // 合法 Show 拉回 uirml（动态屏语义，非僵尸）——delEnt/pix 仅无脚本
        // 模式裁决。415 Stop；417 重建声明实体；420 终局 EnterPlay（终帧像素
        // 断言回归原口径：uirml 声明显示）
        if (frame == 405 && ctx_.Playing()) {
            if (StopPlay()) {
                g_uirmlSmoke.smokeUiLoadsP4Base = gameUi_ ? gameUi_->DocumentLoadCount() : 0;
                g_uirmlSmoke.smokeUiExitHideOk =
                    gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml") &&
                    !gameUi_->IsDocumentShown("Assets/UI/uirml.rml") &&
                    gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
                if (!g_uirmlSmoke.smokeUiExitHideOk)
                    LEMON_ERROR("uirml-smoke: 形态 D 清场断言失败（Stop 后 "
                                "Scene 文档应 Hide / Edit 预览应保持）");
            } else
                LEMON_ERROR("uirml-smoke: 形态 D Stop 失败");
        }
        if (frame == 407 && !ctx_.Playing()) { // 编辑态删声明实体（= 用户操作）
            bool removed = false;
            ctx_.ActiveScene().View<ecs::UIDocument>().each(
                [&](auto raw, ecs::UIDocument&) {
                    if (removed) return;
                    ctx_.DestroyEntityTree(ecs::Scene::FromEntt(raw));
                    removed = true;
                });
            if (removed)
                LEMON_LOG("uirml-smoke: 形态 D——编辑态删除 UIDocument 声明实体");
            else
                LEMON_ERROR("uirml-smoke: 形态 D 删实体失败（场景无 UIDocument）");
        }
        if (frame == 410 && !ctx_.Playing() && TryEnterPlay()) {
            tabFocusPending_ = 1;
            // 簿记断言全模式安全：本钩子先于当帧 TickPlay——脚本模式 C# 的
            // 帧 1 UiRefill（合法通道 B 拉回）发生在断言之后
            g_uirmlSmoke.smokeUiDelEntOk =
                gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml") &&
                !gameUi_->IsDocumentShown("Assets/UI/uirml.rml") &&
                gameUi_->DocumentLoadCount() == g_uirmlSmoke.smokeUiLoadsP4Base;
            if (!g_uirmlSmoke.smokeUiDelEntOk)
                LEMON_ERROR("uirml-smoke: 形态 D 第四局残留（声明移除后"
                            "须：装载保留 + 不显示 + 零装载增量）");
        }
        if (frame == 412 && gameUi_ && Launch().script.empty()) { // 411 渲染块已录
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh))
                g_uirmlSmoke.smokeUiDelEntPixN = CountPixelsNear(rt, rw, rh, 128, 32, 64, 30);
        }
        if (frame == 415 && ctx_.Playing()) {
            if (!StopPlay()) LEMON_ERROR("uirml-smoke: 形态 D 终局前 Stop 失败");
        }
        if (frame == 417 && !ctx_.Playing()) { // 重建声明实体（终局画面回归）
            const AssetEntry* en =
                ctx_.Assets().FindByPath("Assets/UI/uirml.rml");
            if (en) {
                ecs::Entity ue = ctx_.CreateEntity("UIDocument");
                ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue).sourceAssetGuid =
                    en->guid;
            } else
                LEMON_ERROR("uirml-smoke: 形态 D 重建实体失败（夹具资产缺）");
        }
        if (frame == 420 && !ctx_.Playing()) {
            if (TryEnterPlay()) tabFocusPending_ = 1;
            else LEMON_ERROR("uirml-smoke: 终局 EnterPlay 失败");
        }
        // 形态 D 第二段（防线二独立验收面）：421 Stop **直调 ctx_.ExitPlay**
        // 绕过 StopPlay 清场（= 清场被跳过/失效的窗口形态）→ uirml 保持
        // shown → 422 删声明实体 → 424 第六局：无声明 + Scene 来源 + shown——
        // ResetDynamicDocuments 的 origin 判据必须独自完成清场（防线一不在
        // 场）。426 常规 Stop；428 重建实体；430 真终局（画面回归原口径）
        if (frame == 421 && ctx_.Playing()) {
            if (ctx_.ExitPlay()) // 直调（绕过 StopPlay 清场 = 防线一失效形态）
                // seed 在场证明：Stop 后 uirml 仍 shown（清场确被绕过，防线二
                // 是本局唯一清场者——TryEnterPlay 内部 Reset 先于一切读数）
                g_uirmlSmoke.smokeUiResetSeedOk = gameUi_ &&
                                     gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
            else
                LEMON_ERROR("uirml-smoke: 形态 D 二段 Stop 失败（绕过清场）");
        }
        if (frame == 422 && !ctx_.Playing()) {
            bool removed = false;
            ctx_.ActiveScene().View<ecs::UIDocument>().each(
                [&](auto raw, ecs::UIDocument&) {
                    if (removed) return;
                    ctx_.DestroyEntityTree(ecs::Scene::FromEntt(raw));
                    removed = true;
                });
            if (!removed)
                LEMON_ERROR("uirml-smoke: 形态 D 二段删实体失败（无声明实体）");
        }
        if (frame == 424 && !ctx_.Playing() && TryEnterPlay()) {
            tabFocusPending_ = 1;
            g_uirmlSmoke.smokeUiResetOnlyOk = g_uirmlSmoke.smokeUiResetSeedOk && gameUi_ &&
                                 !gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
            if (!g_uirmlSmoke.smokeUiResetOnlyOk)
                LEMON_ERROR("uirml-smoke: 形态 D 二段失败（防线二/Reset origin "
                            "判据未清未声明文档）");
        }
        if (frame == 426 && ctx_.Playing()) {
            if (!StopPlay()) LEMON_ERROR("uirml-smoke: 形态 D 二段 Stop 失败");
        }
        if (frame == 428 && !ctx_.Playing()) {
            if (const AssetEntry* en =
                    ctx_.Assets().FindByPath("Assets/UI/uirml.rml")) {
                ecs::Entity ue = ctx_.CreateEntity("UIDocument");
                ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue).sourceAssetGuid =
                    en->guid;
            } else
                LEMON_ERROR("uirml-smoke: 形态 D 二段重建实体失败");
        }
        if (frame == 430 && !ctx_.Playing()) {
            if (TryEnterPlay()) tabFocusPending_ = 1;
            else LEMON_ERROR("uirml-smoke: 真终局 EnterPlay 失败");
        }
        if (frame == 100) {
            if (rewriteFile(uiDir / "uirml.rml", "#ffd060", "#40ff90"))
                LEMON_LOG("uirml-smoke: .rml 热重载播种（标题金→绿）");
            RescanAssets();
        }
        if (frame == 140) {
            if (rewriteFile(uiDir / "uirml.rcss", "color: #e0e0e0", "color: #80c0ff"))
                LEMON_LOG("uirml-smoke: .rcss 热重载播种（正文灰→蓝，经 ReloadStyleSheets）");
            RescanAssets();
        }
    }
}

// ---- --smoke-uirml 末帧裁决（批③b 四通道 + 批③c C# API + 批③d 双通道三局 +
// 僵尸三形态 + 形态 D 两段；printf 顺序不变，exitCode 写改返回值——与
// SmokeAnimVerdict/SmokeTplVerdict 同款等值变换；批③c-5 自 Run 外迁）----
bool EditorApp::SmokeUirmlVerdict() {
    bool uiOk = false;
    int panelN = 0, titleGN = 0, bodyBN = 0, texN = 0, titleTopN = 0;
    int oldGoldN = 0, oldGrayN = 0;
    std::vector<uint8_t> rt;
    uint32_t rw = 0, rh = 0;
    const bool fetched = device_->DebugFetchTextureCapture(rt, rw, rh);
    // gameRT 落盘：--screenshot 请求时自动出第二张（<名>-gamert.png，无编辑器
    // 铬的纯游戏画面 = 像素断言同源图）；LEMON_UIRML_DUMP 排障路径保留
    if (fetched && !Launch().screenshot.empty()) {
        namespace fs = std::filesystem;
        const fs::path sp(Launch().screenshot);
        stbi_write_png(
            (sp.parent_path() / (sp.stem().string() + "-gamert.png")).generic_string().c_str(),
            (int)rw, (int)rh, 4, rt.data(), (int)rw * 4);
    }
    if (std::getenv("LEMON_UIRML_DUMP") && fetched)
        stbi_write_png("/tmp/uirml-rt.png", (int)rw, (int)rh, 4, rt.data(), (int)rw * 4);
    const bool hasDoc = gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml");
    const bool notoFont = gameUi_ &&
                          std::strcmp(gameUi_->LoadedFontFamily(), "Noto Sans SC") == 0;
    if (fetched && hasDoc) {
        panelN = CountPixelsNear(rt, rw, rh, 32, 64, 96, 14);    // #204060 面板底
        titleGN = CountPixelsNear(rt, rw, rh, 64, 255, 144, 30); // #40ff90 热重载后标题
        // 正文蓝 tol=24：边框 #60a0ff 与正文 #80c0ff 逐通道差 32——tol 44 时边框
        // 混入计数（实测 4476 ≈ 边框周长像素，文字仍旧灰的假阳性来源）
        bodyBN = CountPixelsNear(rt, rw, rh, 128, 192, 255, 24); // #80c0ff 热重载后正文
        texN = CountPixelsNear(rt, rw, rh, 208, 64, 128, 30);    // #d04080 贴图（桥）
        oldGoldN = CountPixelsNear(rt, rw, rh, 255, 208, 96, 20); // 旧标题色残留
        oldGrayN = CountPixelsNear(rt, rw, rh, 224, 224, 224, 30); // 旧正文色残留
        // 位置断言（③a 真人目检抓纵向翻转后的机器化：标题色像素须集中上半幅）
        const uint32_t halfY = rh / 2;
        for (uint32_t y = 0; y < rh; ++y)
            for (uint32_t x = 0; x < rw; ++x) {
                const uint8_t* p = &rt[((size_t)y * rw + x) * 4];
                const int dr = (int)p[0] - 64, dg = (int)p[1] - 255, db = (int)p[2] - 144;
                if ((uint32_t)(dr * dr + dg * dg + db * db) <= 30u * 30u && y < halfY)
                    ++titleTopN;
            }
        uiOk = notoFont && panelN > 3000 && titleGN > 20 && bodyBN > 20 && texN > 500 &&
               oldGoldN < 5 && oldGrayN < 5 && titleTopN >= titleGN * 3 / 4;
    }
    // 批③d-1（B1 dp 坐标系）：ratio = gameRT 高/720 + dpbox 尺寸 = dp 值 × ratio
    //（px 定位/dp 尺寸混合元——既有 px 断言不受 ratio 影响的活证）
    const float dpRatio = gameUi_ ? gameUi_->DpRatio() : 1.0f;
    float dpW = -1.f, dpH = -1.f;
    if (gameUi_)
        gameUi_->TryGetElementBox("Assets/UI/uirml.rml", "dpbox", &dpW, &dpH);
    const bool dpRatioOk = fetched && rh > 0 &&
                           std::fabs(dpRatio - (float)rh / 720.0f) < 0.01f;
    const bool dpBoxOk = fetched &&
                         std::fabs(dpW - 100.0f * dpRatio) < 1.0f &&
                         std::fabs(dpH - 50.0f * dpRatio) < 1.0f;
    // 批③c：C# API 全链位（--script 时生效；未带脚本 = n/a 通过——③b 口径
    // 的直跑兼容）。items = 克隆行数（cards 2 + negbox 1 负面行）；text = 探针
    // SetText 后的 title（热重载两段后仍是探针值 = DocumentReloaded 重灌证据）；
    // ev = 合成点击/重灌计数回读（RtUi uiev）；contract = 契约错误恰 1（负面
    // op 直灌的一个，其余零容忍——"响亮失败"回归防线）
    const bool scripted = ctx_.Scripts() != nullptr;
    int itemsN = -1, negN = -1;
    if (gameUi_) {
        itemsN = gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "cards");
        negN = g_uirmlSmoke.smokeUiNegP2; // 251 帧快照（终帧在三局——通道 A 重装已清负面容器）
    }
    char titleText[64] = {};
    if (gameUi_)
        gameUi_->TryGetElementText("Assets/UI/uirml.rml", "title", titleText,
                                   sizeof(titleText));
    int evClicks = -1, evReloads = -1;
    std::sscanf(smokeUiEvText_, "c%dr%d", &evClicks, &evReloads);
    const uint32_t contractN = gameUi_ ? gameUi_->ContractErrorCount() : 0;
    const bool textOk = std::strcmp(titleText, "升级！三选一") == 0;
    const bool itemsOk = !scripted || (itemsN == 2 && negN == 1);
    const bool evOk = !scripted || (evClicks >= 1 && evReloads >= 2);
    const bool contractOk = !scripted || (contractN == 1 && textOk);
    const bool uiOk3c = itemsOk && evOk && contractOk;
    // 批③d 前置 T5：UIDocument 双通道/层序/stale 裁决位。
    //   uidocA/B = 通道 A/B 装载（场景声明 + Show 落空兜底各至少一条断言）；
    //   layer = D1 层序（B 后 Show 在上 → 171 段 #802040>500；175 重 Show A 后
    //   190 段 <5 = 被盖住）；stale/keepC/p2 = Play→Stop→Play：第二局动态屏被
    //   Hide（装载保留）、Edit 双击装载保持可见、装载增量恰 1（只重装声明文档）
    // hasDocB 读 220 帧存量快照（220 删除播种后终帧恒 false——逐出本身是断言）
    const bool hasDocB = g_uirmlSmoke.smokeUiHasDocB;
    const bool hasDocC = gameUi_ && gameUi_->HasDocument("Assets/UI/editprev.rml");
    const uint32_t loadsN = gameUi_ ? gameUi_->DocumentLoadCount() : 0;
    int cPrevN = 0;
    char dynText[64] = {};
    if (gameUi_ && fetched)
        cPrevN = CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080 Edit 预览
    std::snprintf(dynText, sizeof(dynText), "%s", g_uirmlSmoke.smokeUiDynText); // 220 快照（逐出后终帧读恒空）
    // scripted：C# 同批 Show+SetText 到刚兜底装载的 dyn（通道 B 顺序契约）
    const bool dynTextOk = !scripted || std::strcmp(dynText, "通道B已装载") == 0;
    // 形态 D：stopHide/delEnt 全模式（410 簿记断言先于当帧 C#）；pix 仅无脚本
    // 模式（脚本模式 C# 帧 1 经通道 B 合法 Show 拉回 = 动态屏非僵尸）；
    // reset = 二段防线二独立面（421 绕过清场 → 424 Reset 独自清场）
    const bool evict3Ok = g_uirmlSmoke.smokeUiExitHideOk && g_uirmlSmoke.smokeUiDelEntOk &&
                          g_uirmlSmoke.smokeUiResetSeedOk && g_uirmlSmoke.smokeUiResetOnlyOk &&
                          (scripted || (g_uirmlSmoke.smokeUiDelEntPixN >= 0 &&
                                        g_uirmlSmoke.smokeUiDelEntPixN < 5));
    const bool uidocOk = hasDoc && hasDocB && hasDocC && g_uirmlSmoke.smokeUiExitP2 &&
                         g_uirmlSmoke.smokeUiStaleOk && g_uirmlSmoke.smokeUiKeepCOk && g_uirmlSmoke.smokeUiLoadsP2 == 1 &&
                         g_uirmlSmoke.smokeUiLayerBTopN > 500 && g_uirmlSmoke.smokeUiLayerATopN < 5 &&
                         cPrevN > 100 && dynTextOk && g_uirmlSmoke.smokeUiDelEvictOk &&
                         g_uirmlSmoke.smokeUiEvictSeedOk && g_uirmlSmoke.smokeUiZombiePixN >= 0 &&
                         g_uirmlSmoke.smokeUiZombiePixN < 5 && g_uirmlSmoke.smokeUiEvictEditOk &&
                         g_uirmlSmoke.smokeUiEvictPixN >= 0 && g_uirmlSmoke.smokeUiEvictPixN < 5 && evict3Ok;
    std::printf("[lemon] smoke-uirml: doc=%d font=%s panel=%d(>3000) titleG=%d(>20) "
                "bodyB=%d(>20) tex=%d(>500) old=%d/%d(<5) titleTop=%d/%d(≥3/4) "
                "dp(ratio=%.3f box=%.1fx%.1f/%s) "
                "items=%d/%d ev=c%dr%d contract=%u/%s "
                "uidoc(a=%d/b=%d/c=%d loads=%u/%u) layer(bTop=%d aTop=%d) "
                "p2(stale=%d keepC=%d+%dpx dyn=%s del=%d) "
                "evict2(seed=%d live=%d edit=%d pix=%d) "
                "evict3(stopHide=%d delEnt=%d reset=%d/%d pix=%d) => %s\n",
                hasDoc ? 1 : 0, gameUi_ ? gameUi_->LoadedFontFamily() : "-",
                panelN, titleGN, bodyBN, texN, oldGoldN, oldGrayN, titleTopN, titleGN,
                dpRatio, dpW, dpH, (dpRatioOk && dpBoxOk) ? "OK" : "BAD",
                itemsN, negN, evClicks, evReloads, contractN, textOk ? "textOK" : "textBAD",
                hasDoc ? 1 : 0, hasDocB ? 1 : 0, hasDocC ? 1 : 0, loadsN, g_uirmlSmoke.smokeUiLoadsP2,
                g_uirmlSmoke.smokeUiLayerBTopN, g_uirmlSmoke.smokeUiLayerATopN,
                g_uirmlSmoke.smokeUiStaleOk ? 1 : 0, g_uirmlSmoke.smokeUiKeepCOk ? 1 : 0, cPrevN,
                dynTextOk ? "OK" : "BAD", g_uirmlSmoke.smokeUiDelEvictOk ? 1 : 0,
                g_uirmlSmoke.smokeUiEvictSeedOk ? 1 : 0, g_uirmlSmoke.smokeUiZombiePixN, g_uirmlSmoke.smokeUiEvictEditOk ? 1 : 0,
                g_uirmlSmoke.smokeUiEvictPixN, g_uirmlSmoke.smokeUiExitHideOk ? 1 : 0, g_uirmlSmoke.smokeUiDelEntOk ? 1 : 0,
                g_uirmlSmoke.smokeUiResetSeedOk ? 1 : 0, g_uirmlSmoke.smokeUiResetOnlyOk ? 1 : 0,
                g_uirmlSmoke.smokeUiDelEntPixN,
                (uiOk && uiOk3c && uidocOk && dpRatioOk && dpBoxOk) ? "OK" : "FAIL");
    return uiOk && uiOk3c && uidocOk && dpRatioOk && dpBoxOk;
}

} // namespace lemon::editor
