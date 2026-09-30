# 2026-09-30 M6c 音频开工：代码 push + 设计定稿（ADR-015 + 五批拆解）

## 事件

1. **代码 push**：本地 41 commits 推上远端（`0825961..620657c main -> main`，含 M6b ③d-2 铺量批、真人验收清账、demo 三工程入库、M7 批⓪ 缺陷修复/CI workflow/win preset）。工作树干净，无未提交改动。
2. **M6c 开工设计定稿**，三件产出：
   - [ADR-015](../ADR/ADR-015-Audio-System-And-Baked-Format.md)：miniaudio 引入（CPM + 网络 vendor 兜底）、**`.baked` 音频容器 v1 字节级定形**（32B 头 / PCM16 / 48k 归一 / 运行时零解码器 / 落位 `.lemon/baked/audio/`）、播放面双轨（AudioChannel 零重录 + AudioSource 组件 id 31 一次性三档重录）、混音线程模型（64 voice / Master+BGM/SFX/UI 三组 / 暂停声部级挂起 UI 组例外）、静音降级一等公民（`LEMON_AUDIO=off`）、vtable 尾加 7 槽 + `Lemon.Audio`。
   - [Plans/M6c/M6c.md](../Plans/M6c/M6c.md)：批⓪–批④ 五批拆解（12–16 工作日 ≈ 2–3 周），08 §2 判据逐条映射到批。
   - 06 §2.2 导入器表音频**双行收敛为一行**（原表一行 2026-09-19 老口径 + 一行过时 M6b 改号并存，属文档漂移）。

## 开工前事实盘点（设计依据，均当日核对）

- `.baked` 全仓零存量（磁盘无文件）——音频是该后缀**首个真实落地类型**，M7a packager 依赖此定形（08 §M6c 排序约束）。
- 引擎侧零音频代码；SDL_audio 未用（SDL 仅窗口/输入）；01 选型表 miniaudio 既定。
- 金回放纪律：组件名无条件入哈希流（`StateHash.cpp:75`）→ AudioSource（id 31）必然三档重录（UIDocument 先例）；World 持有通道零重录（Tween 先例）——双轨切分由此定。
- 组件 id 下一号 = 31（`ComponentCatalog.cpp:442` UIDocument 表尾后）；计数同步四处 = `engine_tests.cpp:728,777` + `tests/script/main.cpp:84,91`。
- vtable 现 36 槽（`ScriptHost.cpp:322-357`）→ 尾加 7；`Events.cs` handler 数组 16 槽已用 13 → **不加音频事件**（余量留命）。
- 消费端事件齐备：`Hit/Death/Pickup/LevelUp/WaveStart` 全在且 svr-test 已订阅——音频纯消费零新事件。
- 网络当日不通（代理关）→ miniaudio 最新 tag 未能在线核对，ADR 写"0.11.x 实现期核定"；Vorbis 内建可用性列批⓪ 首项核定。

## 待用户拍板（ADR-015 D1–D4，批⓪ 动工前）

D1 源格式是否顺手放开 mp3/flac（建议放开）；D2 `.baked` 落位 `.lemon/baked/audio/`（建议）；D3 固定三组 BGM/SFX/UI（建议）；D4 BGM 换曲默认 0.5s 交叉淡出（建议）。

## 下一步

拍板 → 批⓪（CPM miniaudio + `Engine/Audio` 骨架 + 静音降级 + 混音单测 + 100 voice 0.5ms 断言）。
