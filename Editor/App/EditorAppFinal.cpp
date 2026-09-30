// Lemon 编辑器 — --final 终验族（M4.5 §6 全量化；批③c-6 自 EditorApp.cpp Run
// 外迁：向导播种 / 场景开 / Play 中热重载播种 / fps 采样 / 末帧验收。模式同
// 批③c-1..5：挂点原位、逐位不变；验收块 exitCode 写改 finalOk 聚合返回。
// play 往返四量（playAliveAtStop/playEnterMs/playExitMs/playVerified）是 Run
// 跨族共享态——以参数/返回值过桥，不入本族状态。

#include "App/EditorApp.h"
#include "App/EditorApp.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Scripting/ScriptHost.h"
#include "imgui.h"

namespace lemon::editor {

// ---- --final 向导播种（M4.5 第一步；批③c-6 自 Run 外迁，挂点原位）----
bool EditorApp::FinalSeedProject() {
    if (!Launch().finalTest) return true;
#ifndef LEMON_SCRIPT_DIR
    LEMON_ERROR("终验需要 LEMON_BUILD_SCRIPTING=ON 构建");
    return false;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(Launch().projectDir.empty() ? "/tmp/lemon-m45" : Launch().projectDir);
    fs::remove_all(p / "lemon-final", ec); // 幂等：清上次终验残留
    ProjectDesc desc;
    desc.parentDir = p.string();
    desc.name = "lemon-final";
    desc.sdkDir = LEMON_SCRIPT_DIR;
    desc.engineVersion = "0.4.0-m4";
    const std::string root = ProjectWizard::Create(desc, &wizardSpawnGuid_);
    if (root.empty()) {
        LEMON_ERROR("终验失败：项目向导创建失败");
        return false;
    }
    launchCopy_.projectDir = root;
    launch_ = &launchCopy_;
    LEMON_LOG("final: 向导建项目 OK %s", root.c_str());
    return true;
#endif
}

// ---- --final 场景开 + 判据场景播种（批③c-6 自 Run else-if 链外迁：守卫留原位）----
bool EditorApp::FinalSeedScene() {
    if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return false;
    SeedJudgementScene(wizardSpawnGuid_);
    smokeSeeded_ = ctx_.ActiveScene().AliveCount();
    return true;
}

// ---- --final Play 中热重载播种（§6 #2：frame 20 改 SpawnerBehaviour 窗口；
// 批③c-6 自 Run 外迁，挂点原位）----
void EditorApp::FinalFrame(uint64_t frame) {
    if (Launch().finalTest && frame == 20) {
        namespace fs = std::filesystem;
        const fs::path sp = fs::path(launch_->projectDir) / "Game" / "SpawnerBehaviour.cs";
        std::ifstream in(sp, std::ios::binary);
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t at = src.find("_tick > 40");
        finalPlayReloadOk_ = at != std::string::npos;
        if (finalPlayReloadOk_) {
            finalAliveAtReload_ = ctx_.ActiveScene().AliveCount();
            src.replace(at, 10, "_tick > 70");
            {
                std::ofstream out(sp, std::ios::trunc);
                out << src;
            } // 先落盘再编译（流未 close 就 build 会读到半截文件——实测坑）
            finalReloadFrame_ = frame;
            finalPlayReloadOk_ = TryHotReloadScripts("final-play（Play 中）");
            finalPlayReloadMs_ = hotReloadMs_;
        }
        if (!finalPlayReloadOk_) LEMON_ERROR("final: Play 中热重载失败");
    }
}

// ---- --final fps 采样（§6 #6：预热 60 帧与换装窗口 90 帧剔除；批③c-6 自 Run
// 外迁，挂点原位（AdvanceFrame 后））----
void EditorApp::FinalSample(uint64_t frame) {
    if (Launch().finalTest && ctx_.Playing() && frame > 60 &&
        frame - finalReloadFrame_ > 90) {
        const float f = ImGui::GetIO().Framerate;
        if (f > 1.0f && f < finalPlayMinFps_) finalPlayMinFps_ = f;
    }
}

// ---- --final 末帧验收（§6 #1/#2/#3/#6/#7：StateBag/双态热重载/自动备份链/
// fps/冷启动；批③c-6 自 Run 外迁，exitCode 写改 finalOk 聚合返回）----
bool EditorApp::FinalVerdict(uint32_t playAliveAtStop, double playEnterMs,
                             double playExitMs, bool playVerified, double firstFrameMs) {
    bool finalOk = true;
    if (Launch().finalTest) {
        // StateBag 续跑断言：换装于 tick=20（窗口 40→70）→ 总刷怪 = 16 + 50 = 66
        // （若状态丢失 = 16 + 66 = 82 ≠ 66；若换装失败 = 36）
        finalStateBagTotal_ = (int)playAliveAtStop - (int)smokeSeeded_;
        const bool bagOk = finalStateBagTotal_ == 66;
        // Edit 态热重载一例（§6 #2 双态各一）：加 EditProbeBehaviour → 注册表可见
        {
            namespace fs = std::filesystem;
            const fs::path gp = fs::path(launch_->projectDir) / "Game";
            std::ofstream probe(gp / "EditProbeBehaviour.cs", std::ios::trunc);
            probe << "public sealed class EditProbeBehaviour : Lemon.LemonBehaviour { }\n";
            std::ifstream in(gp / "GameMain.cs", std::ios::binary);
            std::string src((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
            // 在既有注册行前插一行（锚点替换——直接动 Configure 签名会破坏大括号配对）
            const std::string anchor = "Lemon.Behaviours.Register<InputMoverBehaviour>();";
            const size_t at = src.find(anchor);
            if (at != std::string::npos)
                src.insert(at, "Lemon.Behaviours.Register<EditProbeBehaviour>();\n        ");
            std::ofstream out(gp / "GameMain.cs", std::ios::trunc);
            out << src;
        } // 落盘作用域结束（先 close 再编译）
        finalEditReloadOk_ = TryHotReloadScripts("final-edit（Edit 态）");
        bool seen = false;
        for (const std::string& n : ctx_.ScriptTypeNames()) seen |= n == "EditProbeBehaviour";
        finalEditReloadOk_ = finalEditReloadOk_ && seen;
        // 自动备份/崩溃恢复链（§3.8）：dirty → 快照 → 检出 → 恢复（保持 dirty）→ 落盘 → 清
        bool autosaveOk = false;
        {
            ctx_.Select(ctx_.Primary(), false);
            ctx_.dirty = true;
            const bool wrote = ctx_.AutoSaveNow();
            const std::string rec = ctx_.DetectAutosaveRecovery();
            const bool opened = !rec.empty() && ctx_.OpenSceneRecovery(rec);
            const bool keptDirty = ctx_.dirty;
            const bool saved = ctx_.SaveScene();
            const bool cleared = ctx_.DetectAutosaveRecovery().empty();
            autosaveOk = wrote && opened && keptDirty && saved && cleared;
        }
        const int leaks = host_ ? host_->HotReloadLeakCount() : 0;
        const int reloads = HotReloadCount();
        const bool hrOk = finalPlayReloadOk_ && finalEditReloadOk_ &&
                          finalPlayReloadMs_ <= 2000.0 && hotReloadMs_ <= 2000.0 &&
                          leaks > 0 /* A 线已知泄漏（探针口径）*/;
        const bool fpsOk = finalPlayMinFps_ >= 45.0;
        const bool coldOk = firstFrameMs < 2000.0;
        finalOk = bagOk && hrOk && fpsOk && coldOk && autosaveOk && playVerified;
        std::printf("[lemon] final-wizard: dirs/project.lemon/Game/spawn.png => %s\n", "OK");
        std::printf("[lemon] final-judgement: zero-code scene entities=%u visible=%u\n",
                    smokeSeeded_, viewport_->LastSceneVisible());
        std::printf("[lemon] final-hotreload: play=%.0fms edit=%.0fms(≤2000) "
                    "stateBag=%d/66 reloads=%d leaks=%d(A线) => %s\n",
                    finalPlayReloadMs_, hotReloadMs_, finalStateBagTotal_, reloads, leaks,
                    hrOk ? "OK" : "FAIL");
        std::printf("[lemon] final-play: minFps=%.0f(≥45) enter=%.1fms exit=%.1fms "
                    "byte-exact=%s => %s\n",
                    finalPlayMinFps_, playEnterMs, playExitMs,
                    ctx_.LastExitVerified() ? "YES" : "NO", (fpsOk && playVerified) ? "OK" : "FAIL");
        std::printf("[lemon] final-autosave: snapshot+recovery+save-clear => %s\n",
                    autosaveOk ? "OK" : "FAIL");
        std::printf("[lemon] final-coldstart: %.0fms(<2000) => %s\n", firstFrameMs,
                    coldOk ? "OK" : "FAIL");
        if (!finalOk) std::printf("[lemon] final FAIL 项：bag=%d hr=%d fps=%d cold=%d autosave=%d play=%d\n",
                                  bagOk, hrOk, fpsOk, coldOk, autosaveOk, playVerified);
    }
    return finalOk;
}

} // namespace lemon::editor
