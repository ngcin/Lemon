# M6c 批② — 2D 声源 + C# 正式化：AudioChannel 命令表 + vtable 46 槽 + AudioSystem #20 + AudioSource id 31 + BGM 引擎侧单槽 + D4 交叉淡出

- 日期：2026-09-30（当日设计稿 + 当日代码面收口）
- 批文件：[Plans/M6c/2026-09-30-b2-source-and-csharp-api](../Plans/M6c/2026-09-30-b2-source-and-csharp-api.md)（D5–D8 用户拍板均按建议）
- 关联：[ADR-015](../ADR/ADR-015-Audio-System-And-Baked-Format.md)（M3 字段序/M4 触发源/M6 槽数三处修订注记）

## 落地物

| 层 | 物 |
|---|---|
| 命令通道 | `Engine/Audio/AudioChannel.{h,cpp}`（World 持有非 ECS，零重录通道族第八员）：九命令 staging/逻辑 voiceId 单调不回收/Stop 未提交撤销与已提交保序停/BGM 单槽换曲/StopAll 僵尸播放防线/提交期死条目回收；`Spatial2D.h`（线性衰减 + 半宽声像，max<=ref 退化全程可闻） |
| 引擎 | AudioEngine：`PlayParams.fadeInSec` + `FadeVoice(target, sec, stopWhenDone)` + `SetVoiceParams` + 混音逐样本包络（静音模式 AdvanceLocked 同径推进 = 逻辑记账与有声一致） |
| 系统 | `AudioSystem`（管线 #20，Tween 后/事件派发前；零 RNG/零 ECS 写）：命令统一提交 + AudioSource 绑定生命周期（playOnStart 起一次/实体亡停/换片重绑）+ 逐 tick 监听器空间热更 |
| 组件 | `AudioSource` id 31（24B 冻结，字段序 = ADR 勘误后口径；计数六处 31→32、系统 19→20）+ C# 镜像 + LayoutTables |
| 边界 | vtable 40→**46**（playAt/bgm/bgmStop/masterVol/masterVolGet/setPaused；竖切四槽原位升级语义）；**AudioHooks 退役**——四桥改 `g_world->Audio()` staging（Save 同款窗口约定），guid→clipId 经 `World::SetAudioBackend(engine, resolver, ctx)`（零新增进程全局） |
| SDK | `Lemon.Audio` v2：`PlayAt`（D7 静态快照）/`PlayBgm(clip, vol, fadeSec=0.5)`/`StopBgm(fadeSec)`/`MasterVolume`（D6）/`Paused`（D5 显式，引擎不自动映射 TimeScale）——**C# 静态 `_bgm` 删除 = 热重载孤儿根除** |
| 编辑器 | TryEnterPlay 注入 backend（ctx = audioClips_ map）+ 主循环 Play 分支每帧推监听器（gameCam + gameRT 半宽，一帧延迟口径）；svr-test `SetPaused` 加 `Audio.Paused = on` |

## 实测

- **三档重录零 diff**：bench-sim `--n 10000 --frames 18000` st 录制 → st/mt(threads 4) 双档 `replay=PASS mismatches=0`（终态 alive=8249/created=16003/destroyed=7754 双档一致）；bench-script record/replay `回放 PASS (mismatches=0)`。重录次序纪律兑现：T1–T5 齐落后一次重录，此后批内零哈希面改动。
- engine-tests **33661**（+52：FadeEnvelope/SpatialMath/ChannelCommands/SourceLifecycle 四件）；script-tests **1771**（+12：TestAudioSdk + AudioProbeBehaviour typeId 18，behaviours 19）；ctest 3/3；全量构建零 error。
- smoke 抽检五链全绿：basic / script-chain（首跑 assets hotreload 抖动复跑绿，T1 先例）/ template-chain（flow 九位全 YES）/ uirml / smoke-audio（svr-test 真资产）。

## 实现期要点（详见批文件"实现期发现"六条）

1. **guid 解析收进 World**（较批文件草案的进程级单钩更干净：`SetAudioBackend(resolverFn, ctx)`，C# 桥与 AudioSystem 组件路径同源）。
2. **MasterVolume 异步语义**：set staging/当帧提交期落地，get 引擎直读——同帧写读 = 旧值（探针首版同帧往返实测抓出，改次帧回读；SDK 注释明示）。
3. **StageStop 未提交命中 = 撤命令销记账**（引擎从未起声部）；StopAll 与同 tick 后续 Play 顺序语义经提交序保持。
4. **smoke-template 自陷**：误带 `--script` 旁路项目 Game/ 装配（`launch_->script.empty()` 闸）→ GameFlow 未注册假故障；回归 template 步本就不带该参，正确命令全绿。坑登记批③。
5. smoke-audio 跑真项目会重存场景（实体序重排内容等价）——已回退；批③ 夹具纪律沿用空目录自播种。
6. **`--play` 程序化路径后端注入缺口**（自查抓出）：程序化块不经 TryEnterPlay，首版注入漏挂 = 无头跑全哑；修 = `WirePlayAudioBackend()` 双挂点，`--smoke --play` svr-test 复验 8/8 装载。

## 遗留

- **真人听感两项待用户**（①暂停挂起/恢复续响 ②死亡重开 BGM 不叠曲 + 交叉淡出听感；空间化听感归批④）；
- 批①b 流式（下一批：BGM 槽直接插流式声部）→ 批③ 编辑器收口（Inspector AudioRef 槽/Mixer 面板/smoke-audio 入回归 17 步）→ 批④ 模板全程有声。
