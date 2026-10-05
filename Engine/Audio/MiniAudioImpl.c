// Lemon 引擎 — miniaudio 实现 TU（M6c 批⓪；vendor miniaudio.c 的超集替代）
// 0.11.25 约定：内建 Vorbis 解码器的 stb_vorbis 须外供，且与 miniaudio 实现同
// TU、先于 miniaudio.h 包含（STB_VORBIS_INCLUDE_STB_VORBIS_H 守卫开粘合层）。
// 消费侧（AudioEngine.cpp/BakedClip.cpp）用 STB_VORBIS_HEADER_ONLY 只取声明，
// 保证两 TU 的 ma_decoder 宏视角一致。
#include "thirdparty/stb_vorbis.c"

// stb 实现段的单字母声道宏退役（批⑦ CI 热修⑥）：L/C/R（:5131-5133，声道映射
// 表专用，使用点全在 stb_vorbis.c 内部——上方 include 展开完即无主）会污染同
// TU 后续 include 的 Windows 头——winnt.h 解析级联 + miniaudio WASAPI 的宽字符
// 串字面量 L"..." 前缀被宏吞（C2059 'constant' / C2143 "missing ')' before
// 'string'"，mac 不可见：CoreAudio 路径不走 windows.h）。TRUE/FALSE 与 Windows
// 头同值重定义，无害不undef。
#undef L
#undef C
#undef R

#define MINIAUDIO_IMPLEMENTATION
#include "thirdparty/miniaudio.h"
