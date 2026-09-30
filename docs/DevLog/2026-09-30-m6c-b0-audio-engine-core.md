# 2026-09-30 M6c 批⓪：音频引擎骨架落地（vendor miniaudio 0.11.25 + AudioEngine + 单测四件）

## 事件

用户拍板 ADR-015 D1–D4（均按建议：放开 mp3/flac、`.lemon/baked/audio/` 落位、固定三组、BGM 0.5s 淡出）并提供 miniaudio 0.11.25 本地 zip（网络持续阻断，CPM 路径退役、vendor 兜底转正）。批⓪ 当日开工当日收口。

## 落地内容

- **vendor 三件** `Engine/Audio/thirdparty/`（miniaudio.h 4.1MB + miniaudio.c 56B 实现 TU + LICENSE）；版本与 **Vorbis 内建核定**（内嵌 stb_vorbis，`MA_NO_VORBIS` 不定义即启用——WAV/MP3/FLAC/Vorbis 烤制期全可用，零新增第三方）。平台链接零配置（框架运行时 dlopen）。
- **`Engine/Audio/AudioEngine.{h,cpp}`**（92+372 行，头文件零 miniaudio，Pimpl 同 Window/RHI）：设备回调线程混音（f32 累加→钳位）、voice 池 64 + 偷最旧一次性（全循环占满拒绝）、等功率声像（中心 -3dB）、Master+BGM/SFX/UI 组增益、暂停语义（循环+BGM 声部级挂起、UI 组永不挂起）、静音降级一等公民（逻辑声部照常记账 + `LEMON_AUDIO=off`）、`MixOffline`/`AdvanceSilentFrames` 单测通道（设备模式红字拒绝防游标双写）。
- **CMake/登记**：`Engine/CMakeLists.txt` 源表 + vendor `-w`；THIRD_PARTY.md 行 + 版权段 + 预规划脚注出列；07 矩阵 M6c 行。
- **单测四件**（`tests/engine_tests.cpp:5251-5443`）：混音数学 / 生命周期（末帧退役、ramp 证循环回卷、偷声部、voiceId 不复用、暂停语义）/ 真初始化三路径不崩 / 100 并发 bench。

## 实测

- **100 并发 SFX 模拟侧（staging+Tick）= 0.0092 ms**（08 §2 M6c 判据 ≤0.5ms，余量 54 倍）。
- engine-tests **33585 checks OK**；ctest 3/3；全量构建（editor+samples）链接通过。
- 金回放零影响：零 ECS 面 + StateHash/ComponentCatalog/Systems 零改动；bench-sim 自录自放 `replay=PASS mismatches=0`。

## 实现期发现（批内闭环，详见[批文件](../Plans/M6c/2026-09-30-b0-engine-core.md)）

1. 一次性声部"恰在末帧混完"的 done 置位边角（块首检查漏尾）——帧循环后补判，单测锁死。
2. 测试栈溢出被 `__stack_chk_fail` 实抓（`out[2]` 混 99 帧）：Release 无声 abort + stdout 缓冲吞 FAIL 行，pty 复跑 + lldb 定位。教训：裸指针"调用方给长度"契约下，测试缓冲尺寸即契约一部分。
3. 跨实例 clipId 误用（注册表每实例私有）——引擎行为正确（查无拒绝），测试修正。

## 下一步

批① `.baked` 资产通道（AssetType::Audio 八处 + meta importer + 烤制管道 + 浏览器试听 + 06 表实测对表）。
