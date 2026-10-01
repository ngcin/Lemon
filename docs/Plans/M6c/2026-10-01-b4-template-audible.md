# M6c 批④ — 模板全程有声（CC0 素材 + 接音 + 音量滑条 + 暂停语义）

Status: done（2026-10-01 当日代码面收口。**素材终裁**：BGM = OGA "5 Action
Chiptunes" Level 1（Juhani Junkala，CC0——zip 断点续传 48MB 到手，WAV 转 ogg
1.4MB @ q0.5，74.25s 全曲循环流式）——程序化回退未启用；SFX 六件 = Kenney CC0
五包各取一。**回归 full 16/17 + smoke-ui 单独复跑绿**（既有首跑抖动先例口径；
uirml 双步由本批附带修"watcher 竞速"转绿，见附带修段）+ ctest 3/3 + smoke-template
`aud(mount=7 pause=5 resume=6=OK)`（standalone + 回归双绿）+ bench-survivor
**fps=82 ≥ M6b 基线 78**（alive=10436/fx 饱和同前）+ svr-test 无头 `9 成功
（流式 2）`同断言过。**真人验收清单待用户**（见文末，批② 两项同场）。
[DevLog](../../DevLog/2026-10-01-m6c-b4-template-audible.md)）

## 范围（M6c.md 批④ 行）

模板全程有声：CC0 素材入库（来源登记）+ vs-survivor 模板接音（BGM/命中/击杀/拾取/
升级/波次横幅 + UI 组按钮音）+ svr-test 补接（UI 组按钮音 + 同款音量滑条）+ 设置屏
音量滑条（Settings 档持久化）+ 暂停语义实测（机器断言 + 真人清单同场）。

**开工考古修正**：M6c.md 批④ 行写的"svr-test 接音（BGM/命中/…）"主体已由批⓪.5 竖切
落完（yami 8 件 + Main.scene 全事件）；svr-test 本批余量 = UI 组按钮音 + 音量滑条。
"全程有声"的欠账在**模板**（Templates/vs-survivor 至批③ 止零音频资产）。

## 裁定项（实现前定）

1. **素材源（CC0 入库）**：SFX 六件 = Kenney CC0 包（impact-sounds / rpg-audio /
   digital-audio / ui-audio / music-jingles 各取一，kenney.nl 直链下载实测可达）；
   BGM = OpenGameArt "5 Action Chiptunes"（Juhani Junkala，CC0，OGA 直链——代理限速
   断点续传；**不济则程序化 BGM 回退**（生成器合成短循环，模板小图同款"无版权负担"
   先例），届时本文件记终裁）。入库位置 `Samples/Assets/cc0-audio/`（yami-dungeon
   先例：素材包 + .meta 固定 guid + README 来源登记），THIRD_PARTY.md 加行。
   理由：模板随引擎再分发，素材许可以 CC0 为零负担上限（yami MIT 亦可但模板侧
   已有两类来源，音频走纯 CC0 一类登记最省）。
2. **GUID 族**：模板音频 `7e57400000000001..07`（0=程序图 / 1=prefab / 2=表 /
   3=UI rml 之后的音频段；开工勘定——原拟 7e5730000000 与 UI 段 7e5730000010 视觉
   同头易误读）；svr-test UI 音 `6a6d100000000009`（竖切 8 件后续号）。
3. **UI 按钮音打点位**：`GameMain.OnUiEvent` Click 统一打点（游戏侧自律——引擎不加
   全局 UI 音钩，ADR-015 M5"游戏逻辑不进引擎"口径）。
4. **音量滑条**：settings.rml 四滑条（主音量/音乐/音效/界面，`<input type="range">`
   0–100）——Change 事件通道（UiBridge 已有 kind，RmlUi WidgetSlider 派发源码确认：
   SetValueInternal 值变即 DispatchEvent(Change)，payload=GetValue() "%f" 串）；
   Settings 档新四键 `vol.master/bgm/sfx/ui`（int 0–100 字串，version 仍 1——加键
   向后兼容）；SetAttr 回显 + DocumentReloaded 重灌。**Change 自回环**：SetAttr 写
   value 同样派发 Change → C# 同值早退（无操作、不落盘），免抑制位。
5. **暂停语义**：模板 GameFlow.SetPaused → `Audio.Paused = on`（svr-test 批② 先例）；
   ReturnToMenu → `Audio.StopBgm(0.5f)`（回菜单静场）；EnterRun → `Audio.PlayBgm`
   （重开不叠曲 = 引擎单槽已保）。机器面 = smoke-template 暂停段
   `ActiveVoiceCount()>=1`（BGM 挂起非终止——静音模式逻辑声部同口径）+ 进 Play
   `audioClips_.size()==7`（装载恰 7）。
6. **smoke-template RESULT 扩位**：`aud=%d/%d`（mount/voices 暂停段）追加不破坏既有
   grep（回归 grep 前缀不变）。

## 分解（文件/行级）

### T1 CC0 素材入库（来源登记）

- `Samples/Assets/cc0-audio/`（新建）：7 ogg + .meta（`"type":"audio"` + importer
  `loop/preload`——BGM 全曲循环 `loop:[0,0]` 流式 preload:false；SFX 整载）+
  `README.md` 逐件来源表（包名/原文件名/作者/许可/URL）。
- `THIRD_PARTY.md`：kenney CC0 包行 + OGA CC0 行（或程序化裁定注记）。

### T2 模板接音（`Editor/Templates/VsTemplateGen.cpp`，重生成入库）

- 新 `WriteAudioAssets(root/Assets)`：拷 cc0-audio 七件 → `Assets/Audio/`（.meta
  随行）；GenerateVsTemplate 步骤 1) 段挂调。
- `GameMain.cs`（内嵌源）：Settings 增 `MasterVol/BgmVol/SfxVol/UiVol`（float 0..1，
  默认 0.8）；OnUiEvent：Click 全体 → `Audio.PlayOneShot(kSfxUi, .5f, AudioGroup.Ui)`
  （kSfxUi 常量归 GameMain）；Change → `GameFlow.OnVolumeChange(e)`。
- `GameFlow.cs`：kBgm + EnterRun 播 BGM / ReturnToMenu 停 BGM（0.5s 淡出）/
  SetPaused 挂起；LoadSettings/SaveSettings 扩音量四键 + 应用
  `Audio.MasterVolume`/`SetGroupVolume` + 滑条 SetAttr 回显；OnVolumeChange
  （payload 解析 → 静态值更新 → 引擎应用 → 落盘）；热重载包补四值。
- `PlayerCombat.cs`：kSfxHit/Kill/Pickup/LevelUp 常量；OnHit(team1)→hit、
  OnDeath(team1)→kill、`Subscribe(Pickup)`→pickup、LevelUp 订阅→levelup
  （svr-test PlayerBehaviour 同位点对齐）。
- `PlayerHud.cs`：WaveStart 订阅补 `Audio.PlayOneShot(kSfxWave, .6f)`。
- `settings.rml`（内嵌）：四滑条行（id vol-master/bgm/sfx/ui）+ 移除"更多设置随
  音频（M6c）加入"占位。
- `theme.rcss`（内嵌）：`input[type=range]` 尺寸 + `slidertrack/sliderbar` 样式
  （token 取色；RmlUi demo 先例）。
- README（生成器内嵌）：素材来源段加音频两行 + 玩法锚点加音频行。
- 重生成 `Templates/vs-survivor`（`--gen-vs-template` 入库目录，F-02 自检标记过）。

### T2b svr-test 补接（用户工作项目——不碰 WIP override/.anim）

- `Assets/Audio/ui-click.ogg` + .meta（6a6d100000000009，kenney click 同件）。
- `Game/GameMain.cs`：OnUiEvent Click 打点（kSfxUi）+ Change 路由。
- `Game/GameFlow.cs`：音量四键 Load/Save/OnVolumeChange（同模板）；SetPaused 已有
  `Audio.Paused`（批②）不动；ReturnToMenu 补 StopBgm 淡出（对齐模板）。
- `Assets/UI/settings.rml` + `theme.rcss`：四滑条 + 样式（同模板）。

### T3/T4 机器验收

- smoke-template（`EditorAppSmokeTpl.cpp`）：RESULT 行追加 `aud=%d/%d`
  （audioClips_.size() / 暂停段 ActiveVoiceCount）；断言 mount==7 && pauseVoices>=1
  （flowOk 并入）。
- `ctest` 3/3（引擎零行为改动——预期零新测；滑条/音量 = 脚本面）。
- 回归 full 17 步（template 步 grep 前缀不变；audio-chain 步不受影响）。
- bench-survivor fps 对照 78（音频装载 + BGM 流式在线）。
- svr-test 设备模式无头验放：`--project demo/svr-test --smoke-audio`（WARN 交底
  先例口径）——装载 9 成功（8+ui-click）。

### T5 文档收口

- 批文件勾销 + DevLog 新条目；M6c.md Status/批次表 + 08 §2 M6c 行注记 +
  AGENTS.md 状态行；06 §2.2 无改动（通道批① 已收敛）。
- **真人验收清单（交用户，批② 两项同场）**：一局全程有声（BGM/命中/击杀/拾取/
  升级/波次/UI）/ 滑条即时生效 + 重开保持 / 暂停挂起续响（BGM 停拍→Esc 恢复续响，
  UI 音暂停中仍可响）/ 重开不叠曲 + 淡出听感 / Audio Mixer 视觉走查（Window 菜单，
  批③ 遗留）。

## 出口判据映射（M6c.md 批④ 行 → 08 §2）

| 08 判据 | 本批落点 |
|---|---|
| VS/TD 模板全程有声 | T1+T2（vs-survivor 实装；TD=M9 同 API 零改动消费） |
| 100 并发 ≤0.5ms | 批⓪ 已落（0.0092ms），本批零引擎改动复核 ctest |
| 无音频设备不崩 | 批③ 回归步已盖；本批 template/audio 双步复跑 |
| §M6c 段终验 | T3/T4 + 真人清单 |

## 实测数字（2026-10-01）

| 项 | 结果 |
|---|---|
| ctest | 3/3（引擎零行为改动；script-tests 3/3 = 模板新 C# 面由 smoke-template 编译链盖） |
| smoke-template（standalone + 回归双跑） | `音频装载：7 成功（流式 1）`；`aud(mount=7 pause=5 resume=6=OK)`；BGM 烤制 `2ch / 74.25s / PCM16 48kHz`；六屏/流程链旧位全绿 |
| bench-survivor | **fps=82 PASS**（对照 M6b 收官 78 = 零降级；alive=10436、fx 饱和、director 三波同前） |
| svr-test 无头（`--smoke-audio`） | WARN 交底（既有口径）+ `9 成功（流式 2）` + `voices=1/0 => OK`（ui-click 入库 8→9） |
| 回归 full | **16/17 + smoke-ui 单独复跑绿**（首跑抖动既有先例；uirml 双步经附带修转绿） |

## 附带修（实现期发现，非本批引入）

- **uirml 帧锚定装载 vs 500ms watcher 轮询竞速**（bc5fd73 起可复现、本机快时
  稳定红；插桩实证 watcher 正常、纯锚点竞速）：`EditorAppSmokeUirml.cpp` 两处
  单发装载钩（302 形态 B 种子 / 394 终局复位）改**重试窗**（254..303 / 353..404
  ≈ 50 帧 ≥ 轮询周期；成功即停；`FindByPath` 须过 `!missing`——墓碑命中会
  假成功静默跳载，原 else 红字语义窗尽保留）。修后无脚本 ×3 + 脚本模式 + 回归
  双步全绿。**归因链**：本批改动 stash 后于 3e66017/bc5fd73 双位复现 = 既有
  脆弱性暴露（机器提速），非批④ 改动所致；记录为 M6c 期间发现的回归面收口。
- C# 编译错一轮（`var s = GameMain.Settings` 把嵌套静态类当实例别名——CS0119/
  CS0723），双落修正为全限定访问；svr-test 无头链编译绿。

## 验收反馈热修（2026-10-01 同日，commit bcb20c0 后）

用户走查反馈"暂停挂起续响没法测试（编辑器暂停钮有声 / Esc 退出 Play）"——三定位两
迷惑一真 bug：工具栏暂停原只冻 sim 不碰音频（检视冻结 ≠ 游戏暂停，零交底）；Esc =
编辑器惯例 StopPlay（游戏暂停实际按 **P**，bit6 别名）；**pausedAll 跨会话残留**
（游戏暂停中 StopPlay 再 Play，新 BGM 起播即挂起变哑——自动化未覆盖路径）。修 =
MountPlayAudio 会话起点归位 + 暂停钮联动挂起（恢复按游戏 staged 意图回设）+ 模板
提示/README 键位交底。修后回归 full **17/17 首跑全绿**。
[DevLog](../../DevLog/2026-10-01-m6c-b4-editor-pause-audio-hotfix.md)

## 真人验收清单（待用户；批② 两项同场补验）

1. **一局全程有声**（模板或 svr-test）：BGM 开局起播 / 命中·击杀·拾取·升级·
   波次横幅事件音 / UI 按钮（含升级卡）音——密度与音量平衡观感。
2. **音量滑条**：设置屏四滑条（主/音乐/音效/界面）拖动即时生效；重启编辑器
   进 Play 保持（Settings 档）；滑条外观（轨道/拖点配色与主题一致性）。
3. **暂停挂起续响**（批② 遗留）：**编辑器内按 P**（Esc = 编辑器退出 Play 惯例）
   → BGM 停拍挂起（不回跳不静默）、再按 P 恢复续响；暂停屏按钮音仍可响（Ui 组
   免疫）；工具栏暂停钮同语义（热修后联动）；另验「游戏暂停中 Esc 退出再进
   Play，BGM 正常出声」（残留修复验点）。
4. **重开不叠曲 + 淡出**（批② 遗留）：结算"再战一局"换曲不叠；"回主菜单"
   BGM 0.5s 淡出听感（模板/svr-test 同款语义）。
5. **Audio Mixer 视觉走查**（批③ 遗留）：Window 菜单开工具窗——Master/三组
   滑条、声部计数、节流/微扰滑条布局观感（功能位机器面已绿）。
6. BGM 曲目观感（Juhani Level 1 chiptune 74s 循环）：合不合幸存者调性，
   不合可换件（guid 不动零代码改动）。
