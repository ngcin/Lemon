# M6c 批⓪.5 竖切 —— yami 素材实弹：.baked 资产通道最小子集 + Lemon.Audio 四槽 + svr-test 全事件接音

Status: done（2026-09-30 当日：无头 `--smoke --play` **8/8 音频装载成功**（mp3 112.59s/wav 单双声道/ogg 全过）+ 命中缓存二次跑只重烤失败件 + 引擎单测 33597 checks + ctest 3/3 + 基础编辑器 smoke PASS。真人听感验收待用户进编辑器跑一局。[DevLog](../../DevLog/2026-09-30-m6c-b0-5-audio-vertical-slice.md)）

> **同日 review 热修批**（[DevLog](../../DevLog/2026-09-30-m6c-b0-5-audio-review-hotfix.md)）：四修——①`Audio.Play` GUID 改 TryParse（坏常量不再抛异常崩游戏）；②烤制原子写（tmp + RenameReplace）+ 解码流错误判失败（半截 .baked 曾因 mtime 较新永不重烤）；③`ma_device_start` 失败落静音降级 + 红字（此前 init 成功 start 失败 = 无声无提示）；④引擎侧 Play group 防御钳（越界落 Sfx，此前 groupVol[] 越界读）。附带：AudioEngine.cpp 统一 stb_vorbis 声明先含（三 TU 宏视角一致）。回归锁 +6（坏源不落产物/坏组不崩）→ **33603 checks**、ctest 3/3、svr 热跑 8/8 复验。

> 动因：用户要求把 yami 音频素材拷入 svr-test 实测（"给 Main.scene 中的技能等增加音效"）——把批①（资产通道）与批②（C# API）的最小子集提前竖切。**批①/批② 正式范围不变**（浏览器过滤/图标/试听、meta importer loop 段、AudioSource 组件 + 三档重录、PlayAt 空间化、BGM 淡出、AudioChannel 命令表归批②；竖切四槽语义是其真子集，批② 正式化时原位升级）。

## 分解与勾销

| # | 项 | 落点 | 状态 |
|---|---|---|---|
| T1 | `.baked` 容器读写 | `Engine/Audio/BakedClip.{h,cpp}`：烤制（ma_decoder 目标格式转换 s16/48k/源声道一步完成）→ LBA1 32B 头 + PCM16；装载 = 头校验（魔数/版本/采样率/载荷一致）+ 整读，零解码器 | ✅ |
| T2 | miniaudio Vorbis 修正 | **0.11.25 核定修正：stb_vorbis 非内嵌、须外供**（粘合层守卫 `STB_VORBIS_INCLUDE_STB_VORBIS_H`）——vendor 第四件 `extras/stb_vorbis.c`；实现 TU 换血为 `Engine/Audio/MiniAudioImpl.c`（stb_vorbis 全量 + miniaudio 实现，同 TU 先后序）；消费 TU `STB_VORBIS_HEADER_ONLY` 只取声明保宏视角一致 | ✅（ogg 实测 6.00s 解码通过） |
| T3 | 资产类型数据面 | `AssetType::Audio` 枚举尾加（Generic 前）+ `TypeOf` 四扩展名（.wav/.ogg/.mp3/.flac，D1 拍板口径）+ `AssetTypeName("audio")`；meta 通道零改动（既有 sidecar schema 直用） | ✅ |
| T4 | EnterPlay 装载 | `EditorApp::MountPlayAudio()`（EditorAppScripts.cpp）：扫 Audio 资产 → 缺烤/源新于产物现烤（`.lemon/baked/audio/<guidHex>.baked`）→ 装载注册 → guid→clipId 表；`ResetClips` 防注册表跨局累积；挂 `TryEnterPlay` + `--play` 程序化路径；`StopPlay` → `StopAll` | ✅ |
| T5 | C#/C++ 边界 | `AudioHooks` 四桥（SetAudioHooks 注入，未装 = 0/no-op 降级同 EditorAssetHooks）+ vtable 尾加 4 槽（36→40：audioPlay/audioStop/audioSetGroupVolume/audioStopAll）+ `NativeApi.cs` 尾同步 + `Lemon.SDK/Audio.cs` 门面（PlayOneShot/Play/PlayBgm 单槽/Stop/SetGroupVolume/StopAll） | ✅（金回放零重录：vtable 尾追 + 零 ECS 面，M5 批④ 先例口径） |
| T6 | 编辑器装配 | `EditorApp::audio_` 成员 + gameUi_ 段后 Init（静音降级红字不阻断）+ 主循环 `audio_.Tick(dt)`（bPump 段后） | ✅ |
| T7 | 素材落位 | yami（MIT）8 件 → `demo/svr-test/Assets/Audio/`：bgm.mp3（watery_cave 1.8MB/112.59s）/hit/kill/pickup/levelup/dash/shoot.wav + wave.ogg；**meta 预写固定 GUID**（`6a6d1000000000NN`，模板锚点纪律——C# 常量即写不待首扫） | ✅ |
| T8 | 游戏接音 | PlayerBehaviour 七挂点（怪受击 0.45/击杀 0.6/拾取 0.5/升级 0.7/波次 0.6/冲刺 0.5/主弹发射 0.3——OnSpawn 重构早退序）+ GameFlow.EnterRun → `Audio.PlayBgm`（0.55） | ✅ |
| T9 | 单测 | `TestAudioBakedRoundtrip`：手写 44B RIFF 头合成 44.1k 立体声反相正弦 → 烤制（帧数 4700–4900 断言重采样）→ 装载 → 播放（反相立体声中心声像相消 <1e-2）→ 坏魔数拒绝 | ✅ |

## 实测

- 无头 `--play` Main.scene 600 帧：`进 Play 音频装载：8 成功（现烤 8）` → 二次跑 `8 成功（现烤 1）`（缓存命中，仅补重烤件）。烤制耗时日志：mp3 112.59s / ogg 6.00s / wav 0.18–2.00s 全通过。
- Play 期间音频相关告警 0 条（存量 prefab 碰撞 ×2 + clip-mask 与本批无关）。
- 引擎单测 33597 checks（+12）；ctest 3/3；`--smoke --frames 120` 基础冒烟 PASS（注：`--smoke --play` 组合下 overlay 断言在 Play 态跑必 FAIL——那是旗标组合伪影，回归各步的旗标配对不受影响）。
- script-tests 随 SDK 尾槽扩展复跑绿（register2 尺寸握手：宿主 40 槽，SDK min 拷贝 + 尾零）。

## 实现期发现

1. **miniaudio 0.11.25 Vorbis = 外供件**（批⓪ ADR 写"内嵌"有误）：`MA_NOT_SUPPORTED(-10)` 实测 → zip `extras/stb_vorbis.c`（v1.22 公有领域）vendor 第四件；粘合层守卫要求 stb_vorbis 与 miniaudio 实现**同 TU 先后包含**，`MiniAudioImpl.c` 取代 vendor miniaudio.c 作实现 TU。ADR-015 M1 已修订。
2. `ma_decoder_read_pcm_frames` 0.11.25 签名带 `ma_uint64* pFramesRead` 出参（批⓪ 按旧 API 写的被编译器拦下）；输出声道经 `dec.outputChannels` 字段直读。
3. meta 预写固定 GUID 策略可行：`{guid, type:"audio", hash:0, importedAt:0}` 与 sprite meta 同 schema，Rescan 读优先不覆盖——C# 常量与资产号一次对齐，免去"首扫后回填 GUID"两步。

## 遗留（归批①/批②，不扩本批；2026-09-30 review 后按发现重排）

- 浏览器面（过滤钮/图标/双击试听/tooltip）+ meta importer loop 段（循环点/预载）→ 批①；
- **烤制挪导入期后台线程**（06 §2.2 原设计）：现 EnterPlay 同步烤制实测 ≈230ms（8 件含 112s mp3，冷热对比 3.07s vs 2.84s）——可感知一顿且随资产线性变差 → 批①（EnterPlay 只查缺补）；
- **BGM 流式**（ADR M2 口径 >1MiB ring buffer）：竖切全量入 RAM，watery_cave ≈21.6MB PCM16 常驻；`kStreamThresholdBytes` 现为死常量 → 批①/批② 落地；
- PlayAt 空间化（衰减/声像）+ BGM 交叉淡出（D4）+ AudioSource 组件 id 31 + 三档重录 + AudioChannel 命令表正式化 → 批②；
- **BGM 单槽移引擎侧（C++ 持 guid）**：竖切放在 C# 静态 `_bgm`——脚本热重载换域后静态归零，旧 BGM 循环声部成孤儿（无句柄可停，再 PlayBgm 叠双曲直到退 Play）→ 批② 正式化时一并修；
- 暂停语义接 AudioSystem：现 Time.Scale=0 / 编辑器 Pause 均不停 BGM（声部级暂停待批②）；主弹发射音随射速可能密集（0.3 音量 + Shooter 间隔节流，听感待真人验收定夺是否加每音源冷却）；
- `RegisterClip` 双拷贝（装载 vector → 再 assign，21MB BGM 瞬时 ~43MB 峰值）→ 批① 随流式一并清理；
- 混音面板 + `LEMON_AUDIO=off` 回归步 → 批③。
