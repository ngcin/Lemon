# M6c 批① —— `.baked` 资产通道正式化：浏览器面 + importer 循环点 + 后台烤制 + smoke 链

Status: done（2026-09-30 当日收口：smoke-audio 第一段 `entries=8 baked=8 meta=8 preview=1/1 => OK` + 引擎单测 **33609 checks**（+6 循环点烤制锁）+ ctest 3/3 + svr 热跑 `8 成功，全部就绪`、**EnterPlay 21.9ms → 7.1ms**（烤制离场后纯装载）。**BGM 流式拆出批①b**（见下"重排决策"）。[DevLog](../../DevLog/2026-09-30-m6c-b1-baked-asset-channel.md)）

## 分解与勾销

| # | 项 | 落点 | 状态 |
|---|---|---|---|
| T1 | 引擎小件 | `BakedClip`：`PeekBakedClip`（只读头——tooltip/smoke 探针）+ `BakeAudioFile` 循环点参数（秒→48k 帧取整钳界，0/0 = 全曲）；`AudioEngine::RegisterClip(std::vector<int16_t>&&, …)` 移动重载（21MB BGM 免双拷贝瞬时翻峰——review 登记项清账） | ✅ |
| T2 | meta importer | `AssetEntry` 增 `audioLoopStart/End`（秒）+ `audioPreload`；`ParseAudioImporter`（每次重扫重读，热改 meta 即生效——网格切片同款口径）；新建音频 meta 写默认 importer 段；svr-test 八个既有 meta 补段 | ✅ |
| T3 | 浏览器五处 | 过滤钮「音频」（kLabels 10→11）+ `passType` case 10 + `KindOf` 拖拽 kind 10 + `IconKind::AssetAudio`（扬声器+声波画布）+ 青色着色；tooltip = 烤制状态（Peek：`已烤 2ch / 112.6s / 48kHz` 或 `未烤`）+ `双击：试听 / 停止` | ✅ |
| T4 | 双击试听 | `EditorApp::TogglePreviewAudio`：同曲再点停 / 换曲顶停旧（0.8 音量 Sfx 组）；**Edit 态可响**——`EnsureClipLoaded` 按需现烤现载（竖切的"试听须进 Play"限制解除） | ✅ |
| T5 | 装载重构 | `EnsureClipLoaded(e)`：map 命中直通，缺烤/陈旧现烤 → 移动装载注册；`MountPlayAudio` 改为其消费方（试听与 EnterPlay 兜底共用一口）；`BakedPathFor`/`BakeStale` 抽公共助手 | ✅ |
| T6 | **后台烤制** | `EnqueueAudioBake`（惰性起工作线程 + cv 队列；`audioBakePending_` 原子计数）双钩位：`RescanAssets` added/modified（sprite GPU 导入同款）+ **开项目 `WarmAudioBakes` 一次性预热**；`StopAudioBaker` 幂等（Run 尾 + 析构双保险——早退路径靠析构收线程）；worker 执行前复查 `BakeStale`（入队到执行间可能已被兜底烤过） | ✅ |
| T7 | smoke 第一段 | `--smoke-audio`（须配 `--project`，真项目资产）：导入识别（entries）→ meta importer 段（meta）→ 后台烤制收敛（上限 60s）→ .baked 逐条 Peek 头校验（baked）→ Edit 态试听双点（preview 起/停）——`EditorAppSmokeAudio.cpp` 新 TU | ✅ |

## 实测

- **EnterPlay 音频成本 21.9ms → 7.1ms**（MainMenu.scene 300 帧跑；烤制已在开项目期后台完成，EnterPlay 只剩装载——7.1ms 里还含快照重建等非音频项）。
- smoke-audio 全链 OK；svr-test 热跑 `进 Play 音频装载：8 成功，全部就绪`（零现烤）。
- 引擎单测 33609（+6：`0.01s/0.05s → 480/2400 帧` 精确锁 + 越界 loopEnd 钳尾锁）；ctest 3/3；全量构建零 error。

## 重排决策：BGM 流式拆出批①b

review 登记的"BGM 流式 ring buffer"原排批①——实现评估后**拆独立微批批①b**：SPSC 环 + 设备回调消费 + 循环回卷换位（consumer 公布逻辑游标、producer 跟随，环复位窗口）的线程正确性需要专注验证，与浏览器面同批塞易出糙活。`audioPreload` 位已入库（T2），批①b 消费。竖切批全量入 RAM（BGM 21.6MB）短期不变——单 BGM 场景无实感差异。

## 实现期发现

1. **EditorEntry 变量名陷阱**：`--smoke-audio` 分支误写 `app.launch`（该处是裸 `launch` 局部）——编译器直拦，零成本。同类：smoke 分发点初版多写了 `StopPlay()`（--smoke-audio 是 Edit 态链，无 Play 可停），写文档前自查删。
2. `MountPlayAudio` 与后台烤制的竞态按"正确性优先"处理：EnterPlay 兜底路径保留同步烤（撞上未烤完的文件 = 该文件同步烤，其余命中缓存）——交互流首开大项目可能仍有单文件烤制耗时，但正确性无竞态（原子写 + mtime 判定）。
3. `fs` 别名放进匿名 namespace 后对同 TU 后续代码可见（unnamed namespace 成员隐式 using）——MountPlayAudio 直接用 `fs::` 成立，无需再开局部别名。

## 遗留

- **批①b**（planned，~1 天）：BGM 流式 ring buffer（ADR M2 口径 >1MiB）+ `audioPreload` 位消费 + 长载耗时红字（同步烤超阈值提示）；
- 浏览器试听**播放中状态可视化**（瓦片角标/波形）与 Inspector AudioSource 槽 → 批②/批③；
- `--smoke-audio` 入回归 16 步 → 批③（smoke 链本体已就位）；
- `RegisterClip` const ClipData& 旧重载仍被单测使用——保留双口径（拷贝语义对夹具合理）。
