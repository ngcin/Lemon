# M6c 批③ — 编辑器收口（AudioRef 槽 + Mixer 工具窗 + smoke-audio 全链）

Status: done（2026-10-01 当日收口：**回归 full 17 步**（audio-chain 入库 = 第 17 步；
首跑 16/17——smoke-ui `parent=0/1` 首跑抖动，单独复跑绿 = 既有负载抖动先例口径，
非回归）+ ctest 3/3 + smoke-audio **双环境同绿**（静音 `LEMON_AUDIO=off` 夹具项目
`entries=1 baked=1 meta=1 preview=1/1 voices=1/0`；真项目 svr-test 设备模式
`8 成功（流式 2）`同断言过；播种面零污染——进 Play 面注意项见 review 修①）。附带两修：①后台烤制目录建目录时序缺口
（新项目首次导入即后台烤早于首 EnterPlay，worker 落盘"产物不可写"红字——
`EnqueueAudioBake` 入队侧幂等补建）；②smokeAudio launch 注释错贴（smokeGuid 描述）。
**提交前 review 三修（同日）**：①P1——真项目 `--smoke-audio` 新增存档回写面
（ExitPlay 兜底落盘三档 + Game/ 脚本装配运行；svr-test 两轮实证键值恒等 + .bak
完好，恒等不具一般性）→ 链头 WARN 交底（"验机建议跑副本"）；②P2-1——裸
`--smoke-audio` fail-fast（projectDir 空 = 红字退出 1，守卫先于自动重开块）+ 注释
补回"须配 --project"；③P2-2——TestEditorMetaSanity 补 AudioSource 元数据抽查
（AudioRef + group Enum×3）。修后 ctest 3/3 + 三环境链复验绿。
[DevLog](../../DevLog/2026-10-01-m6c-b3-editor-closeout.md)（含 review 修正段））

## 范围（M6c.md 批③ 行）

**开工考古修正**：计划行"EnterPlay playOnStart 自动开播"的**系统侧已由批② 实现**
（`AudioSystem::Tick` ② 段扫描：playOnStart 绑定建立时起播一次 + 换片重绑 + 悬空
guid 红字，`Systems.cpp:1313`；装载侧竖切批 `MountPlayAudio` / 批②
`WirePlayAudioBackend` 双点齐备）——本批该项实际余量 = **机器验收断言**（smoke
第二段），无新运行时代码。

- Inspector AudioRef 槽（hint `1<<12` + `DrawGuidSlot` 复用）+ AudioSource 全字段行。✅
- Audio Mixer 面板（Master/三组/voice 计数）。✅
- EnterPlay playOnStart 自动开播 = 验收断言补齐（见上考古注）。✅（voices=1/0 双环境过）
- smoke-audio 全链 + 回归步入库（full 16→17 步，audio-chain）。✅
- 出口判据：smoke-audio 全链 OK（**静音模式下逻辑 voice 计数断言**，`voices=1/0` 位）；回归 full 17 步；`LEMON_AUDIO=off` 无头环境同绿（回归步以 `env LEMON_AUDIO=off` 跑 = 同一判据的机器面）。✅

## 候选项裁定：per-资产音频参数（节流窗/并发上限/微扰幅度）

**缓议（本批不做）**。理由：①真人复听 2026-10-01 已接受全局默认值（"暂时这样"）——
无紧迫消费面；②该项跨 meta importer schema + AudioEngine per-clip 参数面 + Inspector
meta 编辑三段，是独立竖切而非编辑器收口的余项；③用户项目（svr-test）出现真实调参需求
时以微批启（登记进 M6c.md 登记项）。Mixer 工具窗内的全局节流/微扰滑条（T2）即为其
调参试验台。

## 分解（文件/行级）

### T1 Inspector AudioRef 槽 + AudioSource 字段行

| 文件 | 改动 |
|---|---|
| `Engine/ECS/ComponentRegistry.h:61` | `FieldHint` 尾加 `AudioRef = 1u << 12`（u64 = 音频资产 GUID 全量；M6c 批③） |
| `Engine/Components/ComponentCatalog.cpp:33` 区 | 宏区加 `ED_AUDIOREF(tip)`（同 ED_RMLREF 形） |
| `Engine/Components/ComponentCatalog.cpp:137-150` | `kEdAudioSource`：clipGuid → ED_AUDIOREF；refDist/maxDist → ED_RANGE；group → Enum（Bgm/Sfx/Ui 名表，kCollectibleKindNames 同法）；头注"批③ 接 AudioRef 选择器"改为已接 |
| `Editor/Panels/InspectorPanel.cpp:428-432` | scrubbable 排除清单加 `AudioRef` |
| `Editor/Panels/InspectorPanel.cpp:464-468` | 字段级重置排除清单加 `AudioRef` |
| `Editor/Panels/InspectorPanel.cpp:509` 后 | DrawGuidSlot 分支链加 `AudioRef + UInt64 → DrawGuidSlot(app, p, AssetType::Audio, 10, "(无片 · 静默)")`（dragKind 10 = AssetBrowser KindOf(Audio) 既有值） |

数据面零改动：FieldEditorMeta 留编辑域（C# 镜像只认运行时位），Enum 写值走既有
整数写径——金回放/存档零影响。

### T2 Audio Mixer 按需工具窗

**形态裁定**：05 §3 面板集冻结的**旁路形态**——不进 `CreateAllPanels` 面板注册表
（.tab 浮动编辑器先例），Window 菜单进入（Animation 之后、Demo 之前），默认关。
05-Editor.md §3 注记区补一行。

| 文件 | 改动 |
|---|---|
| `Editor/App/EditorApp.h` | `audioMixerOpen_` + `DrawAudioMixerWindow()` 私有声明 |
| `Editor/App/EditorAppChrome.cpp:169` Window 菜单 | `MenuItem("Audio Mixer", ...)`（面板列表分隔线后） |
| `Editor/App/EditorAppChrome.cpp` | `DrawAudioMixerWindow()` 实现：Master/三组音量滑条（`Set/Group/MasterVolume` 直写，0..1 钳界引擎侧已有）+ voice 计数 `N/64`（`ActiveVoiceCount`，静音模式同有效）+ 静音降级 badge（`silent()`）+ 重触发节流/音高微扰全局滑条（`SetRetriggerCooldown`/`SetPitchJitter`，两轮热修参数的调参面）。无持久化（Settings 档 = 批④ 设置屏） |
| `Editor/App/EditorAppChrome.cpp:614` 后 | BuildUI 内 `BuildPickersAndModals()` 后挂 `DrawAudioMixerWindow()` |
| `docs/EngineDesign/05-Editor.md` §3 注记区 | 一行：M6c 批③ Audio Mixer 按需工具窗（冻结旁路形态，不进注册表） |

### T3 smoke-audio 全链（第二段：playOnStart / EnterPlay voice 断言）

| 文件 | 改动 |
|---|---|
| `Editor/App/EditorApp.h` | `SeedSmokeAudioProject()` 声明 |
| `Editor/App/EditorApp.cpp:261` 区 | pipeline 前挂 `if (launch.smokeAudio) SeedSmokeAudioProject();`（**只在目标目录无 project.lemon 时播种**——真项目零污染） |
| `Editor/App/EditorAppSmoke.cpp` 或 SmokeAudio TU | `SeedSmokeAudioProject()`：project.lemon + `Assets/smoke-tone.wav`（48k mono PCM16 正弦 0.25s，手写 RIFF 头——24KB < 1MiB 整载路径） |
| `Editor/App/EditorAppSmokeAudio.cpp` | 第二段：编辑场景播种两实体（playOnStart 循环声源 + bit1 清零对照）→ `TryEnterPlay()` → 8× `TickPlay(1/60)+audio_.Tick` → **`ActiveVoiceCount()==1`**（起播恰一 + 对照不起播 + preview 已停）→ `StopPlay()` → **count==0**。汇总行加 `voices=1/0` 位 |

静音模式（LEMON_AUDIO=off）与设备模式同径断言（逻辑声部记账是降级路径的一部分，
批⓪ 口径）。

### T4 回归步入库

| 文件 | 改动 |
|---|---|
| `tools/editor-regression.sh` full 段 | audio-chain 步：`env LEMON_AUDIO=off ${EDITOR} --project ${TMP}/audio --smoke-audio --no-reopen`，grep `smoke-audio.*OK`。full 16→17 |

## 验证

- `cmake --build --preset mac` 零警告面（-Werror）。
- ctest 3/3（引擎无行为改动，hint 是编辑域枚举位）。
- `--smoke-audio` 双环境：真项目（svr-test，设备模式）+ 临时项目（回归步，LEMON_AUDIO=off）。
- `tools/editor-regression.sh full` 17/17（含既有 16 步零回归——Inspector/菜单改动
  过 basic smoke / smoke-ui 的面板与 ID 扫描）。
