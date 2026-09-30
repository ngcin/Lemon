// Lemon 引擎 — .baked 音频容器读写（M6c 竖切批，ADR-015 M2）
// 烤制 = 任意可解码源（wav/ogg/mp3/flac）→ 48kHz 交错 PCM16 + 32B 头（"LBA1"）；
// 装载 = 纯头校验 + 载荷读入，零解码器（M7a lemon-game 同路径消费，packager 只拷贝）。
// 纪律：本头文件零 miniaudio 类型（实现 TU 内含 vendor 头）。
#pragma once

#include <cstdint>
#include <vector>

namespace lemon::audio {

struct BakedClipInfo {
    uint32_t frameCount = 0;
    uint16_t channels = 0;   // 1|2
    uint32_t loopStart = 0;  // 帧（竖切批恒 0/0 = 全曲循环；批① 起 .meta importer 声明）
    uint32_t loopEnd = 0;    // 0 = 尾（== frameCount）
};

/// 解码 srcPath（miniaudio 内建 wav/mp3/flac/vorbis 四解码器）→ 归一 48k PCM16 →
/// 写 dstPath（LBA1 容器）。loopStartSec/loopEndSec = .meta importer 声明的循环点
///（秒；端点 0/0 = 全曲——批① 落地，竖切批恒全曲）。失败 false（红字）。
bool BakeAudioFile(const char* srcPath, const char* dstPath, float loopStartSec = 0.0f,
                   float loopEndSec = 0.0f);

/// 装载 LBA1：校验魔数/版本/头长/格式/采样率/载荷字节数后整读 PCM16。
/// 失败 false（outPcm 保证为空）。
bool LoadBakedClip(const char* path, std::vector<int16_t>& outPcm, BakedClipInfo& outInfo);

/// 只读头（浏览器 tooltip / smoke 探针用）：不碰载荷。失败 false。
bool PeekBakedClip(const char* path, BakedClipInfo& outInfo);

} // namespace lemon::audio
