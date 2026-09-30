# 2026-09-30 M6c 批⓪.5 竖切：yami 音频实弹进 svr-test（用户驱动的提前竖切）

## 事件

用户要求把 yami-rpg-editor 的音频素材拷入 `demo/svr-test` 实测（"给 Main.scene 中的技能等增加音效"）。批⓪ 只有引擎核无消费面——本竖切把批①（资产通道）与批②（C# API）的**最小子集**提前：`.baked` 烤制/装载 + `Lemon.Audio` 四槽 + 全事件接音。批①/批② 正式范围不变（竖切四槽是批② 七槽的真子集，原位升级）。

## 落地

- **引擎**：`Engine/Audio/BakedClip.{h,cpp}`（烤制 = ma_decoder 目标格式一步转换到 s16/48k；装载 = 头校验零解码）+ `AudioEngine::ResetClips`。
- **边界**：`AudioHooks` 四桥（未装宿主 0/no-op 降级）+ vtable 尾 4 槽（36→40）+ `NativeApi.cs` 尾同步 + `Lemon.SDK/Audio.cs`（PlayOneShot/Play/PlayBgm 单槽/Stop/SetGroupVolume/StopAll）。金回放零重录（vtable 尾追 + 零 ECS 面）。
- **编辑器**：`AssetType::Audio`（TypeOf 四扩展名 + 名称，D1 口径）+ `MountPlayAudio()`（EnterPlay 扫资产 → 缺烤/陈旧现烤 `.lemon/baked/audio/<guidHex>.baked` → guid→clipId；挂交互与 `--play` 两路径；StopPlay 清声部）+ gameUi_ 段后装配/主循环 Tick。
- **素材**（yami MIT，`音乐/` 下 CC BY 4.0 包识别后**未取用**）：`Assets/Audio/` 8 件，meta 预写固定 GUID `6a6d1000000000NN`（模板锚点纪律，C# 常量即写）。
- **接音**：PlayerBehaviour 七挂点（受击/击杀/拾取/升级/波次/冲刺/主弹发射）+ GameFlow.EnterRun 起 BGM（watery_cave.mp3）。

## 实测

- 无头 `--smoke --play` Main.scene：**8/8 装载**（mp3 112.59s / ogg 6.00s / wav 0.18–2.00s 全解）；二次跑命中缓存只补重烤件；Play 期音频告警 0。
- 引擎单测 33597 checks（+TestAudioBakedRoundtrip：44.1k→48k 重采样帧数断言 + 反相立体声相消 + 坏头拒绝）；ctest 3/3；`--smoke --frames 120` 基础冒烟 PASS。

## 发现

1. **miniaudio 0.11.25 Vorbis 是外供件**（批⓪ ADR"内嵌"判断有误）：`MA_NOT_SUPPORTED(-10)` 实测 → 粘合层守卫 `STB_VORBIS_INCLUDE_STB_VORBIS_H` 要求 stb_vorbis 与 miniaudio 实现**同 TU 先后包含**。修 = vendor 第四件 `extras/stb_vorbis.c` + 实现 TU 换血 `Engine/Audio/MiniAudioImpl.c`（消费 TU `STB_VORBIS_HEADER_ONLY` 只取声明，保两 TU 宏视角一致防 ma_decoder 布局漂移）。ADR-015 M1 已修订。
2. `ma_decoder_read_pcm_frames` 0.11.25 签名带 `pFramesRead` 出参；输出声道经 `dec.outputChannels` 读。
3. `--smoke --play` 组合下编辑器 overlay 断言在 Play 态跑必 FAIL（sel/handle=0）——旗标组合伪影，回归各步旗标配对不受影响（基础 `--smoke` 单独跑 PASS 证明）。

## 下一步

真人听感验收（编辑器打开 svr-test，MainMenu 入口进一局）→ 批① 正式化（浏览器过滤/图标/试听 + importer loop 段）。
