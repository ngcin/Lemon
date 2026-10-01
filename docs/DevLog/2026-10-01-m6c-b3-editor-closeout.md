# M6c 批③ — 编辑器收口（AudioRef 槽 + Mixer 工具窗 + smoke-audio 全链）

- 日期：2026-10-01
- 批文件：[Plans/M6c/2026-10-01-b3-editor-closeout](../Plans/M6c/2026-10-01-b3-editor-closeout.md)
- 前情：批②/批①b + 两轮听感热修 done；**真人复听本日暂告段落**（用户拍板"暂时这样"，全局默认接受——per-资产覆写随之缓议）。

## 实测数字

| 项 | 结果 |
|---|---|
| ctest | 3/3（engine-tests 34036 / imgui-isolation / script-tests——引擎零行为改动） |
| smoke-audio 静音（`LEMON_AUDIO=off`，夹具项目） | `entries=1 baked=1 meta=1 preview=1/1 voices=1/0 => OK` |
| smoke-audio 设备（svr-test 真项目） | `进 Play 音频装载：8 成功（流式 2）` 同断言 `voices=1/0 => OK`；零播种污染 |
| 回归 full | **17 步**（audio-chain 入库；首跑 16/17——smoke-ui `parent=0/1` 首跑抖动，单独复跑绿，负载抖动复跑绿既有先例） |

## 落地

1. **Inspector AudioRef 槽**：`FieldHint::AudioRef = 1<<12` + `ED_AUDIOREF` 宏 + `DrawGuidSlot`
   分支（`AssetType::Audio` / dragKind 10 / 空槽文案"(无片 · 静默)"）；scrubbable 与
   字段级重置两张排除清单同步加位。`kEdAudioSource` 字段行补齐：refDist/maxDist
   Range+tooltip 共存（手写行——ED_RANGE 宏无 tip 位）、group 三名 Enum 下拉。
   数据面零改动（编辑域元数据，C# 镜像/金回放零影响）。
2. **Audio Mixer 按需工具窗**：05 §3 面板集冻结的**旁路形态**（不进 CreateAllPanels/
   默认布局，.tab 浮动编辑器先例），Window 菜单进入。Master/三组滑条（引擎钳界）+
   voice 计数 N/64 + 静音降级 badge + 「全部停止」急停 + **重触发节流/音高微扰全局
   滑条**（两轮听感热修参数的调参试验台）。无持久化（设置屏音量档 = 批④）。
3. **smoke-audio 全链第二段（playOnStart 机器验收）**：开工考古修正——计划行
   "EnterPlay playOnStart 自动开播"系统侧批② 已实现（AudioSystem ② 段扫描 + 竖切批
   装载 + 批② 后端注入双点），本批余量 = 断言。编辑场景播种双实体（循环声源 bit1
   置位 + 对照 bit1 清零）→ TryEnterPlay → 8× TickPlay/audio_.Tick →
   **`ActiveVoiceCount()==1`**（恰一起播 = playOnStart 生效 + 对照不起播 + preview
   已停三事实合一；==1 而非 >=1 的口径依据 = Stop 当帧跌零、ActiveVoiceCount 只计
   !done）→ StopPlay → **==0**。汇总行扩 `voices=%d/%d` 位。
4. **夹具纪律**：`SeedSmokeAudioProject()` 只在目标目录**不是项目**（无 project.lemon）
   时播种（project.lemon + `Assets/smoke-tone.wav`——48k mono PCM16 正弦 0.25s 手写
   RIFF，24KB 整载路径）；真项目（svr-test）零污染实证。回归步
   `--project ${TMP}/audio` 自足。
5. **回归步入库**：audio-chain 步以 `env LEMON_AUDIO=off` 跑——同一判据兼收
   "静音模式逻辑声部断言"与"off 无头同绿"两出口；full 16→17。

## 附带修（实现期发现）

- **后台烤制目录建目录时序缺口**：新项目首次导入即后台烤（WarmAudioBakes 于
  OpenProjectPipeline 后立刻入队）早于任何 EnterPlay，而烤制目录创建原归
  MountPlayAudio（首进 Play）——夹具新项目首跑即现"烤制产物不可写"红字（真项目
  目录早已存在故从未暴露）。修：`EnqueueAudioBake` 入队侧幂等建目录（主线程，
  无 worker fs 竞争）。
- smokeAudio launch 注释错贴（第二行是 smokeGuid 的描述）顺手修正。

## 候选项裁定

per-资产音频参数（节流窗/并发上限/微扰幅度 meta 覆写）**缓议**：真人复听已接受
全局默认；三段式改动（meta schema + 引擎 per-clip 参数 + Inspector meta 编辑）=
独立竖切微批，登记进 M6c.md 登记项，svr-test 真实调参需求出现时启。

## review 修正（2026-10-01 同日，提交前 review 三件）

1. **P1 真项目存档回写面（隐患，无事故）**：批① smoke-audio 只到试听无此面；批③
   第二段 `TryEnterPlay→StopPlay` 使 `--smoke-audio` 在真项目上新增副作用——Game/
   脚本装配运行 8 tick + EnterPlay 载入/ExitPlay 兜底回写 `.lemon/saves/` 三档。
   svr-test 验机实证：三档 mtime 被改，但**键值集合恒等**（装载值原样回写 + 键序
   重排；16:40 与 18:48 两轮均恒等，.bak 链完好）——恒等不具一般性（游戏在 Start
   写 meta 类键即漂移）。修：①链头 `FindGameProject` 命中即 WARN 交底（含"验机
   建议跑副本"）；②本文"零播种污染"表述据此收窄——播种面确为零污染，进 Play 面
   有上述回写。
2. **P2-1 裸 `--smoke-audio` fail-fast**：裸跑两 hazard——自动重开上次项目并在其上
   跑链（含 P1 副作用），或无最近项目时链路不执行、无提示开窗常驻（后者在修复
   验证中被旧二进制实演挂死）。修：`projectDir` 空 = 红字退出 1（守卫在自动重开
   块前）+ launch 注释补回"须配 --project"。验证：exit 1 + 红字。
3. **P2-2 元数据抽查锁**：`TestEditorMetaSanity` 既有特性抽查补 AudioSource——
   clipGuid AudioRef + UInt64、group Enum×3（本批唯一引擎侧改动的单测守门）。

修后全绿：ctest 3/3（新抽查生效）/ 临时项目链无 WARN OK / svr-test WARN + OK
（8/8 流式 2、voices=1/0，三档二次回写仍键值恒等）。

## 遗留

- 批④：CC0 素材入库 + svr-test/模板接音 + 设置屏音量滑条（Settings 档持久化）+
  暂停语义实测；真人听感两项（暂停挂起续响 / 重开不叠曲+淡出）与批② 同场补验。
- Audio Mixer 视觉走查归入批④ 真人验收清单（工具窗默认关，无既有断言面）。
