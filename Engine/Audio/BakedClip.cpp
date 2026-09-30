// Lemon 引擎 — .baked 音频容器实现（M6c 竖切批，ADR-015 M2）
// miniaudio 类型只出现在本 .cpp。烤制经 ma_decoder 的目标格式转换（s16/48k/
// 源声道）——解码与重采样一步完成，输出端零转换器。
#include "Audio/BakedClip.h"

#include "Audio/AudioEngine.h"
#include "Core/FileOps.h" // RenameReplace（Windows 覆盖语义钉在助手内，07 §3.6⑤）
#include "Core/Log.h"

// miniaudio 0.11.25：stb_vorbis 声明须先于 miniaudio.h（守卫宏开 Vorbis 粘合层；
// 实现归 MiniAudioImpl.c，此处 HEADER_ONLY 只取声明——两 TU 宏视角一致）
#define STB_VORBIS_HEADER_ONLY
#include "thirdparty/stb_vorbis.c"
#include "thirdparty/miniaudio.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace lemon::audio {

namespace {

// LBA1 头（32B，小端；ADR-015 M2 字节表——M7 packager 按此只读消费）
struct LbaHeader {
    char magic[4];         // 'L','B','A','1'
    uint16_t version;      // 1
    uint16_t headerSize;   // 32（自校验）
    uint16_t format;       // 1 = PCM16 interleaved
    uint16_t channels;     // 1|2
    uint32_t sampleRate;   // 48000（烤制期归一）
    uint32_t frameCount;
    uint32_t loopStart;
    uint32_t loopEnd;      // == frameCount（竖切批全曲循环）
    uint32_t payloadBytes; // frameCount * channels * 2
};
static_assert(sizeof(LbaHeader) == 32, "LBA1 header must be 32 bytes (ADR-015 M2)");

void PutLE16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
void PutLE32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
uint16_t GetLE16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t GetLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

} // namespace

bool BakeAudioFile(const char* srcPath, const char* dstPath) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, kMixSampleRate);
    ma_decoder dec;
    if (ma_decoder_init_file(srcPath, &cfg, &dec) != MA_SUCCESS) {
        LogMsg(LogLevel::Error, "audio: 解码失败（格式不支持/文件坏）：%s", srcPath);
        return false;
    }
    const ma_uint32 channels = dec.outputChannels; // 0.11.25：输出声道经字段直读
    if (channels != 1 && channels != 2) { // 引擎混音域只认单/双声道
        LogMsg(LogLevel::Error, "audio: %u 声道不支持（只烤 1|2）：%s", channels, srcPath);
        ma_decoder_uninit(&dec);
        return false;
    }
    std::vector<int16_t> pcm;
    const ma_uint64 kChunk = 8192; // 帧分块读（decoder 可能少给，循环到 0）
    size_t gotTotal = 0;
    bool decodeOk = true;
    for (;;) {
        pcm.resize(pcm.size() + size_t(kChunk) * channels);
        ma_uint64 got = 0;
        // review 2026-09-30：流中错误（截断/坏帧）此前静默 break 仍落部分产物——
        // 半截 .baked 的 mtime 比源新会被缓存判定跳过重烤。错误一律判失败。
        if (ma_decoder_read_pcm_frames(&dec, pcm.data() + gotTotal * channels, kChunk,
                                       &got) != MA_SUCCESS) {
            decodeOk = false;
            break;
        }
        gotTotal += size_t(got);
        if (got < kChunk) {
            pcm.resize(gotTotal * channels);
            break;
        }
    }
    ma_decoder_uninit(&dec);
    if (!decodeOk || gotTotal == 0) {
        LogMsg(LogLevel::Error, "audio: 解码失败（零帧/流错误）：%s", srcPath);
        return false;
    }

    uint8_t head[sizeof(LbaHeader)];
    std::memset(head, 0, sizeof(head));
    head[0] = 'L'; head[1] = 'B'; head[2] = 'A'; head[3] = '1';
    PutLE16(head + 4, 1);                        // version
    PutLE16(head + 6, uint16_t(sizeof(head)));   // headerSize
    PutLE16(head + 8, 1);                        // format = PCM16
    PutLE16(head + 10, uint16_t(channels));
    PutLE32(head + 12, uint32_t(kMixSampleRate));
    PutLE32(head + 16, uint32_t(gotTotal));      // frameCount
    PutLE32(head + 20, 0);                       // loopStart
    PutLE32(head + 24, uint32_t(gotTotal));      // loopEnd = 尾
    PutLE32(head + 28, uint32_t(gotTotal * channels * 2));

    // review 2026-09-30：原子写（tmp + RenameReplace）——半截产物 mtime 比源新会被
    // 缓存判定永不重烤；先写 .tmp 全量成功再换名（WriteFileAtomic 同款纪律）
    const std::string tmpPath = std::string(dstPath) + ".tmp";
    FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) {
        LogMsg(LogLevel::Error, "audio: 烤制产物不可写（目录缺失/权限）：%s", tmpPath.c_str());
        return false;
    }
    const size_t headW = std::fwrite(head, 1, sizeof(head), f);
    const size_t pcmW = std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    const int flushOk = std::fflush(f);
    std::fclose(f);
    if (headW != sizeof(head) || pcmW != pcm.size() || flushOk != 0) {
        LogMsg(LogLevel::Error, "audio: 烤制写入不完整：%s", tmpPath.c_str());
        std::remove(tmpPath.c_str());
        return false;
    }
    if (!RenameReplace(tmpPath, dstPath)) {
        LogMsg(LogLevel::Error, "audio: 烤制产物换名失败：%s → %s", tmpPath.c_str(), dstPath);
        std::remove(tmpPath.c_str());
        return false;
    }
    LogMsg(LogLevel::Info,
           "audio: 烤制 %s → %s（%uch / %.2fs / PCM16 %ukHz）", srcPath, dstPath,
           channels, float(gotTotal) / kMixSampleRate, kMixSampleRate / 1000);
    return true;
}

bool LoadBakedClip(const char* path, std::vector<int16_t>& outPcm, BakedClipInfo& outInfo) {
    outPcm.clear();
    outInfo = {};
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    uint8_t head[sizeof(LbaHeader)];
    if (std::fread(head, 1, sizeof(head), f) != sizeof(head)) {
        std::fclose(f);
        return false;
    }
    if (std::memcmp(head, "LBA1", 4) != 0 || GetLE16(head + 4) != 1 ||
        GetLE16(head + 6) != sizeof(LbaHeader) || GetLE16(head + 8) != 1) {
        LogMsg(LogLevel::Error, "audio: .baked 头非法（非 LBA1 v1）：%s", path);
        std::fclose(f);
        return false;
    }
    const uint16_t channels = GetLE16(head + 10);
    const uint32_t sampleRate = GetLE32(head + 12);
    const uint32_t frameCount = GetLE32(head + 16);
    const uint32_t payloadBytes = GetLE32(head + 28);
    if ((channels != 1 && channels != 2) || sampleRate != uint32_t(kMixSampleRate) ||
        frameCount == 0 || payloadBytes != frameCount * channels * 2) {
        LogMsg(LogLevel::Error, "audio: .baked 字段不一致（声道/采样率/载荷）：%s", path);
        std::fclose(f);
        return false;
    }
    outPcm.resize(payloadBytes / 2);
    const size_t got = std::fread(outPcm.data(), 2, outPcm.size(), f);
    std::fclose(f);
    if (got != outPcm.size()) {
        LogMsg(LogLevel::Error, "audio: .baked 载荷不足：%s", path);
        outPcm.clear();
        return false;
    }
    outInfo.frameCount = frameCount;
    outInfo.channels = channels;
    outInfo.loopStart = GetLE32(head + 20);
    outInfo.loopEnd = GetLE32(head + 24) ? GetLE32(head + 24) : frameCount;
    return true;
}

} // namespace lemon::audio
