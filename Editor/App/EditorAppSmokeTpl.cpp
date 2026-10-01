// Lemon 编辑器 — --smoke-template 冒烟族（批③c-4 自 EditorApp.cpp Run 外迁：
// 预循环播种×2 / 事件 sink 装配 / 循环内转向注入 / 帧采样 / 末帧裁决。
// 模式同批③c-1..3：状态收敛 TplSmokeState + 批④ 单 TU 化（结构体退回本 TU
// 匿名命名空间，EditorAppSmoke.h 共享面退役）+
// 挂点原位、帧号锚定/执行时序逐位不变。EditorAppSmoke.cpp 已 1900 行超软
// 上限 → 本族独立成 TU。
// 批④ 增两读点收口：SmokeTplCapture（Run 渲染段 capReq 块挂点化）+
// SmokeTplPointerHold()（FeedGameUiInput 随 UI 桥外迁后的指针保持窗读点）。

#include "App/EditorApp.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Templates/VsTemplateGen.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Process.h" // CurrentProcessId（tempdir 唯一名；Windows 阻断项①，07 §3.6）
#include "Core/Log.h"
#include "EditorContext.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"

namespace lemon::editor {

namespace {
// ---- M5 批④ --smoke-template 证据状态（批③b 文件级 g_tpl* 标量收敛为单结构体
// 实例；批③c-4 随函数族外迁共享于 EditorAppSmoke.h；批④ 单 TU 化收口：结构体
// 退回本 TU 匿名命名空间——Run 渲染段 capReq 块收口为 SmokeTplCapture 挂点、
// FeedGameUiInput 指针保持窗经 SmokeTplPointerHold() 读，TU 外零引用。无捕获
// event sink lambda 的计数改写（SmokeTplPlaySetup 装配）仍在本 TU，文件级
// 存储刚需不变）----
struct TplSmokeState {
    int waveStarts = 0, levelUps = 0, deaths = 0;
    int gems = 0, mobs = 0; // 峰值快照（帧内采样）
    // 批③d 前置 T5 → 批③d-1/③d-2 随迁：模板场景现挂 6 UIDocument（HUD/cards/
    // main/pause/settings/results）——零装载护栏升级为"通道 A 装载恰 6"（装载点
    // 单一性防线的同型收紧；bench 场景仍零装载）
    int uiLoads = -1;
    // 批③d-1：文档化断言态（原 RtUi 行/卡片探针随迁）+ 层序三拍状态机
    bool hudDocOk = false;
    int layerStage = 0; // 0 基线请求→1 取回→2 等卡片→3 取回→4 等隐藏→5 取回→6 完
    int hudPixN0 = -1, hudPixDuring = -1, hudPixAfter = -1;
    bool capReq = false, capPending = false;
    int capPix = -1;
    int clickPhase = 0, clickCooldown = 0; // 直灌点击三帧（定位/down/up）+ 冷却
    bool pointerHold = false; // 指针保持窗（FeedGameUiInput 让位——Update 建悬停用）
    float clickX = 0, clickY = 0;
    char hudRows[64] = "";
    bool bestLoaded = false, waveRow = false; // g_tplHudOk → g_tplHudDocOk（批③d-1 随迁）
    // T8 后修：进度条断言（文本探针测不出"样式写了布局没生效"——bar-fill 曾因
    // RmlUi 默认 inline 宽高被忽略而恒 0，文本六行全绿）。后修② 条改原生 progress：
    // 断言 = 轨道盒（120dp×10dp×ratio）+ value 属性回读对文本行数值
    bool hudBarBox = false;
    // M6a 批①：受击切段链（怪 clipId 曾 = monster-hit 段）+ fx 通道（飘字/血条在场）
    bool mobHitClip = false, fxText = false, fxBar = false;
    bool cardsSeen = false, picked = false, cardsHidden = false;
    bool deathSeen = false, revived = false, scriptOk = true; // 批④后修④死亡链
    bool deathArmed = false; // 压血一shot（站桩下自动炮火清怪快于刷怪，磨不死）
    // M6a 批② T4：数值表载入断言（weapons 4 行 × upgrades 7 行 × balance 2 行——
    // 含列头行；PlayerCombat.Start 读、EnterPlay 快照建 TableStore）
    bool tablesOk = false;
    // M6c 批④：音频链机器断言——装载恰 7（CC0 七件）+ BGM 声部暂停期挂起非
    // 终止（>=1）+ 恢复后仍活（挂起续响；一次性 SFX 不在断言面——自然放完）
    int audMount = -1, audPauseVoices = -1, audResumeVoices = -1;
    // 批③d-2：流程链（菜单→开局→常规链→二死→结算→重开清场→暂停/设置→回菜单）。
    // 元素点击 = 引擎直灌三帧同卡片条目路径（clickElDoc/Id 非空 = 下一发目标）
    bool menuOk = false, runStarted = false;
    bool death2Armed = false;
    bool resultsOk = false, restartOk = false;
    bool pauseOk = false, settingsOk = false, settingsToggled = false;
    bool resumedOk = false, tomenuOk = false;
    int flowStage = 0; // 0 等菜单→1 等开局→2 常规链(一死复活)→3 等二死结算→4 等重开
                       // →5 等暂停→6 等设置→7 等翻转→8 等回暂停→9 等复活后二段暂停→10 等回菜单
    char clickElDoc[40] = ""; // 元素点击目标（doc relPath；空 = 卡片条目路径）
    char clickElId[40] = "";
    bool clickFromCards = false; // 本发点击来源（up 相位回填 picked 用——元素点击不污染卡片链语义位）
    uint64_t oldPlayerId = 0; // 重开前玩家句柄（清场断言：旧灭新生）
    int revivedAt = -1;       // 复活帧（二死武装避开复活无敌 2s）
    bool wantPause = false;   // Esc 注入请求（SmokeTplSteer 写 bit6 一帧）
};
TplSmokeState g_tplSmoke; // 批④：单 TU 化（EditorAppSmoke.h 的 extern 共享面退役）
} // namespace

// ---- --smoke-template 向导复制播种（M5 批④；批③c-4 自 Run 外迁，挂点原位）----
bool EditorApp::SmokeTplSeedProject() {
    if (!Launch().smokeTemplate) return true;
#ifndef LEMON_SCRIPT_DIR
    LEMON_ERROR("--smoke-template 需要 LEMON_BUILD_SCRIPTING=ON 构建");
    return false;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() /
                         ("lemon-smoke-template-" + std::to_string(lemon::CurrentProcessId()));
    fs::remove_all(tmp, ec);
    ProjectDesc d;
    d.parentDir = tmp.string();
    d.name = "VsSmoke";
    d.sdkDir = LEMON_SCRIPT_DIR;
    d.engineVersion = "0.5.0-m5";
    d.templateName = "vs-survivor";
    d.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
    const std::string root = ProjectWizard::Create(d);
    if (root.empty()) {
        LEMON_ERROR("smoke-template：向导复制失败（模板缺失/不可写）");
        return false;
    }
    launchCopy_.projectDir = root;
    launch_ = &launchCopy_;
    LEMON_LOG("smoke-template: 向导复制 OK %s", root.c_str());
    return true;
#endif
}

// ---- --smoke-template 场景+预置存档播种（M5 批④/M6a 批② T5；批③c-4 自 Run
// else-if 链外迁：守卫（场景开链）留原位，函数体逐位随迁）----
bool EditorApp::SmokeTplSeedScene() {
    if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return false;
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path saves = fs::path(launch_->projectDir) / ".lemon/saves";
        fs::create_directories(saves, ec);
        lemon::ecs::SaveChannel pre;
        pre.Set("vs.best", "123", 3);
        const std::vector<uint8_t> bytes = pre.Encode();
        std::ofstream(saves / "meta.sav", std::ios::binary | std::ios::trunc)
            .write((const char*)bytes.data(), (std::streamsize)bytes.size());
        std::ofstream(saves / "game.sav", std::ios::binary | std::ios::trunc)
            .write((const char*)bytes.data(), (std::streamsize)bytes.size());
    }
    smokeSeeded_ = ctx_.ActiveScene().AliveCount();
    forceDefaultLayout_ = true; // overlay 断言依赖 Scene 面板前台（同 smoke-drag 语义）
    // overlay 三要素需要选中实体：选玩家（tag "Player"）
    ctx_.EditScene().Each([&](ecs::Entity e) {
        if (const ecs::Meta* m = ctx_.EditScene().TryGet<ecs::Meta>(e);
            m && std::strcmp(m->tag, "Player") == 0)
            ctx_.Select(e, false);
    });
    return true;
}

// ---- --smoke-template Play 前置（M5 批④：uiLoads 基线 + 事件计数 sink——
// 无捕获 lambda 引 g_tplSmoke（文件级存储两刚需之一）；批③c-4 自 Run 外迁）----
void EditorApp::SmokeTplPlaySetup() {
    if (Launch().smokeTemplate && ctx_.Playing()) {
    // 批③d-2：通道 A 装载数（恰 6 = 六文档场景声明；bench 场景零装载口径
    // 不变——装载点只在 MountSceneUiDocuments 的 EnterPlay 扫描）
    g_tplSmoke.uiLoads = gameUi_ ? (int)gameUi_->DocumentLoadCount() : -1;
    ctx_.ActiveWorld().SetEventSink(
        [](ecs::World&, const ecs::EventPacket& p) {
            if (p.type == ecs::GameEvent::WaveStart) ++g_tplSmoke.waveStarts;
            else if (p.type == ecs::GameEvent::LevelUp) ++g_tplSmoke.levelUps;
            else if (p.type == ecs::GameEvent::Death) ++g_tplSmoke.deaths;
        });
    }
}

// ---- --smoke-template 转向注入（批④后修④ 两段：kDeathArm 前环绕风筝攒链、
// 此后站桩送死；批③c-4 自 Run 外迁，挂点原位（ApplyInput 前））----
void EditorApp::SmokeTplSteer(uint64_t frame, ecs::InputState& in) {
    if (Launch().smokeTemplate && !gameViewFocused_) {
        // 批③d-2：流程链 Esc 注入（bit6 pause，一帧边沿——GameFlow 侧 prevPause
        // 边沿检测消费；与转向注入同一 ApplyInput 前挂点）
        if (g_tplSmoke.wantPause) {
            in.buttons |= 1ull << 6;
            g_tplSmoke.wantPause = false;
        }
        // 模板冒烟注入（批④后修④ 两段）：kDeathArm 前切向环绕风筝（角速
        // 1.2rad/s × 速 240 → 半径 ~200）攒击杀/升级链；此后站桩送死——
        // 近身 Hazard 磨死 → 死亡对话框 → 自动 pick 复活（断言见 tplDeath 段）
        constexpr uint64_t kDeathArm = 2100;
        if (frame < kDeathArm) {
            const float a = 0.02f * (float)frame;
            in.ax = -std::sin(a);
            in.ay = std::cos(a);
        } else {
            in.ax = 0.0f; // 站桩 + 证据段压血（清怪快于刷怪，磨不死）
            in.ay = 0.0f;
        }
    }
}

// ---- --smoke-template 帧采样（M5 批④/M6a 批①②/批③d-1：HUD 文档六要素 +
// 进度条盒 + 受击切段 + fx + 卡片直灌点击三帧 + 层序三拍 + 死亡/复活链 +
// 低频诊断快照；批③c-4 自 Run 外迁，挂点原位）----
void EditorApp::SmokeTplSample(uint64_t frame) {
    if (Launch().smokeTemplate && ctx_.Playing() && frame > 5) {
        // M6c 批④：音频链采样——装载恰 7（一次）+ 暂停屏在场期声部数（逐帧
        // 覆写 = 恢复前末值）+ 暂停解除后首帧声部数（挂起续响：BGM 不死）
        if (g_tplSmoke.audMount < 0)
            g_tplSmoke.audMount = (int)audioClips_.size();
        static const char* const kPauseDocTpl = "Assets/UI/pause.rml";
        if (gameUi_ && gameUi_->IsDocumentShown(kPauseDocTpl)) {
            g_tplSmoke.audPauseVoices = audio_.ActiveVoiceCount();
        } else if (g_tplSmoke.audPauseVoices >= 0 && g_tplSmoke.audResumeVoices < 0) {
            g_tplSmoke.audResumeVoices = audio_.ActiveVoiceCount();
        }
        // M6a 批② T4：三表快照断言（EnterPlay 建、Start 消费——行数 = 列头+数据行）
        if (!g_tplSmoke.tablesOk) {
            auto rowsOf = [&](uint64_t guid) {
                const auto* t = ctx_.ActiveWorld().Tables().Find((uint32_t)guid);
                return t ? (int)t->size() : -1;
            };
            g_tplSmoke.tablesOk = rowsOf(vs_template::kWeaponsTab) == 4 &&
                            rowsOf(vs_template::kUpgradesTab) == 7 &&
                            rowsOf(vs_template::kBalanceTab) == 2;
        }
        // 批③d-1：HUD 文档化断言（原 RtUi 行探针随迁）——六要素文本经
        // TryGetElementText（DOM 读数，零渲染依赖）；best 含 "123" = 预置存档
        // → C# 读回 → HUD 回显
        static const char* const kHudDoc = "Assets/UI/hud.rml";
        if (gameUi_) {
            char hudTxt[96] = {};
            auto hudHas = [&](const char* id) {
                return gameUi_->TryGetElementText(kHudDoc, id, hudTxt,
                                                  sizeof hudTxt) &&
                       hudTxt[0] != '\0';
            };
            if (!g_tplSmoke.hudDocOk) {
                char t4[4][96] = {};
                bool ok = true;
                const char* const ids[4] = {"hp-text", "xp-text", "time", "kills"};
                for (int i = 0; i < 4; ++i)
                    ok = ok && gameUi_->TryGetElementText(kHudDoc, ids[i], t4[i],
                                                          sizeof t4[i]) &&
                         t4[i][0] != '\0';
                g_tplSmoke.hudDocOk = ok;
            }
            if (!g_tplSmoke.bestLoaded && hudHas("best") && std::strstr(hudTxt, "123"))
                g_tplSmoke.bestLoaded = true;
            if (!g_tplSmoke.waveRow && hudHas("wave")) g_tplSmoke.waveRow = true;
            // T8 后修②：条改原生 <progress>（fill = 引擎定位非 DOM 子元素，
            // 盒探针不可达）——断言换轨双证：① 轨道盒 = 120dp×10dp×ratio
            // （布局在场；display/规则丢失 → 0 尺寸）② value 属性回读 =
            // 文本行同帧数值（C# SetAttr 接线落地；缺属性 = 探针 false）
            if (!g_tplSmoke.hudBarBox) {
                char hpT[96] = {}, xpT[96] = {};
                int hc = -1, hm = -1, xc = -1, xm = -1;
                if (gameUi_->TryGetElementText(kHudDoc, "hp-text", hpT, sizeof hpT))
                    std::sscanf(hpT, "HP %d/%d", &hc, &hm);
                if (gameUi_->TryGetElementText(kHudDoc, "xp-text", xpT, sizeof xpT))
                    std::sscanf(xpT, "LV %*d %d/%d", &xc, &xm);
                const float ratio = gameUi_->DpRatio();
                auto barOk = [&](const char* id, int cur, int max) {
                    float w = 0, h = 0, v = -1.f;
                    if (cur <= 0 || max <= 0 || ratio <= 0.f) return false;
                    if (!gameUi_->TryGetElementBox(kHudDoc, id, &w, &h)) return false;
                    if (std::fabs(w - 120.f * ratio) > 3.f ||
                        std::fabs(h - 10.f * ratio) > 2.f)
                        return false;
                    if (!gameUi_->TryGetElementAttrF(kHudDoc, id, "value", &v))
                        return false;
                    return std::fabs(v - (float)cur) <= 0.51f;
                };
                if (barOk("hp-bar", hc, hm) && barOk("xp-bar", xc, xm))
                    g_tplSmoke.hudBarBox = true;
            }
        }
        // M6a 批①：受击切段 + fx 通道采样（PlayerCombat.OnHit 写——脚本面端到端）
        if (!g_tplSmoke.mobHitClip) {
            ctx_.ActiveScene().View<ecs::Animator2D>().each([&](auto, ecs::Animator2D& a) {
                if (a.clipId == (uint32_t)vs_template::kMonsterHitClip)
                    g_tplSmoke.mobHitClip = true;
            });
        }
        if (!g_tplSmoke.fxText || !g_tplSmoke.fxBar) {
            const lemon::ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
            if (fx.TextCount() > 0) g_tplSmoke.fxText = true;
            if (fx.BarCount() > 0) g_tplSmoke.fxBar = true;
        }
        // 批③d-1：卡片链文档化（原 RtUiCards 探针随迁）。升级卡 3 条 / 死亡对话
        // 框 1 条（key "ok"）；选择 = **引擎直灌点击**（SetPointer + 两帧
        // down/up + 指针保持窗——绕 ImGui 悬停链：template 会话里 GameView
        // IsItemHovered 被压制（mouse 落位正确仍恒假，根因未明），ImGui→RmlUi
        // 路由链由 smoke-uirml 的 60/61 帧点击独立覆盖）：UiEvent → #16 →
        // C# 消费 → Hide。三帧状态机：定位（下一帧 Update 建悬停）→ down → up
        static const char* const kCardsDoc = "Assets/UI/cards.rml";
        const int cardsN =
            gameUi_ ? gameUi_->ContainerItemCount(kCardsDoc, "cards") : -1;
        const bool cardsShown =
            gameUi_ && gameUi_->IsDocumentShown(kCardsDoc);
        if (cardsN == 3) g_tplSmoke.cardsSeen = true;
        if (g_tplSmoke.clickCooldown > 0) --g_tplSmoke.clickCooldown;
        if (g_tplSmoke.clickPhase == 1) { // down（本帧 Update 已按保持指针建好悬停）
            gameUi_->ProcessMouseButton(0, true);
            g_tplSmoke.clickPhase = 2;
        } else if (g_tplSmoke.clickPhase == 2) { // up（RmlUi click 边沿）
            gameUi_->ProcessMouseButton(0, false);
            g_tplSmoke.clickPhase = 0;
            g_tplSmoke.pointerHold = false;
            g_tplSmoke.clickCooldown = 15; // 事件往返（→UiEvent→#16→C#→Hide）余量
            if (g_tplSmoke.clickFromCards) g_tplSmoke.picked = true; // 卡片链语义位
            g_tplSmoke.clickFromCards = false;
        } else if (g_tplSmoke.clickElDoc[0] != '\0' && gameUi_ &&
                   g_tplSmoke.clickCooldown == 0) {
            // 批③d-2：元素点击（流程四屏按钮）——盒中心直灌（定位→down→up
            // 三帧与卡片条目同机器；盒未就绪则保持目标下帧重试）
            float w = 0, h = 0, x = 0, y = 0;
            if (gameUi_->TryGetElementBox(g_tplSmoke.clickElDoc, g_tplSmoke.clickElId,
                                          &w, &h, &x, &y) &&
                w > 1.f && h > 1.f) {
                g_tplSmoke.clickX = x + w * 0.5f;
                g_tplSmoke.clickY = y + h * 0.5f;
                gameUi_->SetPointer((int)g_tplSmoke.clickX, (int)g_tplSmoke.clickY, true);
                g_tplSmoke.pointerHold = true;
                g_tplSmoke.clickPhase = 1;
                g_tplSmoke.clickElDoc[0] = '\0'; // 目标消费（成功起灌）
            }
        } else if (gameUi_ && cardsShown && cardsN >= 1 &&
                   g_tplSmoke.clickCooldown == 0 && frame > 120) {
            // 可见卡片逐 key 试探（u0..u5 = 升级池 id；ok = 死亡对话框）——
            // TryGetItemCenter 只对在场条目返回中心
            static const char* const kCardKeys[7] = {"u0", "u1", "u2", "u3",
                                                     "u4", "u5", "ok"};
            for (const char* key : kCardKeys) {
                float cx, cy;
                if (gameUi_->TryGetItemCenter(kCardsDoc, "cards", key, &cx, &cy)) {
                    g_tplSmoke.clickX = cx;
                    g_tplSmoke.clickY = cy;
                    gameUi_->SetPointer((int)cx, (int)cy, true); // 保持至 up 帧
                    g_tplSmoke.pointerHold = true;
                    g_tplSmoke.clickPhase = 1;
                    g_tplSmoke.clickFromCards = true;
                    break;
                }
            }
        }
        if (g_tplSmoke.picked && !cardsShown) g_tplSmoke.cardsHidden = true; // C# 消费 → Hide
        // 批③d-1：层序三拍（cards 文档的 scrim 压暗 HUD 文字 = cards 在上的像素
        // 级证明——若层序颠倒 scrim 盖不住 HUD）。计数 #f0f0f0 近色（time 行白字）。
        // 帧序：本钩置请求位 → 次帧渲染块录 gameRT → 同帧尾本钩取回
        if (g_tplSmoke.capPending) {
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh)) {
                // HUD 文字区（ratio 派生——画布尺寸跨会话可变，px 写死不健壮）：
                // 左上 (12dp,10dp) 起 ~230dp × ~110dp（六行纵列，卡片面板居中
                // 不入区——只有 scrim 会盖进来）。计 #f0f0f0 近色（time 行白字）
                const float ratio = gameUi_ ? gameUi_->DpRatio() : 1.0f;
                const uint32_t x1 = (uint32_t)(242.0f * ratio);
                const uint32_t y1 = (uint32_t)(120.0f * ratio);
                int n = 0;
                for (uint32_t y = 0; y < rh && y < y1; ++y)
                    for (uint32_t x = 0; x < rw && x < x1; ++x) {
                        const uint8_t* p = &rt[((size_t)y * rw + x) * 4];
                        if (std::abs((int)p[0] - 240) <= 30 &&
                            std::abs((int)p[1] - 240) <= 30 &&
                            std::abs((int)p[2] - 240) <= 30)
                            ++n;
                    }
                g_tplSmoke.capPix = n;
            }
            g_tplSmoke.capPending = false;
        }
        if (g_tplSmoke.layerStage == 0 && frame > 100 && g_tplSmoke.runStarted) {
            // 基线：已开局（批③d-2 菜单期实底覆盖 HUD——N0 必须等 run 起来）、
            // 卡片未到
            g_tplSmoke.capReq = true;
            g_tplSmoke.layerStage = 1;
        } else if (g_tplSmoke.layerStage == 1 && g_tplSmoke.capPix >= 0) {
            g_tplSmoke.hudPixN0 = g_tplSmoke.capPix;
            g_tplSmoke.capPix = -1;
            g_tplSmoke.layerStage = 2;
        } else if (g_tplSmoke.layerStage == 2 && cardsShown) {
            g_tplSmoke.capReq = true;
            g_tplSmoke.layerStage = 3;
        } else if (g_tplSmoke.layerStage == 3 && g_tplSmoke.capPix >= 0) {
            g_tplSmoke.hudPixDuring = g_tplSmoke.capPix;
            g_tplSmoke.capPix = -1;
            g_tplSmoke.layerStage = 4;
        } else if (g_tplSmoke.layerStage == 4 && g_tplSmoke.picked && !cardsShown) {
            g_tplSmoke.capReq = true;
            g_tplSmoke.layerStage = 5;
        } else if (g_tplSmoke.layerStage == 5 && g_tplSmoke.capPix >= 0) {
            g_tplSmoke.hudPixAfter = g_tplSmoke.capPix;
            g_tplSmoke.capPix = -1;
            g_tplSmoke.layerStage = 6;
        }
        // 批③d-1 催命：文档化卡片的选择有几帧事件往返（原 RtUi 直写同帧），
        // 局内时序整体后移 → 波 2 刷新走近的余量变薄（实测一轮贴边一轮超时）。
        // 武装后 50 帧仍未死 = 把追击怪贴脸（保 Hazard 接触真实路径，只省走路）
        if (frame >= 2150 && !g_tplSmoke.deathSeen) {
            Vec2 ppos{0, 0};
            bool got = false;
            // 批③d-2 注：Flow 实体也挂 ScriptBox（无 Health）——传送点按 Health
            // 过滤锁玩家（否则 ppos 漂到 Flow 的原点，催命失效）
            ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                [&](auto ent, scripting::ScriptBox&) {
                    const ecs::Entity e = ecs::Scene::FromEntt(ent);
                    if (!ctx_.ActiveScene().TryGet<ecs::Health>(e)) return;
                    if (const ecs::Transform2D* t =
                            ctx_.ActiveScene().TryGet<ecs::Transform2D>(e)) {
                        ppos = t->pos;
                        got = true;
                    }
                });
            if (got)
                ctx_.ActiveScene().View<ecs::Transform2D, ecs::Chase>().each(
                    [&](auto, ecs::Transform2D& tf, ecs::Chase&) {
                        tf.pos = Vec2{ppos.x + 18.0f, ppos.y + 6.0f};
                    });
        }
        // 批④后修④死亡链回归：kDeathArm 帧起压血到 0.1 + 掐射击（站桩下
        // 自动炮火半路清怪、玩家碰不到怪——停火让怪群近身，Hazard 真路径击杀）
        // → 玩家脚本实体仍在场（View 命中 = 未被销毁）、flags bit0 未置（未被
        // 异常禁用）→ 点击复活 → 血回满 + 解冻 + 卡片文档隐藏 = 复活成功
        if (frame >= 2100 && !g_tplSmoke.deathArmed) {
            g_tplSmoke.deathArmed = true;
            ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                [&](auto ent, scripting::ScriptBox&) {
                    ecs::Entity e = ecs::Scene::FromEntt(ent);
                    if (ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(e))
                        hp->cur = 0.1f;
                    if (ecs::Shooter* sh = ctx_.ActiveScene().TryGet<ecs::Shooter>(e))
                        sh->interval = 3600.0f;
                });
        }
        if (frame >= 2100 && !g_tplSmoke.revived) {
            bool anyScript = false;
            ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                [&](auto ent, scripting::ScriptBox& sb) {
                    anyScript = true;
                    for (uint32_t i = 0; i < sb.count; ++i) // 逐槽查禁用位（M6a 批⓪）
                        if (sb.slots[i].flags & scripting::kScriptFlagDisabled)
                            g_tplSmoke.scriptOk = false;
                    const ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(
                        ecs::Scene::FromEntt(ent));
                    if (!hp) return;
                    if (hp->cur <= 0.0f) g_tplSmoke.deathSeen = true;
                    else if (g_tplSmoke.deathSeen && hp->cur >= hp->max &&
                             ctx_.ActiveWorld().TimeScale() > 0.0f && !cardsShown)
                        g_tplSmoke.revived = true;
                });
            if (!anyScript) g_tplSmoke.scriptOk = false; // 脚本实体消失（销毁回归锚点）
        }

        // ---- 批③d-2：流程链驱动（帧锚定见 TplSmokeState.flowStage 注）----
        // 玩家身份 = ScriptBox && Health（Flow 实体有 ScriptBox 无 Health——
        // 常驻件与 run 实体的判据）；元素点击复用直灌三帧机器（clickEl 目标）
        static const char* const kMainDoc = "Assets/UI/main.rml";
        static const char* const kPauseDoc = "Assets/UI/pause.rml";
        static const char* const kSettingsDoc = "Assets/UI/settings.rml";
        static const char* const kResultsDoc = "Assets/UI/results.rml";
        static const char* const kHudDoc2 = "Assets/UI/hud.rml";
        const bool menuShown = gameUi_ && gameUi_->IsDocumentShown(kMainDoc);
        auto playerAlive = [&](uint64_t id) {
            bool alive = false;
            ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                [&](auto ent, scripting::ScriptBox&) {
                    const ecs::Entity e = ecs::Scene::FromEntt(ent);
                    if (id != 0 && e.id != id) return;
                    if (ctx_.ActiveScene().TryGet<ecs::Health>(e)) alive = true;
                });
            return alive;
        };
        auto requestClick = [&](const char* doc, const char* el) {
            std::snprintf(g_tplSmoke.clickElDoc, sizeof g_tplSmoke.clickElDoc, "%s",
                          doc);
            std::snprintf(g_tplSmoke.clickElId, sizeof g_tplSmoke.clickElId, "%s",
                          el);
        };
        switch (g_tplSmoke.flowStage) {
        case 0: // 菜单即显（EnterPlay showOnStart=1）→ 点「开始游戏」
            if (menuShown) {
                g_tplSmoke.menuOk = true;
                if (g_tplSmoke.clickCooldown == 0 && frame > 60) {
                    requestClick(kMainDoc, "btn-start");
                    g_tplSmoke.flowStage = 1;
                }
            }
            break;
        case 1: // 开局握手（清场→重挂双 prefab）→ 菜单隐藏 + 玩家在场
            if (!menuShown && playerAlive(0)) {
                g_tplSmoke.runStarted = true;
                g_tplSmoke.flowStage = 2;
            }
            break;
        case 2: // 常规链推进（既有块）；复活过且过 2s 无敌窗 → 武装二死（压血+停火）
            if (g_tplSmoke.revived && g_tplSmoke.revivedAt < 0)
                g_tplSmoke.revivedAt = (int)frame;
            if (g_tplSmoke.revivedAt >= 0 && !g_tplSmoke.death2Armed &&
                frame >= (uint64_t)(g_tplSmoke.revivedAt + 140)) {
                g_tplSmoke.death2Armed = true; // 同 2100 段语义（站桩清怪快——停火近身）
                ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                    [&](auto ent, scripting::ScriptBox&) {
                        ecs::Entity e = ecs::Scene::FromEntt(ent);
                        if (ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(e))
                            hp->cur = 0.1f;
                        if (ecs::Shooter* sh = ctx_.ActiveScene().TryGet<ecs::Shooter>(e))
                            sh->interval = 3600.0f;
                    });
                g_tplSmoke.flowStage = 3;
            }
            break;
        case 3: // 二死 → 结算屏（得分行非空）→ 记旧句柄 → 点「再战一局」
            if (gameUi_ && gameUi_->IsDocumentShown(kResultsDoc)) {
                char t[64] = {};
                if (gameUi_->TryGetElementText(kResultsDoc, "res-score", t,
                                                sizeof t) &&
                    t[0] != '\0') {
                    g_tplSmoke.resultsOk = true;
                    ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                        [&](auto ent, scripting::ScriptBox&) {
                            const ecs::Entity e = ecs::Scene::FromEntt(ent);
                            if (g_tplSmoke.oldPlayerId == 0 &&
                                ctx_.ActiveScene().TryGet<ecs::Health>(e))
                                g_tplSmoke.oldPlayerId = e.id;
                        });
                    requestClick(kResultsDoc, "btn-restart");
                    g_tplSmoke.flowStage = 4;
                }
            }
            break;
        case 4: { // 重开完成：旧句柄失效 + 新玩家在场 + 击杀归零 + 结算屏已隐
            //（T10 后修①：真人验收抓的盲区——restart 位此前漏「结算屏隐藏」断言，
            // EnterRun 只隐菜单不隐结算 = 残屏盖新局而 smoke 绿）
            char t[64] = {};
            const bool killsReset =
                gameUi_ && gameUi_->TryGetElementText(kHudDoc2, "kills", t,
                                                      sizeof t) &&
                std::strcmp(t, "\xe5\x87\xbb\xe6\x9d\x80 0") == 0; // "击杀 0"
            if (g_tplSmoke.oldPlayerId != 0 && !playerAlive(g_tplSmoke.oldPlayerId) &&
                playerAlive(0) && killsReset &&
                !gameUi_->IsDocumentShown(kResultsDoc)) {
                g_tplSmoke.restartOk = true;
                g_tplSmoke.wantPause = true;
                g_tplSmoke.flowStage = 5;
            }
            break;
        }
        case 5: // Esc（bit6 注入）→ 暂停屏 → 点「设置」
            if (gameUi_ && gameUi_->IsDocumentShown(kPauseDoc)) {
                g_tplSmoke.pauseOk = true;
                requestClick(kPauseDoc, "btn-psettings");
                g_tplSmoke.flowStage = 6;
            }
            break;
        case 6: // 设置屏 → 点 fxtext 开关
            if (gameUi_ && gameUi_->IsDocumentShown(kSettingsDoc)) {
                g_tplSmoke.settingsOk = true;
                requestClick(kSettingsDoc, "btn-fxtext");
                g_tplSmoke.flowStage = 7;
            }
            break;
        case 7: { // 文案 开→关（Settings 档落盘在 Stop 后断言）→ 点「返回」
            char t[64] = {};
            if (gameUi_ && gameUi_->TryGetElementText(kSettingsDoc, "btn-fxtext",
                                                      t, sizeof t) &&
                std::strcmp(t, "\xe5\x85\xb3") == 0) { // "关"
                g_tplSmoke.settingsToggled = true;
                requestClick(kSettingsDoc, "btn-back");
                g_tplSmoke.flowStage = 8;
            }
            break;
        }
        case 8: // 回暂停（设置隐藏）→ 点「继续」
            if (gameUi_ && gameUi_->IsDocumentShown(kPauseDoc) &&
                !gameUi_->IsDocumentShown(kSettingsDoc)) {
                requestClick(kPauseDoc, "btn-resume");
                g_tplSmoke.flowStage = 9;
            }
            break;
        case 9: // 恢复运行（解冻 + 暂停隐藏）→ 再注入 Esc
            if (gameUi_ && !gameUi_->IsDocumentShown(kPauseDoc) &&
                ctx_.ActiveWorld().TimeScale() > 0.f) {
                g_tplSmoke.resumedOk = true;
                g_tplSmoke.wantPause = true;
                g_tplSmoke.flowStage = 10;
            }
            break;
        case 10: // 二段暂停 → 点「回主菜单」
            if (gameUi_ && gameUi_->IsDocumentShown(kPauseDoc)) {
                requestClick(kPauseDoc, "btn-tomenu");
                g_tplSmoke.flowStage = 11;
            }
            break;
        case 11: // 菜单回归 + run 实体清场（玩家不在场）
            if (menuShown && !playerAlive(0)) {
                g_tplSmoke.tomenuOk = true;
                g_tplSmoke.flowStage = 12;
            }
            break;
        }
        // 二死催命（同 2150 段：追击怪贴脸保 Hazard 真实路径——按 Health 锁玩家）
        if (g_tplSmoke.death2Armed && !g_tplSmoke.resultsOk &&
            g_tplSmoke.revivedAt >= 0 &&
            frame >= (uint64_t)(g_tplSmoke.revivedAt + 200)) {
            Vec2 ppos{0, 0};
            bool got = false;
            ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                [&](auto ent, scripting::ScriptBox&) {
                    const ecs::Entity e = ecs::Scene::FromEntt(ent);
                    if (!ctx_.ActiveScene().TryGet<ecs::Health>(e)) return;
                    if (const ecs::Transform2D* tf =
                            ctx_.ActiveScene().TryGet<ecs::Transform2D>(e)) {
                        ppos = tf->pos;
                        got = true;
                    }
                });
            if (got)
                ctx_.ActiveScene().View<ecs::Transform2D, ecs::Chase>().each(
                    [&](auto, ecs::Transform2D& tf, ecs::Chase&) {
                        tf.pos = Vec2{ppos.x + 18.0f, ppos.y + 6.0f};
                    });
        }
        if (frame % 60 == 0) { // 诊断快照（低频）：文档 HUD 位 + 场内分布
            std::snprintf(g_tplSmoke.hudRows, sizeof g_tplSmoke.hudRows,
                          "doc=%d cards=%d stage=%d menu=%d ph=%d el=%s",
                          g_tplSmoke.hudDocOk ? 1 : 0, cardsN, g_tplSmoke.flowStage,
                          menuShown ? 1 : 0, g_tplSmoke.clickPhase,
                          g_tplSmoke.clickElDoc);
            ctx_.ActiveScene().View<ecs::Collectible>().each(
                [](auto, ecs::Collectible&) { ++g_tplSmoke.gems; });
            ctx_.ActiveScene().View<ecs::Chase>().each(
                [](auto, ecs::Chase&) { ++g_tplSmoke.mobs; });
        }
    }
}

// ---- --smoke-template 末帧裁决（M5 批④：tplOk + 存档落盘 + 第二项目
// spriteId 记账三段聚合；printf 顺序不变，exitCode 写改返回值——与
// SmokeAnimVerdict 同款等值变换；批③c-4 自 Run 外迁）----
bool EditorApp::SmokeTplVerdict() {
        const bool layerOk = g_tplSmoke.hudPixN0 > 40 &&
                             g_tplSmoke.hudPixDuring < g_tplSmoke.hudPixN0 / 2 &&
                             g_tplSmoke.hudPixAfter > g_tplSmoke.hudPixN0 / 2;
        // 批③d-2 流程位：菜单→开局→二死结算→重开清场（旧灭新生+击杀归零）→
        // 暂停→设置翻转→恢复→二段暂停→回菜单清场
        const bool flowOk = g_tplSmoke.menuOk && g_tplSmoke.runStarted &&
                            g_tplSmoke.resultsOk && g_tplSmoke.restartOk &&
                            g_tplSmoke.pauseOk && g_tplSmoke.settingsOk &&
                            g_tplSmoke.settingsToggled && g_tplSmoke.resumedOk &&
                            g_tplSmoke.tomenuOk;
        const bool tplOk = g_tplSmoke.hudDocOk && g_tplSmoke.hudBarBox && g_tplSmoke.bestLoaded &&
                           g_tplSmoke.waveRow &&
                           g_tplSmoke.deaths > 0 && g_tplSmoke.levelUps > 0 && g_tplSmoke.cardsSeen &&
                           g_tplSmoke.picked && g_tplSmoke.cardsHidden && g_tplSmoke.deathSeen &&
                           g_tplSmoke.revived && g_tplSmoke.scriptOk &&
                           g_tplSmoke.mobHitClip && g_tplSmoke.fxText && g_tplSmoke.fxBar && // 批①
                           g_tplSmoke.tablesOk && // 批② T4：数值表载入
                           g_tplSmoke.uiLoads == 6 && layerOk && flowOk && // 批③d-1/③d-2：装载恰 6 + 层序三拍 + 流程链
                           g_tplSmoke.audMount == 7 && g_tplSmoke.audPauseVoices >= 1 &&
                           g_tplSmoke.audResumeVoices >= 1; // 批④：七件装载 + 暂停挂起续响
        std::printf("[lemon] smoke-template: hud(doc=%s bar=%s) saveLoad=%s wave(row=%s n=%d) "
                    "kills=%d levelUps=%d cards(doc seen=%s pick=%s hidden=%s) "
                    "layer(%d/%d/%d=%s) "
                    "death(seen=%s revive=%s scriptOk=%s) "
                    "hitClip=%s fx(text=%s bar=%s) tables=%s uidoc=%d "
                    "aud(mount=%d pause=%d resume=%d=%s) "
                    "flow(menu=%s start=%s results=%s restart=%s pause=%s "
                    "set=%s/%s resume=%s tomenu=%s) => %s\n",
                    g_tplSmoke.hudDocOk ? "YES" : "NO",
                    g_tplSmoke.hudBarBox ? "YES" : "NO", g_tplSmoke.bestLoaded ? "YES" : "NO",
                    g_tplSmoke.waveRow ? "YES" : "NO", g_tplSmoke.waveStarts, g_tplSmoke.deaths,
                    g_tplSmoke.levelUps, g_tplSmoke.cardsSeen ? "YES" : "NO",
                    g_tplSmoke.picked ? "YES" : "NO", g_tplSmoke.cardsHidden ? "YES" : "NO",
                    g_tplSmoke.hudPixN0, g_tplSmoke.hudPixDuring, g_tplSmoke.hudPixAfter,
                    layerOk ? "OK" : "FAIL",
                    g_tplSmoke.deathSeen ? "YES" : "NO", g_tplSmoke.revived ? "YES" : "NO",
                    g_tplSmoke.scriptOk ? "YES" : "NO", g_tplSmoke.mobHitClip ? "YES" : "NO",
                    g_tplSmoke.fxText ? "YES" : "NO", g_tplSmoke.fxBar ? "YES" : "NO",
                    g_tplSmoke.tablesOk ? "YES" : "NO", g_tplSmoke.uiLoads,
                    g_tplSmoke.audMount, g_tplSmoke.audPauseVoices, g_tplSmoke.audResumeVoices,
                    (g_tplSmoke.audMount == 7 && g_tplSmoke.audPauseVoices >= 1 &&
                     g_tplSmoke.audResumeVoices >= 1) ? "OK" : "FAIL",
                    g_tplSmoke.menuOk ? "YES" : "NO", g_tplSmoke.runStarted ? "YES" : "NO",
                    g_tplSmoke.resultsOk ? "YES" : "NO", g_tplSmoke.restartOk ? "YES" : "NO",
                    g_tplSmoke.pauseOk ? "YES" : "NO", g_tplSmoke.settingsOk ? "YES" : "NO",
                    g_tplSmoke.settingsToggled ? "YES" : "NO",
                    g_tplSmoke.resumedOk ? "YES" : "NO", g_tplSmoke.tomenuOk ? "YES" : "NO",
                    tplOk ? "OK" : "FAIL");
        std::printf("[lemon] smoke-template: diag %s gems(peak)=%d mobs(peak)=%d\n",
                    g_tplSmoke.hudRows, g_tplSmoke.gems, g_tplSmoke.mobs);
        bool verdictOk = tplOk;
        // ExitPlay 兜底落盘（写路径）：Stop 后 .lemon/saves/ 三档——slot_0 =
        // 旧 game.sav 惰性迁移后落新名（迁移链闭环：内容含种子键）；meta =
        // vs.best（模板 Chan.Meta）；批③d-2 起 settings 恒存在（GameFlow.Start
        // 建档 version=1，流程链把 fx.text 关到 0——档内容双断言）
        {
            namespace fs = std::filesystem;
            const fs::path savesDir =
                fs::path(ctx_.Assets().ProjectRoot()) / ".lemon/saves";
            std::error_code ec;
            auto fileOk = [&](const char* name) {
                return fs::file_size(savesDir / name, ec) > 16 && !ec;
            };
            const bool slotOk = fileOk("slot_0.sav"), metaOk = fileOk("meta.sav");
            bool migrateOk = false; // slot_0.sav 解码含种子键 vs.best=123
            if (slotOk) {
                std::ifstream f(savesDir / "slot_0.sav", std::ios::binary);
                std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
                lemon::ecs::SaveChannel ch;
                char buf[8] = {};
                migrateOk = ch.Decode(b.data(), b.size()) &&
                            ch.GetLen("vs.best") == 3 && ch.Get("vs.best", buf, 7) == 3 &&
                            std::memcmp(buf, "123", 3) == 0;
            }
            bool settingsOk2 = false; // settings.sav：version=1 + fx.text=0（流程链翻转）
            if (fileOk("settings.sav")) {
                std::ifstream f(savesDir / "settings.sav", std::ios::binary);
                std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
                lemon::ecs::SaveChannel ch;
                char buf[8] = {};
                settingsOk2 = ch.Decode(b.data(), b.size()) &&
                              ch.GetLen("version") == 1 &&
                              ch.Get("version", buf, 7) == 1 && buf[0] == '1' &&
                              ch.GetLen("fx.text") == 1 &&
                              ch.Get("fx.text", buf, 7) == 1 && buf[0] == '0';
            }
            const bool savOk = slotOk && metaOk && migrateOk && settingsOk2;
            std::printf("[lemon] smoke-template: saves(slot_0=%s meta=%s legacy=%s "
                        "settings=%s) => %s\n",
                        slotOk ? "YES" : "NO", metaOk ? "YES" : "NO",
                        migrateOk ? "YES" : "NO", settingsOk2 ? "YES" : "NO",
                        savOk ? "OK" : "FAIL");
            verdictOk = verdictOk && savOk;
        }
        // 批④后修②回归防线：同进程再开第二个模板拷贝 → spriteId 记账必须与
        // 第一个逐项一致（换项目注册表复位）。修复前第二个项目整体后移上个
        // 项目的精灵数 → 场景烘焙引用悬空、玩家/怪物全不渲染（demo/svr-test
        // 实测 +31；自动重开上次项目后走新建向导 = 稳定触发路径）。
#ifdef LEMON_SCRIPT_DIR
        {
            auto SnapshotIds = [](const AssetDatabase& db) {
                std::vector<std::pair<std::string, std::string>> m;
                for (const AssetEntry& e : db.Entries())
                    if (e.type == AssetType::Sprite && !e.missing)
                        m.emplace_back(e.relPath,
                                       std::to_string(e.spriteId) + "/" +
                                           std::to_string(e.sliceBase) + "+" +
                                           std::to_string(e.sliceCount));
                std::sort(m.begin(), m.end());
                return m;
            };
            const auto ids1 = SnapshotIds(ctx_.Assets());
            namespace fs = std::filesystem;
            const fs::path tmp2 =
                fs::temp_directory_path() /
                ("lemon-smoke-template2-" + std::to_string(lemon::CurrentProcessId()));
            std::error_code ec2;
            fs::remove_all(tmp2, ec2);
            ProjectDesc d2;
            d2.parentDir = tmp2.string();
            d2.name = "VsSmoke2";
            d2.sdkDir = LEMON_SCRIPT_DIR;
            d2.engineVersion = "0.5.0-m5";
            d2.templateName = "vs-survivor";
            d2.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
            bool idOk = false;
            if (const std::string root2 = ProjectWizard::Create(d2);
                !root2.empty() && OpenProjectPipeline(root2)) {
                idOk = SnapshotIds(ctx_.Assets()) == ids1;
            }
            std::printf("[lemon] smoke-template: second-project ids %s => %s\n",
                        idOk ? "identical" : "DRIFTED", idOk ? "OK" : "FAIL");
            verdictOk = verdictOk && idOk;
        }
#endif
    return verdictOk;
}

// ---- --smoke-template 层序三拍捕获（批③d-1：动态时点——SmokeTplSample 证据
// 块状态机置请求位）；批④ 自 Run 渲染段外迁：挂点原位、帧序与时序逐位不变
//（证据块（渲染后）置位 → 次帧本挂点录 gameRT → 同帧尾证据块取回数色）----
void EditorApp::SmokeTplCapture(rhi::CommandList& cl) {
    if (Launch().smokeTemplate && g_tplSmoke.capReq) {
        g_tplSmoke.capReq = false;
        g_tplSmoke.capPending = true;
        cl.DebugRecordCapture();
        if (viewport_->GameRenderTarget().IsValid())
            cl.DebugRecordTextureCapture(viewport_->GameRenderTarget());
    }
}

// ---- --smoke-template 指针保持窗读点（批④：FeedGameUiInput 随 UI 桥外迁
// EditorAppUiBridge.cpp 后，跨族读经此访问器——g_tplSmoke 不再出 TU）----
bool EditorApp::SmokeTplPointerHold() const { return g_tplSmoke.pointerHold; }

} // namespace lemon::editor
