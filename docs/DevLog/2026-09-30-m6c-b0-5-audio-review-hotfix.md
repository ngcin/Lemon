# 2026-09-30 M6c 批⓪.5 review 热修批：音频代码全量复查四修 + 回归锁

## 事件

用户要求 review 音频全部代码查未考虑 bug。复查范围 = AudioEngine/BakedClip/MiniAudioImpl/ScriptHost 四桥/编辑器装配/SDK/游戏接线，重点线程、生命周期、边界输入、与 ADR 偏差。结论：无阻断级（崩游戏/坏数据），四项热修 + 三项设计偏差登记。

## 热修（四修，全部当日闭环）

1. **C# GUID 解析防崩**（`Audio.cs`）：`Convert.ToUInt64(hex,16)` 对非 hex 字符抛 FormatException——常量手误 = 游戏崩。改 `ulong.TryParse(HexNumber)`，坏串返回 0 静默降级（SDK 其他入口同口径）。
2. **烤制原子写 + 流错误判失败**（`BakedClip.cpp`）：此前直接 fopen 目标写——烤制途中崩溃留半截 `.baked`，其 mtime 比源新会被 EnterPlay 缓存判定**永不重烤**；且解码中途错误曾静默 break 仍落部分产物。修 = tmp 全量写 + `RenameReplace`（FileOps 助手，Windows 覆盖语义钉死，07 §3.6⑤；WriteFileAtomic 同款纪律）+ 读错误判失败不落盘。
3. **`ma_device_start` 失败落降级**（`AudioEngine.cpp`）：此前 init 成功 start 失败（设备被独占等）= 实际无声但无红字、不落静音标记。修 = 失败 `ma_device_uninit` + Warn + `silent_` 置位（Tick 逻辑推进照常）。
4. **引擎侧 Play group 防御钳**（`PlayLocked`）：越界 group = `groupVol[]` 越界读（UB）。C# 路径上层已钳，此修兜直接 C++ 调用（测试/未来消费者）——越界落 Sfx。
5. 附带卫生：`AudioEngine.cpp` 统一 stb_vorbis 声明先含（三 TU 宏视角一致，杜绝日后在此 TU 用 `ma_decoder` 的布局漂移隐患）。

## 回归锁（+6 → 33603 checks）

- 坏源（垃圾字节 .wav）烤制判失败且**不落任何产物**（含 .tmp 不残留）；
- 越界 group 播放不崩且落 Sfx 组增益（组音量 0.25 实测生效断言）。

## 复验

- 引擎单测 **33603 checks**；ctest 3/3；全量构建零 error。
- svr-test 热跑（缓存命中）`8 成功（现烤 0）`；`--smoke --play` 的 overlay FAIL 为已知旗标组合伪影（Play 态无编辑器 overlay，非回归；零 `[error]` 行、script-spawn OK）。

## review 登记（不热修，入批①/②——见批⓪.5 批文件遗留清单重排）

- **批①**：烤制挪导入期后台线程（EnterPlay 同步烤制实测 ≈230ms：8 件含 112s mp3 冷热对比 3.07s vs 2.84s）；BGM 流式 ring buffer（现全量入 RAM 21.6MB，`kStreamThresholdBytes` 死常量）；RegisterClip 双拷贝清理。
- **批②**：BGM 单槽移引擎侧（C# 静态 `_bgm` 在脚本热重载换域后归零 → 旧 BGM 循环声部成孤儿，再 PlayBgm 叠双曲直到退 Play——TryHotReloadScripts 路径无音频清场实证）；AudioSystem 接 Time.Scale=0 暂停语义。

## 核对无恙面（review 确认项）

线程模型（声部/clip/音量全量同锁：主线程改、回调混音）；析构序（`host_` 先于 `audio_` 销毁 + `ma_device_stop` 同步等回调）；循环区间退化无死循环；金回放零重录口径（零 ECS 面 + vtable 尾追）；静音/设备游标互斥；mtime 缓存判定；meta 预写 GUID 与 Rescan 兼容。
