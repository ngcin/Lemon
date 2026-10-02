// Lemon 编辑器 — M6c --smoke-audio 全链冒烟（批① 第一段 + 批③ 第二段）
// 第一段（资产链，批①）：真项目（--project 传入，须含 Audio 资产）导入识别 →
// meta importer 段 → 后台烤制（WarmAudioBakes 已入队；本链等待收敛）→ .baked
// Peek 头校验 → Edit 态试听（EnsureClipLoaded + TogglePreviewAudio：声部存活 →
// 再点停止）。
// 第二段（playOnStart，批③）：编辑场景播种 AudioSource 双实体（循环声源 bit1
// 置位 + 对照 bit1 清零）→ EnterPlay → 逻辑声部计数断言 ==1 → Stop → ==0。
// 真人验收面（音量/听感）不在此链——Edit/Play 逻辑通道的机器证明；静音模式
//（LEMON_AUDIO=off/无设备）同径断言（批⓪ 口径：逻辑记账是降级路径的一部分）。
#include "App/EditorApp.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <string>

#include "Audio/BakedClip.h"
#include "Assets/AssetDatabase.h"
#include "Components/AudioComponents.h"
#include "Core/Log.h"

namespace lemon::editor {

// 批③夹具：目标目录不是项目（无 project.lemon）才播种——回归临时目录自足、
// 真项目（svr-test 等）零污染（smoke-anim 空目录自播种同纪律）。产物 =
// project.lemon + Assets/smoke-tone.wav（48k mono PCM16 正弦 0.25s；24KB <
// 1MiB 整载路径——流式分支归批①b 单测，此处不占）。
// review 2026-10-02 #37：写盘失败（磁盘满/权限）返 false fail-fast——此前误报
// 为「项目无音频资产」的用户用法错误。
bool EditorApp::SeedSmokeAudioProject() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(launchCopy_.projectDir);
    if (root.empty() || fs::exists(root / "project.lemon", ec)) return true;
    fs::create_directories(root / "Assets", ec);
    {
        std::ofstream f(root / "project.lemon", std::ios::trunc);
        if (!f) {
            LEMON_ERROR("smoke-audio：夹具播种失败（project.lemon 不可写——磁盘满/"
                        "权限）：%s", root.string().c_str());
            return false;
        }
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-audio\",\n"
             "  \"engineVersion\": \"0.6.0-m6c\"\n}\n";
    }
    const fs::path wav = root / "Assets" / "smoke-tone.wav";
    if (fs::exists(wav, ec)) return true;
    constexpr uint32_t kFrames = 12000, kRate = 48000; // 0.25s
    constexpr uint32_t kDataBytes = kFrames * 2;        // mono PCM16
    std::ofstream f(wav, std::ios::binary | std::ios::trunc);
    if (!f) {
        LEMON_ERROR("smoke-audio：夹具播种失败（%s 不可写——磁盘满/权限）",
                    wav.string().c_str());
        return false;
    }
    auto le32 = [&f](uint32_t x) {
        for (int i = 0; i < 4; ++i) f.put(char((x >> (8 * i)) & 0xFF));
    };
    auto le16 = [&f](uint16_t x) {
        f.put(char(x & 0xFF));
        f.put(char((x >> 8) & 0xFF));
    };
    f.write("RIFF", 4);
    le32(36 + kDataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    le32(16);
    le16(1); // PCM
    le16(1); // mono
    le32(kRate);
    le32(kRate * 2); // byteRate = rate × ch × 2
    le16(2);         // blockAlign
    le16(16);        // bits
    f.write("data", 4);
    le32(kDataBytes);
    for (uint32_t i = 0; i < kFrames; ++i) {
        const double t = (double)i / kRate;
        const int16_t s = (int16_t)(12000.0 * std::sin(6.283185307179586 * 440.0 * t));
        le16((uint16_t)s);
    }
    return true;
}

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
        LEMON_ERROR("smoke-audio：项目无音频资产（--project 须指向含 Assets/Audio 的工程，"
                    "或空目录由夹具自播种——目标已是项目时不播种）");
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

    // ---- 第二段（批③）：AudioSource playOnStart / EnterPlay 逻辑声部断言 ----
    // P1 交底（review 修 2026-10-01）：真项目带 Game/ 时本段副作用面——用户脚本
    // 装配运行 + EnterPlay 载入/ExitPlay 兜底回写 .lemon/saves/ 三档（svr-test
    // 验机实证：装载值恒等回写 + .bak 轮换；恒等不具一般性）。验机建议跑副本。
    {
        std::string csproj, dll;
        if (FindGameProject(csproj, dll))
            LEMON_WARN("smoke-audio：项目含 Game/（%s）——第二段将装配运行用户脚本，"
                       "ExitPlay 回写 .lemon/saves/ 三档；验机建议跑项目副本",
                       csproj.c_str());
    }
    // 播种双实体（编辑场景）：循环声源（bit1 置位，Sfx 组）+ 对照（bit1 清零——
    // 只循环不起播）。计数断言用 ==1 而非 >=1：恰一起播 = playOnStart 生效 +
    // 对照不起播 + preview 已停（ActiveVoiceCount 只计 !done，Stop 当帧跌零）
    // 三事实合一。系统侧实现 = 批② AudioSystem::Tick ② 段扫描；本段是 Edit→Play
    // 全链（装载/后端注入/系统扫描）的机器验收。
    // review 2026-10-02 #29 交底：==1 的第四个隐含前提 = 进 Play 的编辑场景无其他
    // 音频活动（声源/会发音频命令的脚本）。当前成立靠调用时序——本链先于 --scene
    // 打开逻辑执行、编辑场景恒为空场景 + 两个播种实体；若日后 --smoke-audio 组合
    // --scene 或项目获得 startup scene 自动打开，真项目 EnterPlay 会多出声部，
    // 断言将 FAIL 且错误行 voices>1 即此因。
    bool playOk = false;
    int liveAfterEnter = -1, liveAfterStop = -1;
    bool entered = false;
    {
        ecs::Scene& s = ctx_.ActiveScene();
        const ecs::Entity src = ctx_.CreateEntity("AudioSmokeLoop");
        const ecs::Entity quiet = ctx_.CreateEntity("AudioSmokeQuiet");
        if (!src.IsNull() && !quiet.IsNull()) {
            ecs::AudioSource& a = s.Emplace<ecs::AudioSource>(src);
            a.clipGuid = first;
            a.flags = ecs::kAudioLoop | ecs::kAudioPlayOnStart;
            a.group = (uint8_t)audio::Group::Sfx;
            ecs::AudioSource& q = s.Emplace<ecs::AudioSource>(quiet);
            q.clipGuid = first;
            q.flags = ecs::kAudioLoop; // bit1 清零 = 不起播对照
            q.group = (uint8_t)audio::Group::Sfx;

            entered = TryEnterPlay(); // 装载（MountPlayAudio）+ 后端注入 + UI 归位全链
            if (entered) {
                for (int i = 0; i < 8; ++i) { // 首步 AudioSystem 建绑定起播；余步验稳
                    ctx_.TickPlay(1.0f / 60.0f);
                    audio_.Tick(1.0f / 60.0f);
                }
                liveAfterEnter = audio_.ActiveVoiceCount();
            }
            const bool stopped = entered ? StopPlay() : false;
            liveAfterStop = audio_.ActiveVoiceCount();
            playOk = entered && liveAfterEnter == 1 && stopped && liveAfterStop == 0;
            if (!playOk)
                LEMON_ERROR("smoke-audio playOnStart 断言失败：enter=%d voices=%d stop 后=%d",
                            (int)entered, liveAfterEnter, liveAfterStop);
        }
    }
    // review 2026-10-02 #11：metaOk 纳入裁决（>=1——至少一条音频 .meta 带 importer
    // 段，锁「新建音频 meta 写入 loop/preload 段」这条验收点；此前只打印不裁决，
    // importer 写坏时夹具 metaOk=0 回归仍全绿。全量 ==entries 不采用：预写老 meta
    // 缺段 = 既有容错语义）
    std::printf("[lemon] editor-smoke audio: entries=%d baked=%d meta=%d preview=%d/%d "
                "voices=%d/%d => %s\n",
                entries, baked, metaOk, (int)voiceUp, (int)voiceDown, liveAfterEnter,
                liveAfterStop,
                (entries == baked && metaOk >= 1 && previewOk && playOk) ? "OK" : "FAIL");
    return entries == baked && metaOk >= 1 && previewOk && playOk;
}

} // namespace lemon::editor
