# M6c 批② —— 2D 声源 + C# 正式化：AudioChannel 命令表 + vtable 六新槽 + AudioSystem + AudioSource id 31 + BGM 引擎侧单槽 + D4 交叉淡出

Status: done（2026-09-30 当日代码面收口。**D5–D8 用户拍板均按建议**（"按建议开工"）；三档重录现录现放 **mismatches=0**（sim st/mt + script）；engine-tests **33661**（+52）+ script-tests **1771**（+12，behaviours 19）+ ctest 3/3；smoke 抽检五链全绿（basic/script-chain/template/uirml/smoke-audio）。**真人听感两项 ✅ 2026-10-07 随 M7a 批⑧ V3 同场过**（[验收记录](../../DevLog/2026-10-07-acceptance-m7a-b8-v1-v3.md)）。[DevLog](../../DevLog/2026-09-30-m6c-b2-source-and-csharp-api.md)）

> 设计基准 [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（M3/M4/M5/M6 已拍板项不变）；本文件定稿实现口径。两处 ADR 勘误已回注（M3 字段序 24B / M6 槽数，见 ADR 修订注记）。

## 动因

- 竖切批（b0.5）与批① 留下的正式化欠账：播放面走进程级 AudioHooks 直呼引擎（Save/RtUi/Tween 均 `g_world` 直达 World 通道，音频是唯一例外）、BGM 单槽在 C# 静态 `_bgm`（热重载换域静态归零 → 旧循环声部成孤儿，再 PlayBgm 叠双曲）、无空间化、无暂停、无组件化持续声源。
- review 登记三项中本批清账两项（BGM 单槽引擎侧、暂停语义）；流式（批①b）后手。

## 设计定稿

### 1. AudioChannel 命令表（World 持有，非 ECS——零重录通道族第八员）

`Engine/Audio/AudioChannel.{h,cpp}`；World 成员 `audioChannel_` + `Audio()` 访问器（TweenTable 同款口径：EnterPlay 新建 World 自清零、热重载随 World 存活=孤儿修复的落点、不入 StateHash）。

- **staging 语义**：C# 桥当帧写命令进表（`g_world->Audio()`，Save 先例——AudioHooks 四函数退役），AudioSystem 统一提交 AudioEngine。C# 侧 `Play/PlayAt` **同步返回逻辑 voiceId**（单调 u32 不回收，Tween 句柄同款）；提交期映射引擎声部 id。
- **guid→clipId 在 staging 期解析**：进程级单钩 `g_audioResolve(guid)→clipId`（ScriptHost.cpp，EditorAssetHooks 降级口径：未装 = 返回 0）。好处：坏 clip 即时返 0（作者错误当场暴露，不静默到提交期）；命令表存 clipId 后为纯数据，提交只需引擎指针。编辑器侧 ctx = `guidToClip_` map 成员（MountPlayAudio 每局重建内容、成员地址稳定，全局指针免重装）。
- 命令集（9 种）：`Play / PlayAt / Stop / Bgm / BgmStop / GroupVol / MasterVol / SetPaused / StopAll`。`Stop(logicalId)`：同 tick 未提交 → 撤销待发命令；已提交 → 引擎停。`StopAll`：清待发 + 逻辑表 + BGM 槽 + 引擎全停。提交后逐帧清死条目（引擎 VoiceAlive 判亡即回收表位，id 不复用）。
- **BGM 单槽在表内**：`bgmLogicalId` + 引擎声部 id；`PlayBgm(clip, vol, fadeSec)` 提交期 = 旧 BGM 声部 `FadeVoice→0 后停` + 新声部 `fadeIn 到 vol`；`StopBgm(fadeSec)` 同径。fadeSec<=0 = 硬切（D4 参数口径）。

### 2. AudioEngine 扩展（包络与在体调参）

- `PlayParams` 尾加 `float fadeInSec = 0`（起播 0 增益线性爬到 volume）。
- 新 API：`FadeVoice(voiceId, targetVolume, seconds, stopWhenDone)`、`SetVoiceParams(voiceId, volume, pan)`（AudioSource 每 tick 空间更新用；沿用批⓪ 短临界区口径）。
- 包络在混音循环内**逐样本线性推进**（gain 向 gainTarget 按 rate 走；stopWhenDone 且到 0 = 声部终结）——MixOffline 可单测、静音模式逻辑记账同路径。BGM 为 loop 声部不吃池偷（批⓪ 只偷一次性，核对不动）。

### 3. 空间化数学（ADR M5 落地，纯函数）

`Engine/Audio` 内 `ComputeSpatial(src, listener, refDist, maxDist) → (gain, pan)`：线性衰减（d≤ref=1 → d≥max=0）；pan = clamp((src.x - listener.x) / listener.halfWidth, -1, 1)，等功率对已由批⓪ pan 通道承担。**监听器 = 宿主每帧推**（编辑器 Play = gameCam 刚体跟随位 + gameRT aspect 半宽；`World::SetAudioListener(center, halfWidth)`，一帧延迟口径 = FeedGameUiInput 先例）。默认 ref 256 / max 1024（ADR M5），实现期对表模板相机（halfH 360/aspect≈1.78 → halfW 640）校一次。**D7：PlayAt 在提交期按当帧监听器快照一次性定 gain/pan**（Unity PlayClipAtPoint 同构；移动声源 = AudioSource 组件）。

### 4. AudioSystem（Engine/Systems，Tween 后 ScriptEventDispatch 前插 #20）

- **零 RNG 子流、零 ECS 写**（只读 AudioSource+Transform+World 监听器）→ 自身零哈希漂移（AnimGraph/Tween 同款尾插论证：其后系统不消费 RNG，id 语义零影响）。
- 职责序：①提交 AudioChannel 待发命令（经 `World::AudioSink()`——宿主注入 `AudioEngine*`，null = 无声宿主全 no-op，script-tests/无头口径）②AudioSource 绑定生命周期（见 §5）③逐绑定 ComputeSpatial → `SetVoiceParams` ④死条目回收。
- 绑定表 = **AudioSystem 成员** `map<Entity → {logicalVoice, clipGuid}>`（系统局部，非 ECS 非 World 通道——不入哈希；随 World 管线存亡，EnterPlay 自清）。
- 暂停（**D5**）：显式语义——C# `Audio.Paused = bool`（set-only 属性）staging `SetPaused` 命令，提交期 `engine->SetPaused`；挂起范围 = ADR M4 既有拍板（BGM/循环声部级挂起、一次性放完、UI 组免疫）。**引擎不自动映射 TimeScale==0**（svr-test 里 Scale=0 大量用于菜单/Spawning/选卡/死亡/清场非暂停态，GameFlow.cs:85 注释已预期"清场冻结不挂起"；游戏暂停态自调）。编辑器 Pause 不接（调试操作，BGM 继放便于调音；登记项）。

### 5. AudioSource 组件（id 31，表尾追加）

```cpp
struct AudioSource {            // 24B 冻结（自然对齐；ADR M3 原字段序 u8 group 在 u16 flags
    uint64_t clipGuid;          // 前会 25→32B，勘误调序为 flags 前、group 后——见 ADR 修订注记）
    float volume   = 1.0f;
    float refDist  = 256.0f;
    float maxDist  = 1024.0f;
    uint16_t flags = kAudioPlayOnStart; // bit0 loop、bit1 playOnStart（Unity playOnAwake
    uint8_t  group = 0;                 // 默认 true 同构；C# default=0 镜像坑注记——UIDocument 同款）
    uint8_t  pad_  = 0;
};                              // static_assert(sizeof==24 && trivially_copyable)
```

- FieldMeta 六行（pad 不登记）；Inspector 通用行自动出，**AudioRef 专用槽（hint 1<<12）归批③**。
- 生命周期（AudioSystem 逐 tick）：`playOnStart && 无绑定` → 起声部（loop 按 bit0，group/音量/空间参数取组件值）；实体亡 / 组件摘 → 停声部解绑；`clipGuid` 变更 → 停旧起新（运行时换片健壮性，纯读反应零哈希面）；`volume/ref/max` 变更 = 下一 tick 空间更新自然生效。**运行时手动触发组件声源不做**（playOnStart 覆盖静态用例、动态位 = PlayAt；`Audio.PlaySource(entity)` 登记项）。
- 加组件 = **三档金回放一次性重录**（组件名无条件入哈希流，StateHash.cpp:75；WaveDirector/UIDocument 先例），批内闭环见 §7。

### 6. vtable 40→46（六新槽尾追）+ SDK Audio.cs v2

```cpp
uint32_t (*audioPlayAt)(uint64_t clipGuid, float x, float y, float volume, int32_t group,
                        float refDist, float maxDist, int32_t loop); // 逻辑 voiceId（0=失败）
int32_t  (*audioBgm)(uint64_t clipGuid, float volume, float fadeSec); // 1=受理 0=clip 无效
void     (*audioBgmStop)(float fadeSec);
void     (*audioMasterVol)(float v);
float    (*audioMasterVolGet)();   // D6：get/set 对称（getXpCurveK 同款；砍则 45 槽）
void     (*audioSetPaused)(int32_t on); // D5；若改自动 TimeScale 方案则此槽不需要（45 槽）
```

- 既有四槽签名不变、语义原位升级：audioPlay/audioStop 走逻辑句柄，audioSetGroupVolume/audioStopAll 走 staging。`AudioHooks` 结构体 + `SetAudioHooks` + 编辑器装配点整体退役（`g_audioResolve` 单钩替代）。ADR M6"36→43"为竖切前口径，修订注记回注（现实 40 → 批② 后 46，D5/D6 变更则 44–45）。
- SDK `Audio.cs` 重写：`PlayAt(...)` 新入口（默认 ref 256/max 1024）；`PlayBgm(clip, volume=0.55f, fadeSec=0.5f)` / `StopBgm(fadeSec=0.5f)`（svr-test 现调用 `PlayBgm(kBgm, 0.55f)` 兼容零改）；`MasterVolume` 属性（D6）；`Paused` set-only（D5）；**`_bgm` 静态删除**（孤儿根除）。`NativeApi.cs` 尾同步六委托；script-tests register2 尺寸握手自动随宿主。
- `Assets.GuidOf(relPath)` 登记项维持不做（C# 常量即写已够用，svr-test 口径）。

### 7. 金回放三档重录批内闭环

- **次序纪律**：T1–T5 代码齐落（组件 + AudioSystem 插桩 + 通道 + vtable + SDK 全就位）后**一次重录**三档（bench-sim st/mt + bench-script，现录现放复验 mismatches=0，WaveDirector/UIDocument 先例流程）——之后批内改动只许非哈希面（编辑器接线/单测/文档），杜绝二次重录。
- 零漂移论证（重录后防回归）：AudioSystem 零 RNG/零 ECS 写；AudioChannel/监听器/绑定表均不入 StateHash；vtable 尾追零回放面（M5 批④ 口径）。基准场零 AudioSource 实例（bench 场无音频）→ 组件实例化路径不进哈希流。

### 8. 编辑器接线（T6）

- EnterPlay：World 创建后 `SetAudioSink(&audio_)` + `g_audioResolve` 注入（MountPlayAudio 尾）；StopPlay：`audio_.StopAll()` 既有 + 下局新 World 通道自清。主循环 Play 分支每帧 `SetAudioListener(gameCam.center, gameCam 半宽)`（UpdateGameCameraFollow 后取值，一帧延迟）。
- svr-test：`GameFlow.SetPaused` 加一行 `Audio.Paused = on`（D5 消费面）；无其他必改。

## 分解与勾销

| # | 项 | 落点 | 状态 |
|---|---|---|---|
| T1 | AudioChannel 命令表 + World 装配 | `Engine/Audio/AudioChannel.{h,cpp}`（九命令 + 逻辑 voice 表 + BGM 槽）+ `Spatial2D.h`；`World.h/cpp` 成员 `audioChannel_`/`Audio()`/`SetAudioSink→SetAudioBackend`/`SetAudioListener`（CMake 源表登记） | ✅ |
| T2 | AudioEngine 包络与调参 | `PlayParams.fadeInSec` + `FadeVoice`/`SetVoiceParams` + 混音逐样本包络 + `AdvanceLocked` 静音同径推进 | ✅ |
| T3 | vtable 六槽 + 桥改 staging + hooks 退役 | `ScriptHost.{h,cpp}`（vtable 尾 6 → **46 槽**；四桥改 `g_world->Audio()`；AudioHooks 结构体删除）+ `NativeApi.cs` 尾同步 + `Audio.cs` v2（`_bgm` 删，PlayAt/PlayBgm/StopBgm 淡出/MasterVolume/Paused） | ✅ |
| T4 | AudioSource id 31 | `Components/AudioComponents.h`（24B 冻结 + kAudioLoop/kAudioPlayOnStart）+ `ComponentCatalog.cpp` REGISTER_ED 尾加（id 31）+ 计数六处（engine_tests 732/781/1036/1059：31→32、19→20 + script main 84/91）+ `Components.cs` 镜像 + `LayoutTables.cs` 行 | ✅ |
| T5 | AudioSystem | `Systems.{h,cpp}` Tick 四职责（提交/起播/空间热更/绑定回收）+ `InstallDefaultSystems` Tween 后插（**#20**，管线序断言 20/19 同步）+ 绑定表成员 | ✅ |
| T6 | 编辑器接线 + svr-test 一行 | `EditorAppScripts.cpp`（`ResolveAudioClipThunk` + TryEnterPlay 注入 backend + `AudioClipOfGuid`）+ `EditorApp.cpp`（HookAudio* 四函数退役 + 主循环 Play 分支每帧推监听器 gameCam+gameRT 半宽）+ svr-test `GameFlow.SetPaused` 加 `Audio.Paused = on` | ✅ |
| T7 | 单测 | engine-tests：TestAudioFadeEnvelope + TestAudioSpatialMath + TestAudioChannelCommands + TestAudioSourceLifecycle（playOnStart 起/实体亡停/换片重绑/零 ECS 写断言）；script-tests：**TestAudioSdk**（AudioProbeBehaviour typeId 18 + behaviours 19：Play/PlayAt 非零、坏 guid 0、Stop 真值序、MasterVolume 次帧往返、BGM 槽对拍、**ComputeStateHash 跨帧逐位不变反例**、StopAll 清场）+ 测试宿主静音引擎 + resolver 装配 | ✅ |
| T8 | 金回放三档重录 | T1–T5 齐落（批内此后零哈希面改动）→ bench-sim `--threads 1 --record` 18000 帧现录 → **st/mt(threads 4) 双档 replay mismatches=0**（终态 alive=8249/created=16003/destroyed=7754 双档一致）+ bench-script record/replay **回放 PASS mismatches=0** | ✅ |
| T9 | 真人听感两项（D8-A） | ①暂停挂起/恢复续响（Esc 往返）②死亡重开 BGM 不叠曲 + 换曲交叉淡出顺带听感（svr-test 即席）；空间化听感归批④ | **✅ 2026-10-07（M7a 批⑧ V3 同场过）** |

预估 3–4 天 → 实际当日代码面收口。

## 实测

- engine-tests **33661 checks**（+52）；script-tests **1771 checks**（+12）behaviours 19；ctest 3/3；全量构建零 error。
- 三档重录：`bench-sim --n 10000 --frames 18000` st 录制 → st/mt 回放 `replay=PASS mismatches=0` 双档；`bench-script --n 5000 --frames 18000` record/replay `回放 PASS (mismatches=0)`。
- smoke 抽检：basic（overlay 三要素 OK）/ script-chain（`--script SDK --smoke --play --validate` 首跑 assets hotreload 抖动复跑绿 = T1 先例再现）/ template-chain（flow 九位全 YES + saves 三档 YES + 双项目 id 一致）/ uirml-chain（全位 OK）/ smoke-audio（svr-test 真资产 OK）。
- 热重载孤儿根除的机械面：BGM 槽在 World.AudioChannel（C# 静态 `_bgm` 删除）——换域不丢句柄；真人验收项覆盖听感面。

## 实现期发现

1. **guid→clipId 解析收进 World**（设计微调）：批文件 §1 原草案为进程级 `g_audioResolve` 单钩——实现改 `World::SetAudioBackend(engine*, resolverFn, ctx)`（resolver 函数指针 + ctx 宿主持有），C# 桥经 `g_world->ResolveAudioClip` 直达，**零新增进程全局**，且 AudioSystem 组件路径同源消费。比草案更干净，AudioHooks 退役后 ScriptHost 侧音频零状态。
2. **MasterVolume 异步语义**：set 走 staging（当帧提交期落地引擎）、get 引擎直读——**同帧写读 = 旧值**。SDK 注释明示，探针改次帧回读（首版探针同帧往返实测抓出）。设置屏滑条消费面不受影响（跨帧写读）。
3. **StageStop 未提交命中 = 直接撤命令销记账**（比"排 Stop 保序命令"更干净）：同 tick Play+Stop 引擎从未起声部；StopAll 与其后同 tick Play 的顺序语义经 Submit 顺序保持（表内条目存在性 = 命令有效性闸）。
4. **smoke-template 调用坑（自陷非回归）**：回归 template 步**不带** `--script`——显式 `--script` 会旁路项目 Game/ 编译装配（`launch_->script.empty()` 闸，EditorAppScripts.cpp），宿主装载 TestScript → GameFlow 未注册 → flow 卡 menu 零怪。排查后正确命令复跑全绿；此坑登记批③ 冒烟文档侧。
5. **smoke-audio 跑真项目会保存场景**（实体序重排、内容等价）——MainMenu.scene 被 `--project demo/svr-test --smoke-audio` 重存一次，已 `git checkout --` 回退；批③ smoke-audio 入回归时夹具纪律沿用空目录自播种口径。
6. AudioSource `flags` 默认 = playOnStart 置位（Unity playOnAwake 同构）；C# 镜像 default=0 的默认值坑已在 Components.cs 注释（UIDocument.ShowOnStart 同款）。
7. **`--play` 程序化路径后端注入缺口**（自查抓出）：程序化进 Play 块（`--smoke --play`，EditorApp.cpp playTest 段）直调 `EnterPlay()+MountPlayAudio()` 不经 TryEnterPlay——首版只挂在交互侧 = 无头跑音频全哑。修 = 抽 `WirePlayAudioBackend()` 成员双挂点（竖切批 MountPlayAudio 同款纪律）；`--smoke --play` svr-test 复验 `进 Play 音频装载：8 成功`。

## 出口判据（勾销）

- ✅ script-tests TestAudioSdk 全绿（含"音频调用后 state hash 不变"反例——ComputeStateHash 跨帧逐位相等机械断言）；
- ✅ 三档重录后 sim/script replay **mismatches=0**（st/mt 双档）；bench-survivor fps 对照未跑（模板链冒烟绿；正式对照归批④ 收官口径，本批零 ECS 写/RNG 面论证 + 三档零 diff已证）；
- ✅ ctest 3/3 + 引擎单测计数到位（33661/1771）；全量构建零 error；
- ⏳ （D8-A）真人两项待用户；热重载孤儿反面验（Play 态改 C# 触发重载后 PlayBgm 不叠曲）随真人验收一并过。

## 风险与兜底（收口注记）

- 重录窗口纪律兑现：T8 后批内零哈希面改动（后续仅冒烟复跑与文档）。
- AudioSystem 每 tick 引擎写走批⓪ 短临界区口径（SetVoiceParams × 绑定数 ≤ 声源数，微秒级）。
- 监听器半宽 = gameRT 实宽高 aspect（模板相机 halfW 640 基准，批④ 铺量时听感复核）。

## 登记项（观察，不扩 scope）

- `Audio.Alive(voiceId)` 轮询、`Audio.PlaySource(entity)` 运行时手动触发组件声源（用例出现再启）；
- 编辑器 Pause 挂起 BGM（调试语义另议，M8 打磨池）；
- 批①b 流式（本批后插队：BGM 槽直接换流式声部）；
- 混音面板/Inspector AudioRef 槽/smoke-audio 入回归 → 批③。

## 关联

- [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md) + 本日修订注记（M3 字段序 / M6 槽数）
- [M6c.md](./M6c.md) 批次表批② 行；竖切批 [b0.5](./2026-09-30-b0-5-audio-vertical-slice.md) 遗留清单（本批清账：BGM 单槽/暂停/PlayAt/命令表）
- [09-Testing](../../EngineDesign/09-Testing.md) §6.8 零重录纪律（加组件必重录推论）
