# M6c 实施计划 —— 音频系统（miniaudio + 2D 声源 + `.baked` 音频类型，2026-09-30 开工）

Status: in-progress（设计定稿 2026-09-30：[ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)——D1–D4 当日用户拍板均按建议（D1 放开 mp3/flac / D2 `.lemon/baked/audio/` / D3 固定三组 / D4 0.5s 淡出），miniaudio **0.11.25** 用户手备 zip vendor 引入。**批⓪ done 2026-09-30**（[批文件](./2026-09-30-b0-engine-core.md)：Engine/Audio 骨架 + 静音降级 + 混音单测；ctest 3/3 + 33585 checks + **100 并发 SFX 模拟侧 0.0092ms** + 金回放零影响；[DevLog](../../DevLog/2026-09-30-m6c-b0-audio-engine-core.md)）。同日**批⓪.5 竖切 done**（[批文件](./2026-09-30-b0-5-audio-vertical-slice.md)：yami 素材实弹——.baked 通道最小子集 + Lemon.Audio 四槽 + svr-test 全事件接音，无头 8/8 装载 + 单测 33597 + ctest 3/3；真人听感待验收）。下一步批① 资产通道正式化（浏览器面 + importer loop 段））

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
| 批① | `YYYY-MM-DD-b1-baked-asset-channel.md` | `.baked` 资产通道正式化：AssetType::Audio 八处入库（竖切已落数据面三处，余浏览器五处）+ meta importer（loop/preload）+ **烤制挪导入期后台线程**（竖切 EnterPlay 同步 ≈230ms，review 登记）+ **BGM 流式 ring buffer**（竖切全量入 RAM 21.6MB，ADR M2 口径）+ 浏览器过滤/图标/双击试听 | ~3 天 | smoke-audio 第一段绿（导入→烤→meta→试听 toggle）；hash 变更自动重烤；烤失败红字响亮；06 §2.2 音频双行收敛 + ADR-015 M2 格式实测对表 |
| 批② | `YYYY-MM-DD-b2-source-and-csharp-api.md` | 2D 声源 + C#：AudioChannel 命令表（零哈希）+ vtable 尾加 7 槽（竖切 4 槽原位升级）+ AudioSystem（Tween 后插、零 RNG/零 ECS 写；接 Time.Scale=0 暂停语义）+ AudioSource 组件 id 31（计数 31→32 四处 + SDK 镜像）+ **金回放三档重录批内闭环** + `Lemon.Audio` + **BGM 单槽移引擎侧**（竖切 C# 静态 `_bgm` 热重载孤儿声部，review 登记）+ 交叉淡出（D4） | 3–4 天 | script-tests 新增 TestAudioSdk 全绿（含"音频调用后 state hash 不变"反例）；三档重录后 bench-sim/script replay 零 diff；ctest 绿 |
| 批③ | `YYYY-MM-DD-b3-editor-closeout.md` | 编辑器收口：Inspector AudioRef 槽（hint 1<<12 + DrawGuidSlot 复用）+ AudioSource 全字段行 + EnterPlay playOnStart 自动开播 + Audio Mixer 面板（Master/三组/voice 计数）+ smoke-audio 全链 + 回归步入库 | 2–3 天 | smoke-audio 全链 OK（静音模式下逻辑 voice 计数断言）；**回归 full 16→17 步**（audio-chain）；`LEMON_AUDIO=off` 无头环境同绿 |
| 批④ | `YYYY-MM-DD-b4-template-audible.md` | 模板全程有声：CC0 素材入库（来源登记）+ svr-test 接音（BGM/命中/击杀/拾取/升级/波次横幅 + UI 组按钮音）+ 设置屏音量滑条（Settings 档持久化）+ 暂停语义实测 | 2–3 天 | 08 §2 M6c 行四判据全勾；bench-survivor fps 零降级（对照 M6b 收官 fps=78）；真人验收清单过（一局全程有声/滑条生效/无设备不崩） |

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

- `Assets.GuidOf(relPath)`（C# 侧路径→GUID 反查，批② 核定顺手补否）；
- `Audio.Alive(voiceId)` 轮询（无事件回调用例出现再启）；
- 设备热插拔完整恢复（M8 打磨期）；
- mp3/flac 源格式（ADR-015 D1 拍板若"暂不"，此后手一行即开）。

## 关联

- [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（M1–M7 决策 + D1–D4 拍板项 + `.baked` 容器字节表）
- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M6c 行 + §M6c 段（排序约束：先于 M7a）
- [06-Asset-Pipeline](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §2.2 音频行（批① 收敛）
- [M7a](../M7/M7.md)（`.baked` 全类型 packager——容器 v1 的下游消费者）
