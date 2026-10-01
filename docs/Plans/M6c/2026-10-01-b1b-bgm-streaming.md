# M6c 批①b — BGM/长 clip 流式（SPSC 环 + 专用填充线程）

Status: done（2026-10-01 当日收口：引擎单测 **34008 checks**（+332：SPSC 环序锁 + 流式声部链）+ ctest 3/3 + svr-test `--smoke --play` `8 成功（流式 2）` 零欠载 + `--smoke-audio` OK。分流判据实测：21.6MB BGM 与 1.15MB 环境音走流式（RAM 常驻 256KiB 环/声部），其余 ≤384KB 短音效保持整载。[DevLog](../../DevLog/2026-10-01-m6c-b1b-bgm-streaming.md)）

## 范围（自批① 拆出的专注批）

- **出口判据**（M6c.md 批①b 行）：>1MiB clip 走流式（RAM 常驻 < 阈值）；BGM 播放无爆音/欠载静音 ≤ 单次；单测锁 SPSC 环序。
- 落地：`Engine/Audio/SpscRing.h`（无锁 SPSC 字节环）+ `AudioEngine::RegisterStreamClip`（流式 clip 注册）+ 声部级 `StreamFeed`（256KiB 环/句柄/回卷游标）+ 设备模式专用填充线程 + 编辑器 `EnsureClipLoaded` 分流（payload > `kStreamThresholdBytes`(1MiB) 且未显式 `audioPreload` → 流式；`kStreamThresholdBytes` 死常量转活）。
- 附带：同步烤制超 100ms 红字（后台预热未命中证据面）；装载日志分流计数（`8 成功（流式 2）`）。

## 设计要点（ADR-015 M2 实现口径）

1. **回卷换位在生产者侧**：环内是"线性化帧流"（[0..loopEnd) + [loopStart..loopEnd) 反复展开）——消费者（设备回调）只顺序读帧、无回卷逻辑；生产者跨过 loopEnd 时 `SeekBakedFrame` 换位续喂。消费者终点语义分流：整载循环 = loopEnd（消费者回卷），流式循环 = 无穷，一次性 = frameCount。
2. **每声部一份 StreamFeed**（环+句柄+游标）：同 clip 多声部（BGM 交叉淡出）互不干扰；句柄随声部开闭（`shared_ptr` + `dead` 原子弃养，填充线程撤 job 后末引用 `fclose`）。
3. **起播主线程 open + 头解析 + 预填整环**（窄偏差于 ADR"主线程只做 open+头解析"）：256KiB 顺序读 ≈ 亚毫秒，保首回调零欠载（无预填则每次起播必欠载一拍）。
4. **填充线程**：设备模式唯一生产者（空队 condvar 睡、活跃 4ms 轮询补环；256KiB ≈ 1.37s 立体声余量）；静音/离线模式无线程——`PumpStreams()` 手动泵（单测确定性驱动，设备模式调用 = 双生产者违规红字拒）。
5. **欠载观测**：环空帧静音混出并计数（`StreamUnderrunFrames()` + Tick 一次性 Warn）——"欠载静音 ≤ 单次"验收判据的机器证据。
6. 顺带语义对齐：`loopEnd<=loopStart` 退化区间统一视同不循环（整载路径原先截断在 loopEnd，流式路径消费者无法表达该语义——两路对齐全曲；烤制路径本就钳界不会产出退化区间，仅直灌 API 受影响）。

## 提交前 review 收口（2026-10-01，两修）

1. **Shutdown `deviceOk` 与填充线程共存亡**：原在 join 前置 false——窗口期 `PumpStreams` 见 false 放行手动泵 = 与填充线程同环双生产者（主线程调用契约下不可达，防御纵深修）。
2. **流式循环声部 u32 游标 24.8h 回卷边界**：`cursor` 抵 `UINT32_MAX` 会被终点检查误判曲终（BGM 静默停）——`!(v.stream && v.loop)` 守卫跳过终点检查让游标回卷续播。

其余核对无恙：SPSC 内存序成对、锁序单向（engine→fill；填充线程不取 engine 锁）、Play 两阶段与 ResetClips 竞争（PlayLocked 重校验）、feed 字段经 fillMtx_ 发布的 happens-before、PumpFeed 边界（loop 语义归一后 end-nextFrame ≥ 1）、one-shot 曲终双检查、句柄单生产者独占、编辑器缓存/分流语义。

## 遗留

- 真人听感验收（长 BGM 进出 Play / 交叉淡出/暂停恢复）待用户——批② 的真人两项同场补验。
- 填充线程单流粒度：>1MiB 的**一次性** SFX（非循环长音效）同样走流式（同机制），实测 svr-test 1.15MB 环境音即此形态。
