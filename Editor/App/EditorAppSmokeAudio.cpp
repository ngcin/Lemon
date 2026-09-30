// Lemon 编辑器 — M6c 批① --smoke-audio 资产链冒烟（第一段）
// 链路：真项目（--project 传入，须含 Audio 资产）导入识别 → meta importer 段 →
// 后台烤制（WarmAudioBakes 已入队；本链等待收敛）→ .baked Peek 头校验 → Edit 态
// 试听（EnsureClipLoaded + TogglePreviewAudio：声部存活 → 再点停止）。
// 真人验收面（音量/听感）不在此链——Edit 态逻辑通道的机器证明。
#include "App/EditorApp.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <string>

#include "Audio/BakedClip.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"

namespace lemon::editor {

bool EditorApp::RunSmokeAudioChain() {
    namespace fs = std::filesystem;
    int entries = 0, metaOk = 0;
    uint64_t first = 0;
    for (const AssetEntry& e : ctx_.Assets().Entries()) {
        if (e.type != AssetType::Audio || e.missing) continue;
        if (first == 0) first = e.guid;
        ++entries;
        // meta：新建音频应带 importer 段（loop/preload；预写老 meta 缺段 = 容错不算失败）
        std::error_code ec;
        std::ifstream mf(ctx_.Assets().AbsolutePath(e) + ".meta", std::ios::binary);
        if (mf) {
            std::string text((std::istreambuf_iterator<char>(mf)),
                             std::istreambuf_iterator<char>());
            if (text.find("importer") != std::string::npos) ++metaOk;
        }
    }
    if (entries == 0) {
        LEMON_ERROR("smoke-audio：项目无音频资产（--project 须指向含 Assets/Audio 的工程）");
        return false;
    }
    // 后台烤制收敛（WarmAudioBakes 已在分发点入队；上限 60s 防挂死）
    for (int i = 0; i < 1200 && audioBakePending_.load() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (audioBakePending_.load() > 0) {
        LEMON_ERROR("smoke-audio：后台烤制 60s 未收敛（pending=%d）", audioBakePending_.load());
        return false;
    }
    int baked = 0;
    const std::string root = ctx_.Assets().ProjectRoot();
    for (const AssetEntry& e : ctx_.Assets().Entries()) {
        if (e.type != AssetType::Audio || e.missing) continue;
        audio::BakedClipInfo bi;
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)e.guid);
        const std::string dst = root + "/.lemon/baked/audio/" + hex + ".baked";
        if (!audio::PeekBakedClip(dst.c_str(), bi)) {
            LEMON_ERROR("smoke-audio：.baked 缺失或头非法：%s", dst.c_str());
            return false;
        }
        ++baked; // 头校验在 Peek 内（魔数/版本/采样率/载荷一致）
        if (bi.frameCount == 0 || (bi.channels != 1 && bi.channels != 2)) {
            LEMON_ERROR("smoke-audio：.baked 字段非法（%uch / %u 帧）：%s", bi.channels,
                        bi.frameCount, dst.c_str());
            return false;
        }
    }
    // 试听：Edit 态装载 + 声部存活 → 再点停止（复用双击通道本体）
    TogglePreviewAudio(first);
    const bool voiceUp = previewVoice_ != 0 && audio_.ActiveVoiceCount() >= 1;
    TogglePreviewAudio(first);
    const bool voiceDown = previewVoice_ == 0;
    const bool previewOk = voiceUp && voiceDown;
    std::printf("[lemon] editor-smoke audio: entries=%d baked=%d meta=%d preview=%d/%d => %s\n",
                entries, baked, metaOk, (int)voiceUp, (int)voiceDown,
                (entries == baked && previewOk) ? "OK" : "FAIL");
    return entries == baked && previewOk;
}

} // namespace lemon::editor
