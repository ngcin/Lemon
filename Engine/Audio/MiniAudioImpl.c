// Lemon 引擎 — miniaudio 实现 TU（M6c 批⓪；vendor miniaudio.c 的超集替代）
// 0.11.25 约定：内建 Vorbis 解码器的 stb_vorbis 须外供，且与 miniaudio 实现同
// TU、先于 miniaudio.h 包含（STB_VORBIS_INCLUDE_STB_VORBIS_H 守卫开粘合层）。
// 消费侧（AudioEngine.cpp/BakedClip.cpp）用 STB_VORBIS_HEADER_ONLY 只取声明，
// 保证两 TU 的 ma_decoder 宏视角一致。
#include "thirdparty/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "thirdparty/miniaudio.h"
