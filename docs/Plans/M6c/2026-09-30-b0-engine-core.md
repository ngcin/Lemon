# M6c 批⓪ —— 引擎骨架：miniaudio vendor + AudioEngine + 静音降级 + 混音单测

Status: done（2026-09-30 开工当日收口：ctest 3/3（engine-tests 33585 checks）+ 全量构建链接通过 + **100 并发 SFX 模拟侧实测 0.0092 ms**（判据 ≤0.5ms，余量 54 倍）+ bench-sim 自录自放 `replay=PASS mismatches=0`（金回放零影响旁证：StateHash/ComponentCatalog/Systems 零改动）。[DevLog](../../DevLog/2026-09-30-m6c-b0-audio-engine-core.md)）

> 前置：ADR-015 D1–D4 已拍板（2026-09-30 用户，均按建议）；miniaudio 0.11.25 zip 用户手备（网络阻断 CPM 不可行）。

## 分解与勾销

| # | 项 | 落点 | 状态 |
|---|---|---|---|
| T1 | 版本/内建核定 | 0.11.25（`MA_VERSION_*` 确认）；仓根双文件 = `miniaudio.h` 4.1MB amalgamated + `miniaudio.c` 56B 实现 TU；**Vorbis 内建确认**（内嵌 stb_vorbis，`MA_NO_VORBIS` 守卫不定义即启用；WAV/MP3/FLAC/Vorbis 四格式烤制期全可用） | ✅ |
| T2 | vendor 三件 | `Engine/Audio/thirdparty/{miniaudio.h, miniaudio.c, LICENSE}`（公有领域/MIT-0 双许可）；平台链接零配置（macOS/Windows 框架运行时 dlopen，`dl` 已在链接表） | ✅ |
| T3 | AudioEngine 核心 | `Engine/Audio/AudioEngine.h`（92 行，零 miniaudio 类型）+ `AudioEngine.cpp`（372 行，Pimpl）：设备回调线程混音（f32 累加→钳位）、voice 池 64 + 偷最旧一次性、等功率声像、组/主音量、暂停语义（循环+BGM 挂起，UI 组永不挂起）、静音降级（逻辑声部照常记账）、`LEMON_AUDIO=off` | ✅ |
| T4 | CMake 接线 | `Engine/CMakeLists.txt` 源表 + vendor `-w` 警告静音；`lemon-engine` 静态库直编（M7a `lemon-game` 同源消费） | ✅ |
| T5 | 登记 | `THIRD_PARTY.md` 表行 + 版权声明段 + 预规划脚注出列；07 矩阵 §2 里程碑表 M6c 行 | ✅ |
| T6 | 单测四件 | `tests/engine_tests.cpp:5251` TestAudioMixerMath（声像中心 -3dB/pan±1/立体声路由/两声部线性和/组与主增益乘法）；`:5315` TestAudioLifecycle（末帧当场退役/循环回卷 ramp 证毕/Stop/StopAll/voiceId 单调不复用/池满偷最旧/全循环拒绝/暂停语义）；`:5396` TestAudioDeviceInitNoCrash（真初始化 = 设备 ✓ / null 后端 / 降级三路径不崩；设备模式 MixOffline 红字拒绝）；`:5416` TestAudioBench100Sfx（0.5ms 断言 + 实测数字打印） | ✅ |

## 实测数字

- **100 并发 SFX 模拟侧（100×Play staging + Tick 应用）= 0.0092 ms**（08 §2 M6c 判据 ≤0.5ms）。余量来自：voice 池定长数组（Play 零分配）+ 偷声部线性扫 64 槽 + 短临界区单锁。
- engine-tests 33585 checks OK；ctest 3/3（imgui-isolation / script-tests 随全量构建复跑绿）。
- 全量构建（editor + 全部 samples/benches）链接通过——miniaudio 入 `lemon-engine` 无链接配置增量。

## 实现期发现与修正（批内闭环）

1. **一次性声部"末帧 done"边角**：混音循环只在块首检查游标越界——恰好混到末帧的声部要等下一块才终止。修 = 帧循环后补 `cursor>=end → done`（`AudioEngine.cpp` MixVoices 尾），单测"exactly at end frame"锁死。
2. **测试栈溢出（`__stack_chk_fail` 实抓）**：`float out[2]` 却 `MixOffline(out, 99)`（契约 = out 容纳 frames×2 float）。Release 下无声 abort、stdout 缓冲吞掉 FAIL 行——pty 复跑 + lldb `thread backtrace` 定位。教训：MixOffline 这类"调用方给长度"的裸指针契约，单测缓冲尺寸即契约的一部分。
3. **跨实例 clipId 误用**：子引擎复用了别实例注册的 clipId（注册表每实例私有，查无 → Play 全拒）。测试修正；引擎行为（返回 0）本身正确。
4. 设计期自纠两处：暂停规则精确化（UI 组含其循环永不挂起，`group != Ui && (loop || Bgm)`）；`voices = {}` 裸数组整体赋值不合法改逐元素；嵌套 `Options` NSDMI 用于包围类默认实参（clang 限制）提到命名空间级 `InitOptions`。

## 验收对照（M6c.md 批⓪ 出口判据）

| 判据 | 结果 |
|---|---|
| THIRD_PARTY/07 登记落 | ✅ 两处 |
| ctest 绿（离线混音数学 + 无设备降级） | ✅ 3/3 |
| 100 并发 SFX 模拟侧 ≤ 0.5ms | ✅ 0.0092 ms |
| Vorbis 内建核定结论落批文件 | ✅ 见 T1 |

金回放零影响：批⓪ 零 ECS 面（无组件/系统/通道），`StateHash.cpp`/`ComponentCatalog.cpp`/`Systems.cpp` 零改动；bench-sim 自录自放 PASS 作机制旁证。**批② 加 AudioSource（id 31）时按 UIDocument 先例三档重录**。

## 遗留

- 无阻塞遗留。批⓪ 未接编辑器装配（EditorApp Init/Tick 挂线归批③，与混音面板同批）；BGM 单槽 + 交叉淡出（D4）归批② 随 C# API 落。
