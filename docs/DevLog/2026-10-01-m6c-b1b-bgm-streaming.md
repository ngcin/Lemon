# 2026-10-01 M6c 批①b BGM 流式落地：SPSC 环 + 专用填充线程 + 装载分流

## 事件

继续 M6c 批次（批② done 后的既定下一步）。批① review 时拆出的"BGM 流式 ring buffer 专注批"（线程正确性需独立验证）落地：>1MiB clip 免整载（watery_cave 21.6MB 常驻 → 256KiB 环/声部），`kStreamThresholdBytes` 死常量转活，`audioPreload` 位消费。

## 落地

- **`Engine/Audio/SpscRing.h`**（新，header-only）：无锁 SPSC 字节环——单调索引免 ABA、head/tail 各自单写 + release/acquire 成对发布、容量取 2^n 掩码取段。恰一生产者（填充线程/离线泵）× 恰一消费者（设备回调/MixOffline——设备模式游标归音频线程的既有契约保证任一时刻恰一）。
- **BakedClip**：`BakedClipInfo.payloadBytes` 字段（流式/整载分流判据）+ `OpenBakedStream`（头校验 + 停在载荷首字节）+ `SeekBakedFrame`（回卷换位；Windows `_fseeki64` / POSIX `fseek` long=64 位）。
- **AudioEngine**：`RegisterStreamClip(path)`（Peek 头校验单源）+ 声部级 `StreamFeed`（256KiB 环 + 句柄 + 生产者私有 `nextFrame`）+ 设备模式填充线程（空队 condvar 睡 / 活跃 4ms 轮询）+ `PumpStreams()`（静音/离线手动泵，设备模式双生产者违规红字拒）+ `StreamUnderrunFrames()` 观测。**回卷换位在生产者侧**——环内是线性化帧流，消费者零回卷逻辑。起播主线程 open + 头解析 + **预填整环**（ADR"主线程只做 open+头解析"的窄偏差，亚毫秒级，保首回调零欠载）。Play 两阶段：锁内取路径 → 锁外 open/预填 → 锁内落位。
- **编辑器**：`EnsureClipLoaded` 分流（payload > 1MiB 且未 `audioPreload` → 流式注册）+ 同步烤超 100ms 红字 + 装载日志流式计数。
- 语义对齐：`loopEnd<=loopStart` 退化区间两路统一视同不循环（整载原先截断在 loopEnd；烤制路径钳界不会产出退化区间，仅直灌 API 受影响）。

## 实测

- 引擎单测 **34008 checks**（33676 → +332）：`TestAudioSpscRing`（交错随机块 FIFO 跨回卷 + 全满拒写 + 双线程 4MiB 字节精确）+ `TestAudioStreamVoice`（预填即鸣零欠载/样本字节精确/一次性曲终/循环回卷线性化对照/欠载精确计数 14464 帧/同 clip 双声部叠加）。**一次全绿**。
- ctest 3/3；全量构建零 error；`--smoke-audio` OK（试听链打到流式路径）。
- svr-test `--smoke --play`：`进 Play 音频装载：8 成功（流式 2），全部就绪`——分流恰中 >1MiB 两件（21.6MB BGM + 1.15MB 环境音），**零欠载零失败**；既有孤儿 .meta 红字为项目卫生问题非本批。

## 发现

1. 设备失败降级路径（昨日二轮热修落位的 `silent_`）与流式正交兼容：静音模式无线程、`PumpStreams` 手动泵、AdvanceLocked 逻辑游标照推——降级/CI 确定性双契约保持。
2. 消费者侧回卷在流式下不可表达（环是线性字节流）——把回卷职责整体推给生产者是本批最关键的结构决策，环序/欠载/曲终三类单测全靠它化简。
3. 提交前 review 两修：Shutdown `deviceOk` 改在填充线程 join 后落 false（堵 `PumpStreams` 双生产者理论窗口）；流式循环声部 u32 游标 `UINT32_MAX` 回卷守卫（否则连续循环 2³² 帧 ≈24.8h 误判曲终）。

## 下一步

真人听感验收（长 BGM 进出 Play/交叉淡出/暂停恢复——与批② 真人两项同场）→ 批③。
