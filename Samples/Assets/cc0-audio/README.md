# cc0-audio — 模板音频素材包（M6c 批④）

vs-survivor 模板"全程有声"七件（`--gen-vs-template` 拷入模板 `Assets/Audio/`，
.meta 固定 guid `7e57400000000001..07`——引用锚点，勿改）。全部 **CC0**（无署名
义务；本表登记仅为溯源，再分发无附加条件）。

| 文件 | 用途 | 原始文件 | 来源包（kenney.nl，CC0） |
|---|---|---|---|
| bgm.ogg | BGM 循环（开局起播，>1MiB 流式） | 行动类 chiptune 循环曲 | OpenGameArt "5 Action Chiptunes"（Juhani Junkala，CC0）——见下行备注 |
| hit.ogg | 怪受击（高频，小音量） | `impactGeneric_light_000.ogg` | [impact-sounds](https://kenney.nl/assets/impact-sounds) |
| kill.ogg | 击杀 | `knifeSlice.ogg` | [rpg-audio](https://kenney.nl/assets/rpg-audio) |
| pickup.ogg | 宝石拾取 | `handleCoins.ogg` | [rpg-audio](https://kenney.nl/assets/rpg-audio) |
| levelup.ogg | 升级 | `powerUp1.ogg` | [digital-audio](https://kenney.nl/assets/digital-audio) |
| wave.ogg | 波次横幅 | `jingles_NES00.ogg`（8-Bit jingles/） | [music-jingles](https://kenney.nl/assets/music-jingles) |
| ui-click.ogg | UI 组按钮音 | `click3.ogg` | [ui-audio](https://kenney.nl/assets/ui-audio) |

- Kenney 各包 `License.txt`：License: (Creative Commons Zero, CC0)
  http://creativecommons.org/publicdomain/zero/1.0/ ——"This content is free to
  use in personal, educational and commercial projects. Support us by crediting
  Kenney or www.kenney.nl (this is not mandatory)"（下载于 2026-10-01）。
- BGM 备注：OpenGameArt [5 Action Chiptunes by Juhani Junkala](https://opengameart.org/content/5-chiptunes-action)
  （CC0——作者 INFO.txt 原文 "These music tracks have been released under CC0
  creative commons license. You can do anything you want with these tunes."，
  2018）；本包取其 Level 1 WAV 转档 ogg（q0.5）。若代理不济改程序化生成，本行
  届时改记"程序化合成（无版权负担）"并同步 THIRD_PARTY.md（本轮未启用）。
- svr-test 的 `Assets/Audio/ui-click.ogg`（guid `6a6d100000000009`）与本包
  `ui-click.ogg` 同源同件（Kenney click3，CC0）——用户项目侧 UI 音不引 yami 面。

THIRD_PARTY.md 总登记见仓库根；模板 README"素材来源与许可"段另有面向模板用户
的摘要。
