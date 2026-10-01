# M6c 批④ — 模板全程有声（CC0 素材入库 + 模板/svr-test 接音 + 音量滑条 + 暂停语义）

- 日期：2026-10-01
- 批文件：[Plans/M6c/2026-10-01-b4-template-audible](../Plans/M6c/2026-10-01-b4-template-audible.md)
- 前情：批③ + review 三修 done（commit 3e66017）；真人复听暂告段落。批④ 出口 =
  08 §2 M6c 四判据 + bench fps 零降级 + 真人清单。

## 实测数字

| 项 | 结果 |
|---|---|
| ctest | 3/3 |
| smoke-template | `音频装载：7 成功（流式 1）` + `aud(mount=7 pause=5 resume=6=OK)`；BGM 烤制 `2ch / 74.25s / PCM16 48kHz`；旧位（六屏/流程链/层序）全绿——standalone + 回归双跑 |
| bench-survivor | fps=82 PASS（M6b 收官基线 78 = 零降级；alive=10436、fx 饱和、三波同前） |
| svr-test 无头 | `--smoke-audio`：WARN 交底（既有口径）+ `9 成功（流式 2）` + `voices=1/0 => OK`（ui-click 8→9 件） |
| 回归 full | 16/17 + smoke-ui 单独复跑绿（首跑抖动既有先例；uirml 双步经附带修转绿） |

## 落地

1. **CC0 素材包 `Samples/Assets/cc0-audio/`**（1.5MB，yami-dungeon 同款入库形态）：
   SFX 六件 = Kenney 五包各取一（impact/rpg/digital/jingles/ui-audio，直链下载）；
   BGM = OGA "5 Action Chiptunes" Level 1（Juhani Junkala，作者 INFO.txt 自证 CC0）。
   zip 48MB 经代理限速断点续传九轮到手，WAV→ogg 1.4MB（soundfile/libsndfile 本机
   转，q0.5），74.25s 全曲循环（作者按可循环作曲）。程序化回退未启用（批文件
   裁定 1 的备选路径关闭）。来源登记三处：包内 README 逐件表 + THIRD_PARTY.md
   两行 + 模板 README 摘要。GUID 族 `7e57400000000001..07`（开工勘定：原拟
   7e5730000000 与 UI rml 段 7e5730000010 视觉同头易误读）。
2. **模板接音**（`VsTemplateGen.cpp` 重生成入库）：`WriteAudioAssets` 拷七件 →
   `Assets/Audio/`（缺件红字断生成，对齐 yamiSrc 先例）；GameFlow = 开局 PlayBgm
   （0.55）/ 回菜单 StopBgm（0.5s 淡出）/ SetPaused 补 `Audio.Paused`（svr-test
   批② 先例对齐）；PlayerCombat 四事件音（hit 0.45 高频小音量——引擎 45ms 节流
   兜底 / kill 0.6 / pickup 0.5 / levelup 0.7）；PlayerHud 波次音；GameMain 全体
   Click 统一打点 Ui 组按钮音。
3. **设置屏音量四滑条**（模板 + svr-test 双落）：`<input type="range">` 0–100
   step 5——**Change 事件通道首用**（UiBridge 枚举批③c 已留位；RmlUi 源码核读：
   WidgetSlider 值变即 DispatchEvent(Change)，payload=GetValue() "%f" 串；SetAttr
   写 value 同径回发——**同值早退**做自回环防线，免抑制位）；theme.rcss 新组件
   `slidertrack/sliderbar`（伪元素几何 = RCSS 定尺寸 + widget 算偏移，RmlUi demo
   先例）；Settings 档新增 `vol.master/bgm/sfx/ui` 四键（int 0–100，version 仍 1
   加键向后兼容）；GameFlow OnVolumeChange（解析/钳界/应用/落盘）+ ApplyVolumes
   （Master + 三组）+ 热重载包四值 + 热进回设。
4. **svr-test 补接**（用户工作项目，WIP override/.anim 不碰）：ui-click.ogg
   （`6a6d100000000009`，kenney click3 同源同件）+ GameMain Click 打点/Change 路由
   （UiEcho 自订阅面不吞）+ GameFlow 音量四键（同模板）+ ReturnToMenu 补 StopBgm
   对齐。
5. **smoke-template 扩位**：RESULT 行追加 `aud(mount/pause/resume)`——装载恰 7
   （七件全链）+ 暂停屏在场期声部 ≥1（BGM 挂起非终止，静音模式逻辑声部同口径）
   + 暂停解除首帧 ≥1（续响）——"暂停语义实测"的机器面；grep 前缀不变（追加式）。

## 附带修（既有缺陷暴露，非本批引入）

- **uirml 冒烟帧锚定装载 vs 500ms watcher 轮询竞速**：302/394 单发装载钩在
  快机上竞速落空（352 复种复活需 ≥1 轮询周期 500ms，42 帧 @75fps ≈ 550ms 贴边）
  → 405 清场断言连锁红。**归因链**：本批改动 stash 后于 3e66017 与 bc5fd73
  双位稳定复现 = 既有脆弱性（机器提速暴露；批③ 当日 full-17 绿 = 时段机器慢的
  运气），插桩实证 watcher/复活/装载全正常、纯锚点竞速。修 = 两钩改重试窗
  （254..303 / 353..404，成功即停；`FindByPath` 过 `!missing` 防墓碑假成功——
  墓碑命中会走静默早退，原 else 红字恰好被吞）。修后无脚本 ×3 + 脚本 + 回归
  双步全绿。
- C# 编译错一轮：`var s = GameMain.Settings` 把嵌套静态类当实例别名（CS0119/
  CS0723），svr-test 无头链暴露，双落改全限定访问。

## 遗留

- 真人验收清单六条（一局全程有声/滑条/暂停挂起续响/重开不叠曲+淡出/Mixer 走查/
  BGM 曲目观感）——批② 两项同场，见批文件文末。
- P3 观感项沿批③ 登记（Mixer SameLine 宽度/"全部停止"Play 态语义/flags 行
  checkbox 化）。
- Kenney 包内其余素材未入库（按需取件，README 表可扩）。
