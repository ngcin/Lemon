// Lemon 引擎 — 组件目录 · 音频组首件（M6c 批②；Unity AudioSource 同构粒度：
// 挂实体随 Transform 移动的循环/环境声，AudioSystem #20 逐 tick 监听器空间化）。
// 运行时声部绑定全在 AudioSystem 绑定表（系统局部，非 ECS），**严禁回写本组件**
//（快照确定性铁律——只读扫描，UIDocument 同款）。clipGuid 运行时改值 = 停旧
// 起新（AudioSystem 纯读反应）；playOnStart 只在绑定建立时起一次（重触发语义
// 不做——动态位 = Lemon.Audio.PlayAt；StopAll 后不自动复活）。
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

inline constexpr uint16_t kAudioLoop = 1 << 0;        // bit0：循环（环境声）
inline constexpr uint16_t kAudioPlayOnStart = 1 << 1; // bit1：进 Play 自动起播（默认
                                                      // 置位，Unity playOnAwake 同构）

struct AudioSource {
    uint64_t clipGuid = 0;   // 音频资产 GUID（0 = 无片静默，Inspector 提示）
    float volume = 1.0f;
    float refDist = 256.0f;  // 全增益半径（ADR-015 M5 线性衰减）
    float maxDist = 1024.0f; // 衰减到 0 半径
    uint16_t flags = kAudioPlayOnStart;
    uint8_t group = 1;       // audio::Group（0 Bgm/1 Sfx/2 Ui；AudioSystem 钳界）
    uint8_t pad_ = 0;
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 逐字节同构，改动=破回放）----
// u64+f32×3+u16+u8+u8 = 24B 自然对齐 sizeof=24/alignof=8。ADR-015 M3 原字段序
// （u8 group 在 u16 flags 前）自然对齐下 25→32B——2026-09-30 批② 勘误调序为
// flags 前、group 后（ADR 修订注记），语义不变。
static_assert(std::is_trivially_copyable_v<AudioSource> && sizeof(AudioSource) == 24 &&
                  alignof(AudioSource) == 8,
              "AudioSource 布局冻结");

} // namespace lemon::ecs
