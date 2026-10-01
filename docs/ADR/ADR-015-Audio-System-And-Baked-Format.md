# ADR-015：M6c 音频系统——miniaudio 后端、2D 声源双轨与 `.baked` 音频容器 v1 定形

- 日期：2026-09-30（M6c 开工设计定稿）
- 状态：已采纳（**D1–D4 已拍板 2026-09-30 用户：均按建议**——D1 放开 mp3/flac、D2 `.lemon/baked/audio/`、D3 固定三组、D4 BGM 默认 0.5s 交叉淡出；miniaudio 版本核定 **0.11.25**，用户备 zip、vendor 引入。批⓪ 已动工）
- 影响：[08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md) §2 M6c 行（实施指针已加）、[06-Asset-Pipeline](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §2.2（音频双行收敛为一行）、`THIRD_PARTY.md`（miniaudio 预规划行 → 批⓪ 转正式）、[07-Porting-Matrix](../EngineDesign/07-Porting-Matrix.md)（miniaudio 矩阵行，批⓪ 落）、[M6c.md](../Plans/M6c/M6c.md)（批次拆解总览）、M7a packager（`.baked` 音频为该后缀**首个真实落地类型**，容器字段 M7 只读消费）

## 背景

1. **排序已定**（08 §M6c 2026-09-30 用户拍板）：音频先于 M7a 出包主体开工——`.baked` 音频资产类型随 M6c 定形，packager 一次做全类型。当前全仓 `.baked` **零存量**（磁盘无文件，仅命名约定与 M7 注记），音频是第一个把该后缀做成事实标准的类型，容器格式必须在此定形。
2. **引擎侧零音频代码**（2026-09-24 全栈审查 §7 登记的规划外缺口）：SDL_audio 未用（SDL 只拉了窗口/输入）、无任何音频目录/子系统。
3. **消费端就绪**：`GameEvent` 已含 `Hit/Death/Pickup/LevelUp/WaveStart`（[Events.h:12-31](../../Engine/ECS/Events.h)），svr-test 已订阅（`PlayerBehaviour.cs:117-132`）——音频纯消费，无新事件需求。
4. **金回放纪律约束设计**：组件名无条件入状态哈希流（`StateHash.cpp:75`）——**加 ECS 组件 = 三档金回放必重录**（UIDocument/M5 批② 先例）；World 持有非 ECS 通道 = 零重录（Tween/Fx/Save 先例）。这直接决定播放面的形态切分。

## 决策

### M1 后端与引入：miniaudio 0.11.x 单头，CPM 拉取

- 01 选型表既定（"单头文件、无依赖"），SDL_audio 弃用（能力重叠、抽象层更薄）。
- 版本：**0.11.25**（2026-09-30 核定：用户下载 `miniaudio-0.11.25.zip` 提供本地包；仓根双文件 = `miniaudio.h` 4.1MB amalgamated + `miniaudio.c` 56B 官方实现 TU，恰为 `#define MINIAUDIO_IMPLEMENTATION` + include）。
- 引入方式 = **vendor 落 `Engine/Audio/thirdparty/`**（miniaudio.h + miniaudio.c + stb_vorbis.c + LICENSE 四件，2026-09-30 落位；miniaudio.c 由 `Engine/Audio/MiniAudioImpl.c` 实现 TU 替代编译）——网络持续阻断使原 CPM 主路径不可行，兜底转正；接口面（本 ADR 契约）不变。平台链接零配置：macOS/Windows 框架均运行时 dlopen（CoreAudio/ole32），`dl` 已在引擎链接表（`${CMAKE_DL_LIBS}`）。
- 落位：新目录 `Engine/Audio/`（平铺 .h/.cpp，显式列入 `Engine/CMakeLists.txt` 源表，`Ui/` 两文件粒度先例）；实现 TU = vendor 自带 `miniaudio.c` 直编（vendor 代码警告 `-w` 静音，house 卫生）。miniaudio 链接进 `lemon-engine`（M7a `lemon-game` 运行时要吃；无 Vulkan/SDL 依赖，不违分层）。
- **解码器已核定（0.11.25；竖切批实测修正）**：WAV/MP3/FLAC 内建；**Vorbis 非内嵌、须外供**——粘合层以 `STB_VORBIS_INCLUDE_STB_VORBIS_H` 守卫，要求 stb_vorbis 与 miniaudio 实现同 TU 先后包含。解法 = vendor 第四件 `extras/stb_vorbis.c`（v1.22 公有领域）+ 实现 TU `Engine/Audio/MiniAudioImpl.c`（stb_vorbis 全量 + `MINIAUDIO_IMPLEMENTATION`；消费 TU 以 `STB_VORBIS_HEADER_ONLY` 只取声明，两 TU 宏视角一致）。四格式烤制期全可用，零新增第三方（stb_vorbis 随 miniaudio 包分发）。
- 登记动作（批⓪）：`THIRD_PARTY.md` 预规划行转正式（0.11.25 / public domain 与 MIT-0 双许可择一 / vendor 三件落位）；07 矩阵加行。

### M2 `.baked` 音频容器 v1（本 ADR 核心定形物）

**烤制模型：编辑器导入期一次性解码 + 统一重采样，运行时零解码器。**

- 源 `.wav/.ogg` → miniaudio 解码 → 重采样到 **48000 Hz**、交错 **PCM16** → 写 `.lemon/baked/audio/<guidHex>.baked`。
- **全引擎音频域固定 48 kHz / stereo / 混音 f32**：miniaudio 设备初始化请求固定 sampleRate=48000，设备原生率不符时由 miniaudio 内建总线转换器兜底——**运行时每声部零重采样器、零解码器**（M7a `lemon-game` 不带任何编解码代码；与 M7a "运行时资产层" 的解耦方向一致）。
- 容器头 32 字节（小端）：

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | magic | `"LBA1"`（Lemon Baked Audio v1） |
| 4 | 2 | version | u16 = 1（容器版本号——M7 packager 按此只读消费；v1 有误可升 v2 不破包） |
| 6 | 2 | headerSize | u16 = 32（自校验） |
| 8 | 2 | format | u16：1 = PCM16 interleaved（v1 唯一合法值；2 = float32 预留） |
| 10 | 2 | channels | u16：1 \| 2（>2 拒烤红字） |
| 12 | 4 | sampleRate | u32 = 48000（≠48000 拒载——烤制期已归一） |
| 16 | 4 | frameCount | u32（48k 下上限 ≈ 24.8 小时，充分） |
| 20 | 4 | loopStart | u32 帧（0 = 头；源 `.meta` importer 的秒值烤制期换算取整） |
| 24 | 4 | loopEnd | u32 帧（== frameCount = 尾） |
| 28 | 4 | payloadBytes | u32（= frameCount × channels × 2，加载期自校验） |
| 32 | … | payload | PCM16 交错数据 |

- **落位 `.lemon/baked/audio/`**（项目状态目录，随 gitignore，manifest 同侧先例）：生成物、可随时重烤（源 hash 变更自动重烤）、`git clean -xfd` 后编辑器按需补烤——与 M7a "无需 `.lemon/` 缓存" 判据相容；M7 packager 出包时从源资产现烤入包。
- 源侧 `.meta` importer 段（`SyncMeta` 既有 schema，网格切片同款先例）：`{"importer":{"loop":[起,止(秒)],"preload":bool}}`；`preload` 缺省 = payloadBytes > 1 MiB 走流式。
- 加载策略：SFX 整载入 RAM；长 BGM 流式（ring buffer 256 KiB，专用加载线程填充，主线程只做文件 open + 头解析——**"解码不占主线程"由架构保证**，验收的 0.5ms 预算只剩命令 staging + 空间化数学）。**实现注记（2026-10-01 批①b）**：环为声部级（同 clip 多声部各持环/句柄，BGM 交叉淡出即此形态）；**回卷换位在生产者侧**——环内是线性化帧流，消费者（设备回调）零回卷逻辑；起播主线程另做**预填整环**（≤256KiB 顺序读 ≈ 亚毫秒，"只做 open+头解析"的窄偏差，保首回调零欠载）；欠载帧静音混出并计数（`StreamUnderrunFrames` 观测 + Tick Warn）。
- `.baked` 容器家族口径：本 ADR 只定音频容器；`LBA1` 魔数属音频类型，图集/场景等后续 `.baked` 各自定头（06 §4 双格式策略不变），共同点 = version 字段 + M7 packager 单点消费。

### M3 播放面双轨：AudioChannel（零重录）+ AudioSource 组件 id 31（一次性重录）

- **命令通道 = World 持有 `AudioChannel` 命令表**（非 ECS，不入 StateHash——Tween/Fx 先例）：C# 侧所有播放/控制命令当帧 staging，`AudioSystem` 统一提交 `AudioEngine`。**金回放零影响**。
- **持续声源 = `AudioSource` 组件（id 31，表尾追加）**：挂实体随 Transform 移动的循环/环境声（Unity AudioSource 同构粒度）。字段 24B 冻结：`u64 clipGuid; f32 volume; f32 refDist; f32 maxDist; u16 flags(bit0 loop, bit1 playOnStart); u8 group; u8 pad;`（**2026-09-30 批② 勘误**：原字段序 `u8 group; u16 flags` 自然对齐下为 25→32B，调序为 u16 flags 在前、u8 group 衬其后方保 24B；语义不变，批文件 §5 为准）。**代价 = 三档金回放一次性重录**（组件名入哈希流，UIDocument 先例），批② 内闭环（重录 → 全绿同批勾销）。
- **`AudioSystem`**：`InstallDefaultSystems` 插 Tween 后、ScriptEventDispatch 前（`Systems.cpp:1338-1343` 尾插纪律），**零 RNG 子流、零 ECS 写**（只读 AudioSource+Transform 与相机）→ 自身不引入哈希漂移。职责：监听器（相机位）采样、逐持续声源算衰减/声像、AudioChannel 命令提交。
- 同步动作清单（批②，UIDocument 同款四处）：计数断言 31→32（`tests/engine_tests.cpp:728,777` + `tests/script/main.cpp:84,91`）+ C# 镜像 struct（`Interop/Components.cs` 头注记 + 新行）+ `LayoutTables.cs` 行。

### M4 混音与线程模型

- **设备回调线程混音**（miniaudio device callback）：活跃声部逐样本 f32 累加 → 组增益 → 主增益 → 输出。voice 池 **kMaxVoices = 64**，满时偷最旧一次性声部。
- **命令/参数跨线程 = 单小临界区**：主线程 staging 进 AudioChannel → 每帧一次锁提交；音频回调每块一次锁取走。争用可忽略（256 帧块 @48k ≈ 5.3ms 一锁）。TSAN 进 09 登记项（CI 侧后手）。
- **组模型 = Master + BGM/SFX/UI 三组固定**（音量各自 0..1，D3 拍板确认）。BGM = 单独声部槽，同时仅一条；新 `PlayBgm` 对旧曲交叉淡出（默认 0.5s，参数可 0 = 硬切，D4 拍板确认）。
- **暂停语义**（对齐 M6b bit6 Pause）：循环声源（BGM/loop SFX）声部级挂起，一次性 SFX 自然放完，**UI 组不挂起**（暂停菜单按钮音仍可响）。设备级 `ma_device_pause` 不用（会连 UI 音一起哑）。（**2026-09-30 批② D5 拍板（用户"按建议开工"）**：触发源 = 显式 SDK `Audio.Paused`（游戏暂停态自调），**引擎不自动映射 TimeScale==0**——svr-test 里 Scale=0 大量用于菜单/Spawning/选卡/死亡/清场非暂停态，自动映射会误挂起这些场景的 BGM。）
- **静音降级 = 一等公民**：`AudioEngine::Init` 失败（无设备/CI 无头）→ 静音模式——设备不建、混音跳过、**逻辑声部照常记账**（voice 计数/生命周期与有声模式一致，冒烟断言可用）、所有 API 成功返回。环境变量 `LEMON_AUDIO=off` 强制静音（CI 确定性）。编辑器装配点 = `EditorApp::Run` 的 gameUi_ 段之后（同为"失败红字不阻断"降级服务，UiSubsystem 同款纪律）。

### M5 2D 空间化（坚决不做 3D/DSP 图）

- 监听器 = 活动相机世界位（编辑器 Play = GameView 相机）。
- **衰减 = 线性**：`gain = 1`（d ≤ refDist）→ 线性降到 0（d ≥ maxDist）；组件/PlayAt 各自给 ref/max（默认 256/1024 世界单位，实现期与模板相机尺度对表校一次）。
- **声像 = 等功率增益对**：源 x 相对监听器半宽归一 `[-1,1]` → (l,r) 常数功率增益；mono 复制 + 增益对，stereo 直接增益对。
- 不做：音高/变调、滤波、混响、总线图、3D 空间化、音频可视化——全部登记项（08 §4 砍单口径）。

### M6 C#/C++ 边界：vtable 尾加 7 槽 + `Lemon.Audio`

- `NativeApiVtable` 表尾追加（旧宿主零扰动，animParamSlot/Tween×4/Save×3 先例；~~现 36 槽 → 43~~ **2026-09-30 勘误**：竖切批已 36→40（audioStopAll 为 ADR 集合外增件），批② 尾加 6 槽 → **46 落定**（D5/D6 用户拍板均按建议）：`audioPlayAt / audioBgm / audioBgmStop / audioMasterVol / audioMasterVolGet(D6) / audioSetPaused(D5)`；AudioHooks 四函数退役为 `World::SetAudioBackend(engine, resolverFn, ctx)`——签名实况见[批② 批文件](../Plans/M6c/2026-09-30-b2-source-and-csharp-api.md) §6）。
- SDK `Lemon.SDK/Audio.cs` 静态类（一域一文件惯例）：

```csharp
Lemon.Audio.PlayOneShot(clip, vol = 1f, group = Sfx);      // 一次性，非空间
Lemon.Audio.PlayAt(clip, worldPos, vol = 1f, group = Sfx); // 一次性/循环，2D 空间 → voiceId
Lemon.Audio.Stop(voiceId);
Lemon.Audio.PlayBgm(clip, fadeSec = 0.5f);
Lemon.Audio.StopBgm(fadeSec = 0.5f);
Lemon.Audio.SetGroupVolume(group, v);                      // BGM/Sfx/Ui
Lemon.Audio.MasterVolume { get; set; }
```

- clip 引用 = 资产 GUID（prefab 消费同款，meta hex 常量或表列字符串）；批② 核定是否顺手补 `Assets.GuidOf(relPath)`。
- **不加新 GameEvent**（`Events.cs` handler 数组 16 槽已用 13，余量留命；"播放完"回调用轮询 `Alive(voiceId)` 替代——本批连 `Alive` 都不做，登记项）。
- 音频状态不入 StateHash（表现层，Tween/Fx/UI 先例）——**除 AudioSource 组件自身外零重录**。

### M7 编辑器集成面（批①/批③ 落地清单）

- 资产类型八处（加新类型既有清单）：`AssetType` 枚举尾加 `Audio` + `TypeOf()` 分支（`.wav/.ogg/.mp3/.flac`——D1 已拍板放开）+ `AssetTypeName()` + `KindOf()` + `kLabels` 扩容 + `passType` case + `IconKind::AssetAudio` + 着色；双击 = **试听 play/stop 切换**，tooltip = 时长/采样率/声道/预载。
- Inspector：`FieldHint` 尾加 `AudioRef = 1<<12`（`ComponentRegistry.h:47-61`，RmlRef=1<<11 后继位）→ `InspectorPanel.cpp:499-510` 分派复用 `DrawGuidSlot`；AudioSource 其余字段走通用行。
- 混音面板 "Audio Mixer"：Master + 三组滑条 + 活跃 voice 计数（`PanelRegistry::Add`，OpenByDefault=false，Window 菜单自动列——AnimationPanel 先例）。

## 拍板项（2026-09-30 用户确认，均按建议采纳）

- **D1 源格式范围 ✅ 放开 `.mp3/.flac`**：烤制期解码内建零成本，`TypeOf` 四扩展名；烤后运行时无差别。
- **D2 `.baked` 落位 ✅ `.lemon/baked/audio/<guidHex>.baked`**：生成物语义、M7a git-clean 判据相容。
- **D3 组集合 ✅ 固定 BGM/SFX/UI 三组**：数值预留第四组；不做用户自定义组名。
- **D4 BGM 换曲默认 ✅ 0.5s 交叉淡出**：`fadeSec` 参数可 0 硬切。

## 后果与风险

- **金回放三档一次性重录**（批② 内闭环，UIDocument 先例流程熟）；重录窗口期不合入其他改动。
- **M7 packager 对齐**：容器 v1 定形后 M7 只读消费；`version` 字段保升级通道，v1 设计错误不锁死出包线。
- **miniaudio 拉取网络风险**（本机代理时开时关）：CPM 失败 → 手动 vendor 落 `Engine/Audio/thirdparty/` 并登记，接口面不变（M1 兜底）。
- **线程竞争面收敛在小临界区**：命令提交/取走各一锁每帧/每块；TSAN 进 09 登记项（CI 后手，不阻塞本里程碑）。
- **设备热插拔/独占冲突**（macOS CoreAudio 偶发）：登记项——设备丢失回调 → 静音模式降级（M4 降级路径天然复用），完整热恢复 M8 打磨期再议。
- 排除项再确认：无 DSP 图/中间件/3D 空间化/变调/录制（08 §4 砍单一致；ARPG/塔防/幸存者/增量四品类 2D 用例无此需求）。

## 关联

- [M6c.md](../Plans/M6c/M6c.md)（批次拆解：批⓪–批④）
- [06-Asset-Pipeline](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §2.2 音频行收敛、§4 双格式策略
- [09-Testing](../EngineDesign/09-Testing.md) §13 零重录纪律（本 ADR M3 的判定依据）
- [DevLog 2026-09-30 开工条目](../DevLog/2026-09-30-m6c-kickoff-design.md)
