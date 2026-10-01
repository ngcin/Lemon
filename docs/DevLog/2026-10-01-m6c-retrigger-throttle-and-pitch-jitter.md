# 2026-10-01 M6c 听感验收驱动热修（第二轮）：重触发节流 + 音高微扰

## 事件

上一轮热修（母带软限幅 + 同 clip 并发上限）后用户复听：**kill.wav 叠发破音消除**，但 **pickup.wav 密集时"像放鞭炮"**。定位 = 经典**机枪效应**（machine gun effect）：同一素材高频重触发——音高全同（相干叠加/拍频）+ 密度过高（起播换血持续不断）。素材实测：SE Trade 单声道 394ms chime、峰值 −3.2dB；游戏侧每颗宝石拾取 `PlayOneShot(0.5f)`，吸尘期十几~几十 Hz。限幅/并发上限治的是"叠加过顶"，治不了"同音高连发"。

## 落地（业界标准两件套）

- **同 clip 重触发节流**（`SetRetriggerCooldown`，默认 45ms ≈ 22Hz 上限）：非循环播放在距上次**被接受**起播的窗内 → 丢弃（返 0，不占槽不偷不更锚）；循环声部豁免（BGM/环境的重触发语义是"重启"非"叠发"）；异 clip 互不影响。**时钟 = 混音帧域**（`mixFrames_`，MixVoices/AdvanceLocked 同径累加）——设备/静音/离线一致，测试确定性（墙钟在离线测试不流逝）。
- **音高微扰**（`SetPitchJitter`，默认 ±2%）：Sfx/Ui 组非循环**整载**声部起播时均匀随机 ±range（BGM 组/循环/流式不扰——乐律精确与环字节序）；分数游标 + **1-tap 线性插值**（rate==1 走既有整数游标位精确路径，字节精确契约不动；尾帧持住防越界）。序列固定种子——表现层装饰不入状态哈希/金回放。ADR-015 M2"零重采样器"口径按性能语义不变（1-tap lerp ≠ 采样率转换重采样器），M4 注记说明。

组合效果：22Hz 上限 × 每次微扰去相干 → 密集拾取从"同频 chime 拍频嗡鸣/鞭炮"变"硬币流闪烁"；394ms 尾 × 22Hz ≈ 8.6 想叠 → 并发上限 4 照常兜底偷取。

## 实测

- 引擎单测 **34036 checks**（34023 → +13）：`TestAudioRetriggerThrottlePitchJitter`——窗内丢（返 0 不占槽）/恰 50ms 过窗即收/异 clip 不连坐/循环豁免/微扰两连播同帧对拍相异（±5% 放大观测）/关闭回整数路径位精确（±1e-6）。**一次全绿**。
- 既有测试适配（六处 `SetRetriggerCooldown(0)`/`SetPitchJitter(0)` 归零，均为"断言别的东西"的语义测试）：MixerMath/Lifecycle×2/StreamVoice（同帧双声部 BGM 形态）/Bench100（保留池满+并发上限两级偷取覆盖）/FadeEnvelope/Limiter（相干叠加数学）；script-tests TestAudioSdk（探针同帧同 clip 连发 Play+PlayAt）。常数 PCM 夹具对微扰天然免疫（lerp 等值样本 = 原值）。
- ctest 3/3；`--smoke-audio` OK；svr-test `--smoke --play --frames 240`：`进 Play 音频装载：8 成功（流式 2）` 零欠载；`script-spawn` FAIL 仍为用户 WIP 层既有（上轮已 stash 基线复现实锤，SpawnerBehaviour 未注册）。

## 发现

1. 节流时钟必须用**混音帧域**而非墙钟：离线/静音测试里 AdvanceSilentFrames 推进数千帧而墙钟近 0——墙钟窗会永久吞掉测试的连播。
2. 音高微扰是机枪效应的**根治项**（去相干），节流是**密度项**（限速率）；只做节流 = 慢速机枪，只做微扰 = 高频闪烁毛雨，两者互补。
3. 节流丢返回 0 与既有语义天然合流（池满返 0 同款）；AudioChannel 提交期死条目回收机制原样消费，C# `PlayOneShot` 丢弃无感。
4. per-资产覆写（节流窗/并发上限/微扰幅度按 clip 配）是下一步正解位——`.meta` importer 段扩字段归批③。

## 下一步

真人复听密集拾取（吸尘收宝石场景）+ 击杀叠发（上轮修复复验）——与批② 真人两项同场。per-资产音频参数进批③ 候选清单。
