# M6c 实施计划 —— 音频系统（miniaudio + 2D 声源 + `.baked` 音频类型，2026-09-30 开工）

Status: code-complete（批⓪–批④ 全部落码 2026-10-01，里程碑代码面收口；余真人验收清单六条（批④ 批文件文末，批② 两项同场）。**批④ done 2026-10-01**：模板全程有声——CC0 素材包 `Samples/Assets/cc0-audio/`（Kenney 六件 + OGA BGM 1.4MB ogg 流式，来源登记三处）+ 模板接音（开局 BGM/回菜单淡出/四事件音/UI 组按钮音 + `Audio.Paused`）+ 设置屏音量四滑条（**Change 事件通道首用**，同值早退防自回环；模板/svr-test 双落）+ smoke-template `aud(mount=7 pause=5 resume=6)` 位（暂停语义机器面）；bench-survivor **fps=82 ≥ 78 零降级**；回归 16/17 + smoke-ui 复跑绿先例；附带修 uirml 冒烟 watcher 竞速（既有缺陷，双提交位归因）——[批文件](./2026-10-01-b4-template-audible.md)、[DevLog](../../DevLog/2026-10-01-m6c-b4-template-audible.md)。此前各批：设计定稿 2026-09-30：[ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)——D1–D4 当日用户拍板均按建议（D1 放开 mp3/flac / D2 `.lemon/baked/audio/` / D3 固定三组 / D4 0.5s 淡出），miniaudio **0.11.25** 用户手备 zip vendor 引入。**批⓪ done 2026-09-30**（[批文件](./2026-09-30-b0-engine-core.md)：Engine/Audio 骨架 + 静音降级 + 混音单测；ctest 3/3 + 33585 checks + **100 并发 SFX 模拟侧 0.0092ms** + 金回放零影响；[DevLog](../../DevLog/2026-09-30-m6c-b0-audio-engine-core.md)）。同日**批⓪.5 竖切 done**（[批文件](./2026-09-30-b0-5-audio-vertical-slice.md)：yami 素材实弹——.baked 通道最小子集 + Lemon.Audio 四槽 + svr-test 全事件接音，无头 8/8 装载 + 单测 33597 + ctest 3/3；真人听感待验收）。**批① done 2026-09-30 同日**（[批文件](./2026-09-30-b1-baked-asset-channel.md)：浏览器五处 + 双击试听（Edit 态可响）+ importer 循环点/预载 + **后台烤制**（Rescan/开项目双钩位——EnterPlay 21.9ms→7.1ms）+ RegisterClip 移动重载 + smoke-audio 第一段 OK + 单测 33609；**BGM 流式拆出批①b**（线程正确性需专注批）；[DevLog](../../DevLog/2026-09-30-m6c-b1-baked-asset-channel.md)）。**批② done 2026-09-30 同日**（[批文件](./2026-09-30-b2-source-and-csharp-api.md)：AudioChannel 命令表/vtable 40→46/AudioSystem #20/AudioSource id 31/BGM 引擎侧单槽/D4 引擎包络淡出；**D5–D8 用户拍板均按建议**——D5 显式 Audio.Paused、D6 加 masterVolGet 槽、D7 PlayAt 静态快照、D8 含轻量真人两项；三档重录 st/mt/script **mismatches=0** + 单测 33661/1771 + smoke 五链绿；**真人两项待用户**；[DevLog](../../DevLog/2026-09-30-m6c-b2-source-and-csharp-api.md)））。**批①b done 2026-10-01**（[批文件](./2026-10-01-b1b-bgm-streaming.md)：SPSC 环 + 声部级 StreamFeed + 专用填充线程（回卷换位在生产者侧——环内线性化帧流）+ 装载分流（>1MiB 未 preload 走流式）+ 预填整环保首回调零欠载；单测 34008（+332 环序/流式链）+ ctest 3/3 + svr-test `8 成功（流式 2）` 零欠载；[DevLog](../../DevLog/2026-10-01-m6c-b1b-bgm-streaming.md)）。**同日听感验收驱动热修 done**（[DevLog](../../DevLog/2026-10-01-m6c-master-limiter-and-clip-voice-cap.md)：用户报 kill.wav 多发叠加破音 → 母带软限幅（膝点 0.8/tanh/L-R 联动）+ 同 clip 并发上限 `kMaxVoicesPerClip=4` 偷最老 5ms 释放——ADR-015 M4 实现注记同步；单测 34023；svr-test `script-spawn` FAIL 为用户 WIP 层既有（基线复现，SpawnerBehaviour 未注册）非回归））。**同日第二轮热修 done**（[DevLog](../../DevLog/2026-10-01-m6c-retrigger-throttle-and-pitch-jitter.md)：破音消除后 pickup 密集"放鞭炮"= 机枪效应 → 重触发节流（`SetRetriggerCooldown` 默认 45ms，混音帧域时钟）+ 音高微扰（`SetPitchJitter` 默认 ±2%，Sfx/Ui 非循环整载；1-tap lerp，rate==1 位精确路径不动）；单测 34036 + ctest 3/3；per-资产覆写进批③ 候选；**真人复听 2026-10-01 暂告段落（用户拍板"暂时这样"）**）。**批③ done 2026-10-01**：编辑器收口——Inspector AudioRef 槽（hint 1<<12 + DrawGuidSlot 复用）+ AudioSource 字段行（refDist/maxDist range + group enum）+ **Audio Mixer 按需工具窗**（05 §3 冻结旁路形态，不进面板注册表：Master/三组音量 + voice 计数 + 重触发节流/音高微扰全局调参面；两轮听感热修参数的试验台）+ smoke-audio 全链第二段（AudioSource playOnStart `voices=1/0` 断言，静音/设备双环境同过；夹具自播种 wav——真项目零播种）+ 回归 full **16→17 步**（audio-chain 步以 `LEMON_AUDIO=off` 跑 = "off 无头同绿"判据的机器面）；附带修 = 后台烤制目录建目录时序缺口（新项目首导入即后台烤早于首 EnterPlay，worker 落盘"产物不可写"红字——EnqueueAudioBake 入队侧幂等补建）；per-资产音频参数**缓议**（见登记项），[批文件](./2026-10-01-b3-editor-closeout.md)、[DevLog](../../DevLog/2026-10-01-m6c-b3-editor-closeout.md)）

> 里程碑总览页惯例（M6b 同款）：每批一个文件，开工前分解到文件/行级，完工后批文件内勾销；事件流水与实测数字记 [DevLog](../../DevLog/)。设计定形物在 [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（M1–M7 决策 + D1–D4 拍板项），本页只做拆解与验收映射，不重复设计内容。

## 并行依赖

- **上游**：M6b 已收官（③d-2 + 真人验收 2026-09-30）——暂停 bit6、Settings 档（`settings.sav`）、UIDocument 屏幕栈都是音频的消费面。
- **消费者**：`demo/svr-test`（用户幸存者游戏，批④ 全程有声实装）+ `Samples/vs-survivor` 模板（同一通道）+ **M7a packager（`.baked` 容器只读消费，类型必须在此定形）**。
- **下游预留**：M9 TD 模板吃同一 API（波次/摆塔/UI 音就绪即用，零引擎改动）。

## 批次表

| 批 | 文件（开工日落） | 主题 | 预估 | 出口判据 |
|---|---|---|---|---|
| 批⓪ | [2026-09-30-b0-engine-core](./2026-09-30-b0-engine-core.md) | 引擎骨架：~~CPM~~ **vendor** miniaudio 0.11.25 + `Engine/Audio`（AudioEngine/voice 池/组增益）+ 静音降级 + 混音单测 | 2–3 天 | **done 2026-09-30 当日**（CTest 3/3 + 33585 checks；100 并发实测 0.0092ms；Vorbis 内建确认；金回放零影响） |
| 批⓪.5 | [2026-09-30-b0-5-audio-vertical-slice](./2026-09-30-b0-5-audio-vertical-slice.md) | 竖切：yami 素材实弹（用户驱动）——BakedClip 烤/装 + AssetType::Audio 数据面 + vtable 四槽 + Lemon.Audio + svr-test 接音；**批①/② 正式范围不变** | 当日 | **done 2026-09-30**（无头 8/8 装载；miniaudio Vorbis 外供修正；真人听感待验收） |
| 批① | [2026-09-30-b1-baked-asset-channel](./2026-09-30-b1-baked-asset-channel.md) | `.baked` 资产通道正式化：浏览器五处 + 双击试听（Edit 态）+ meta importer（loop/preload）+ **烤制挪导入期后台线程**（EnterPlay 21.9→7.1ms，review 登记项清账）+ RegisterClip 移动重载 + smoke-audio 第一段 | ~3 天 | **done 2026-09-30 当日**：smoke-audio 第一段绿 `entries=8 baked=8 meta=8 preview=1/1`；hash 变更自动重烤（Rescan 增量钩位）；烤失败红字响亮；06 §2.2 双行收敛 + ADR-015 M2 实测对表（竖切批已落）；流式拆出批①b ↓ |
| 批①b | [2026-10-01-b1b-bgm-streaming](./2026-10-01-b1b-bgm-streaming.md) | BGM 流式 ring buffer（ADR M2 口径 >1MiB；SPSC 环 + 回调消费 + 回卷换位）+ `audioPreload` 位消费 | ~1 天 | **done 2026-10-01 当日**：出口三判据全过——>1MiB 走流式（svr-test 21.6MB BGM + 1.15MB 环境音 = `流式 2`，RAM 常驻 256KiB 环/声部）；零欠载（StreamUnderrunFrames 观测 + Tick Warn 钩）；SPSC 环序锁（双线程 4MiB 字节精确）+332 checks → 34008 |
| 批② | [2026-09-30-b2-source-and-csharp-api](./2026-09-30-b2-source-and-csharp-api.md) | 2D 声源 + C#：AudioChannel 命令表（零哈希）+ vtable 尾加 6 槽（40→46，竖切 4 槽原位升级语义；ADR M6"7 槽/43"系竖切前口径已勘误）+ AudioSystem（Tween 后插、零 RNG/零 ECS 写；暂停 = **显式 `Audio.Paused`，引擎不自动映射 TimeScale**——D5 拍板）+ AudioSource 组件 id 31（计数六处 + SDK 镜像）+ **金回放三档重录批内闭环** + `Lemon.Audio` + **BGM 单槽移引擎侧**（竖切 C# 静态 `_bgm` 热重载孤儿声部，review 登记）+ 交叉淡出（D4，引擎声部包络） | 3–4 天 | **done 2026-09-30 当日代码面**（D5–D8 用户拍板均按建议；三档重录 st/mt/script **mismatches=0**；engine-tests 33661 + script-tests 1771（TestAudioSdk 含 hash 不变反例）+ ctest 3/3；smoke 抽检五链绿；**真人两项待用户**；[DevLog](../../DevLog/2026-09-30-m6c-b2-source-and-csharp-api.md)） |
| 批③ | [2026-10-01-b3-editor-closeout](./2026-10-01-b3-editor-closeout.md) | 编辑器收口：Inspector AudioRef 槽（hint 1<<12 + DrawGuidSlot 复用）+ AudioSource 全字段行 + EnterPlay playOnStart 自动开播 + Audio Mixer 面板（Master/三组/voice 计数）+ smoke-audio 全链 + 回归步入库 | 2–3 天 | **done 2026-10-01**（playOnStart 系统侧批② 已实现——本批补 `voices=1/0` 双环境断言；Mixer 走 05 §3 冻结旁路工具窗形态；回归 full **17 步**（audio-chain @ LEMON_AUDIO=off；首跑 smoke-ui 抖动复跑绿 = 既有先例）；附带修后台烤制目录时序缺口；**提交前 review 三修（同日）**：真项目存档回写面 WARN 交底（ExitPlay 兜底三档 + Game/ 脚本装配，svr-test 两轮实证键值恒等）+ 裸 --smoke-audio fail-fast + AudioSource 元数据单测抽查锁；per-资产参数缓议进登记项） |
| 批④ | [2026-10-01-b4-template-audible](./2026-10-01-b4-template-audible.md) | 模板全程有声：CC0 素材入库（来源登记）+ svr-test 接音（BGM/命中/击杀/拾取/升级/波次横幅 + UI 组按钮音）+ 设置屏音量滑条（Settings 档持久化）+ 暂停语义实测 | 2–3 天 | **done 2026-10-01**（开工考古：svr-test 接音主体已由批⓪.5 落、欠账在模板——本批补齐模板七件 + svr-test UI 音/滑条；BGM 终裁 = OGA CC0 chiptune 1.4MB ogg 流式、程序化回退未启用；smoke-template 扩 `aud(mount=7 pause=5 resume=6)` 位 = 暂停语义机器面；**fps=82 ≥ 基线 78 零降级**；回归 16/17 + smoke-ui 复跑绿既有先例；附带修 uirml 冒烟 watcher 竞速（既有缺陷暴露，双提交位归因）+ 嵌套静态类别名编译错；真人验收清单六条待用户——批② 两项同场） |

合计 12–16 工作日 ≈ **2–3 周**（08 §2 口径）。

批次顺序理由：⓪ 无消费者先行（设备/混音/降级底座，风险隔离在最小批）；① 资产通道次之（`.baked` 定形是本里程碑对 M7 的承诺，早落早对表）；② 才接 C#（命令面 + 组件 + 重录一次性闭环）；③ 编辑器面收口后再 ④ 铺内容（素材/模板/真人验收）——音频链路完整前不铺量，避免返工重录素材接线。

## 出口判据映射（08 §2 M6c 行 → 批）

| 08 判据 | 落点 |
|---|---|
| VS/TD 模板全程有声（命中/击杀/拾取/升级/BGM/波次横幅） | 批④（vs-survivor/svr-test 实装；TD=M9 同 API 零改动消费——判据按模板线落账） |
| 100 并发 SFX 模拟侧 ≤ 0.5ms（解码不占主线程） | 批⓪（解码在烤制期，主线程零解码 = 架构保证；0.5ms 断言 = staging + 空间化实测） |
| 无音频设备/静音输出不崩（无头 CI 可跑） | 批⓪ 静音降级 + 批③ `LEMON_AUDIO=off` 回归覆盖 |
| §M6c 段：模板一局全程有声 + 无音频设备不崩 | 批④ 终验 |

## 范围边界（不做，08 §4 砍单一致 + ADR-015 M5）

DSP 图/中间件（FMOD/Wwise）/3D 空间化/变调滤波混响/用户自定义总线/录制/beat 同步/音频可视化/AudioFinished 事件（`Events.cs` 16 槽余量不消耗）/设备热恢复完整支持（降级复用静音路径，M8 再议）。

## 风险与兜底

- **miniaudio 拉取网络**（本机代理时开时关）：CPM 失败 → 手动 vendor `Engine/Audio/thirdparty/` + THIRD_PARTY 登记改号（ADR-015 M1，接口面不变）。
- **Vorbis 内建不确定性**：批⓪ 首项核定；未内建 → miniaudio 仓 `extras/stb_vorbis.c`（零新增第三方）。
- **金回放重录窗口**：批② 当天完成"加组件 → 重录三档 → 全绿"闭环，窗口期不合入无关改动（UIDocument 先例流程）。
- **混音线程竞争**：小临界区（每帧提交/每块取走各一锁）；TSAN 进 09 登记项，CI 后手不阻塞。
- **0.5ms 断言的 CI 抖动**：断言只测 staging + 空间化（微秒级），不含设备路径；CI 超限先看 runner 负载复跑（drag/ui 负载抖动复跑绿 = 既有先例）。

## 登记项（观察，不扩 scope）

- **per-资产音频参数**（重触发节流窗 / 同 clip 并发上限 / 音高微扰幅度，meta 覆写）——批③ 裁定**缓议**：真人复听已接受全局默认；跨 meta schema + 引擎 per-clip 参数 + Inspector meta 编辑三段 = 独立竖切微批，svr-test 出现真实调参需求时启（Mixer 工具窗全局滑条 = 过渡试验台）；
- `Assets.GuidOf(relPath)`（C# 侧路径→GUID 反查，批② 核定顺手补否）；
- `Audio.Alive(voiceId)` 轮询（无事件回调用例出现再启）；
- 设备热插拔完整恢复（M8 打磨期）；
- mp3/flac 源格式（ADR-015 D1 拍板若"暂不"，此后手一行即开）。

## 关联

- [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（M1–M7 决策 + D1–D4 拍板项 + `.baked` 容器字节表）
- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M6c 行 + §M6c 段（排序约束：先于 M7a）
- [06-Asset-Pipeline](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §2.2 音频行（批① 收敛）
- [M7a](../M7/M7.md)（`.baked` 全类型 packager——容器 v1 的下游消费者）
