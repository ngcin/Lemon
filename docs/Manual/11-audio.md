# 音频

底层 miniaudio，但资产在打包/导入期已**预解码为 PCM16**（`.baked` 容器）——
运行时零解码器，装载即播。

## 两种用法

- **AudioSource 组件**：挂实体上的循环/环境声（带空间化——监听器在玩家）。
- **`Lemon.Audio` 门面**：一次性触发音效、BGM、全局控制——玩法事件音的标准入口。

## AudioSource 组件

| 字段 | 语义 |
|---|---|
| `clipGuid` | 音频资产槽（.wav/.ogg/.mp3/.flac；0 = 静默） |
| `volume` | 0–4 |
| `refDist` / `maxDist` | 空间衰减（≤refDist 全增益，线性衰减到 maxDist 归零） |
| `flags` | bit0 循环 · bit1 进 Play 自动起播（默认开） |
| `group` | 混音组：Bgm / Sfx / Ui |

移动声源用组件（每帧跟随）；静态触发音用 `Audio.PlayAt` 快照定位。

## Audio 门面

| API | 语义 |
|---|---|
| `Audio.Play(guidHex, volume, group, pan, loop)` | 播放；返回 voiceId（0 = 失败：未装载/坏 GUID） |
| `Audio.PlayOneShot(guidHex, volume)` | 一次性音效（Sfx 组）——命中/拾取/技能音标准入口 |
| `Audio.PlayAt(guidHex, worldPos, ...)` | 2D 空间播放（触发时快照定衰减/声像） |
| `Audio.PlayBgm(guidHex, volume, fadeSec)` | BGM 单槽换曲（交叉淡出；槽在引擎侧，热重载不叠曲） |
| `Audio.StopBgm(fadeSec)` / `Audio.Stop(voiceId)` / `Audio.StopAll()` | 停 |
| `Audio.SetGroupVolume(group, v)` / `Audio.MasterVolume` | 组音量 0..1 / 主音量（设置屏消费） |
| `Audio.Paused` | 显式暂停（true = 循环声源/BGM 挂起；一次性放完、Ui 组免疫） |

clip 引用 = **资产 GUID 十六进制串**（AssetBrowser / manifest 可查）。

## 语义要点

- **引擎不自动映射 TimeScale**：`Time.Scale = 0`（菜单/选卡冻结）不会误停 BGM——
  要停自己调 `Audio.Paused`。这是刻意的流程冻结防线。
- 循环区间 = 烘焙期 loop 点（`.meta` importer 声明），不是播放期参数。
- 音频状态不入状态哈希——回放/性能口径与音频互不干扰。
- BGM 音量惯例 0.55 起（模板口径）。

> 设计依据：[ADR-015 音频系统与烘焙格式](../ADR/ADR-015-Audio-System-And-Baked-Format.md)；
> SDK 本体 = `Lemon.SDK/Audio.cs`。
