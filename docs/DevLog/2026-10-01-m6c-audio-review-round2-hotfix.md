# 2026-10-01 M6c 音频二轮 review 热修批：降级落位/烤制并发/坏头健壮性

## 事件

用户要求二轮 review M6c 音频全部源码查 bug。范围 = AudioEngine/AudioChannel/BakedClip/Spatial2D/AudioSystem #20/ScriptHost 桥/C# SDK/编辑器装载与后台烤制/冒烟与单测，对照 2026-09-30 两份 review DevLog 与 git 历史逐条核实。结论：**三 bug 按优先级热修**（其一为 09-30 热修③只落一半），其余核心面（线程模型、生命周期、循环回卷、命令保序、vtable 对齐、Play 世界重建语义）复核无恙。

## 热修（三修 + 一处注释口径）

1. **设备失败降级落 `silent_`**（`AudioEngine.cpp` Init）：`ma_device_init`/`ma_device_start` 失败分支 09-30 起只打红字不置位——热修③ DevLog 自称"silent_ 置位"但代码从未落地（git 首笔即缺）。后果链：`silent_` 恒假 → `Tick` 不推逻辑游标 → 声部永 done=false → `AudioChannel::Submit` 死条目回收（VoiceAlive 判据）失效 = **每次播放泄漏一条记账**（高频 PlayOneShot 游戏无界增长）+ BGM 淡出永不完成 + `silent()` 误报有声。恰落在"无音频设备不崩"验收判据的降级路径上。修 = 两失败分支各补 `silent_ = true`。无单测（设备故障注入不可移植），代码面闭环。
2. **烤制 tmp 唯一化**（`BakedClip.cpp`）：tmp 名原为确定性 `dst + ".tmp"`——后台烤制线程（WarmAudioBakes/Rescan）与主线程 `EnsureClipLoaded`（EnterPlay 兜底/试听）在 `BakeStale` 判定到 dst rename 之间存在 TOCTOU 窗口（无头 `--play` 冷缓存首跑与交互快进 Play 均默认可达；`--smoke-audio` 有收敛等待除外），并发时两把 FILE* 以 "wb" 开同一 inode 交错写 → 坏产物/假"换名失败"/一侧失败清理 `remove` 误删对端在写文件；异参并发可产出**通过头校验的乱码 .baked** 且 mtime 比源新 = 永不重烤。修 = tmp 带 `pid+单调序` 唯一后缀，各写各的、`RenameReplace` 原子换名后写者胜。
3. **`.baked` 头校验 64 位域化 + 1GiB 载荷上限**（`BakedClip.cpp` ParseLbaHead）：`payloadBytes != frameCount*channels*2` 原在 uint32 域做乘法可回绕——构造 frameCount=0x60000000/双声道时截断值恰等于声称 payloadBytes → `resize` ~2GiB 直接 bad_alloc 崩装载路径（"坏数据不崩"纪律破口）。修 = 期望载荷 uint64 域计算 + `kMaxBakedPayloadBytes = 1GiB`（48k 立体声 ≈89 分钟，资产域宽裕）。
4. 附带卫生：分块解码注释"循环到 0"与实际"短读即 EOF"不符（后者与 miniaudio 自家 `ma_decoder_read_pcm_frames_data` 判据一致，行为正确）——注释改为如实口径，防后人照注释修出 bug。

## 回归锁（TestAudioBakedHardening +15 → 33676 checks）

- 手写两种坏头（uint32 回绕 0x60000000 帧 / 无回绕超限恰 2GiB）：Load/Peek 双双拒绝且 outPcm 为空（旧码 = bad_alloc 崩）；
- 双线程并发烤制同一 dst：两侧均成功、产物完整可装载、字段无损；
- 失败烤制不留任何 tmp（唯一后缀名同受失败清理覆盖，目录扫描断言零残留）。

## 复验

- 引擎单测 **33676 checks OK**（33661 → +15）；ctest **3/3**；全量构建零 error。
- svr-test 无头 `--smoke --play`：`进 Play 音频装载：8 成功，全部就绪`（缓存命中零重烤）。
- 基础 `--smoke` 报 `errors=2` FAIL = demo/svr-test 既有孤儿 .meta（源 png 已删）项目卫生问题，与本批无关（改动面仅 Engine/Audio + Tests）。

## review 确认无恙面（二轮新增核实项）

- 热修② 原子写本身正确（防崩溃半截），本次只补并发洞；
- Play 世界每次 EnterPlay 重建（playWorld_ 全新 InstallDefaultSystems）→ AudioChannel/AudioSystem 绑定零跨局残留；StopPlay→ExitPlay 销毁即清场；
- vtable 尾 10 音频槽 C++ 注册序与 C# NativeApi 声明序逐一对齐；
- voiceId 单调不复用 → AudioSystem 绑定表持死 engineVoice 调 SetVoiceParams/Stop = 安全 no-op；
- `BakeStale` 判定/`audioBakePending_` atomic 计数/烤制线程收口序（Run 尾 + 析构双保险）无恙。
