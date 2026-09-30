# 2026-09-30 M6c 批①：`.baked` 资产通道正式化（浏览器面 + importer + 后台烤制 + smoke 链）

## 事件

真人听感验收过（MainMenu 完整跑局零调整意见）后，批① 当日开工当日收口。**EnterPlay 音频成本 21.9ms → 7.1ms**（烤制离场——竖切批 review 登记的 ≈230ms 同步顿就此清账）。

## 落地

- **引擎小件**：`PeekBakedClip`（只读头）；`BakeAudioFile` 循环点参数（秒→48k 帧取整钳界）；`RegisterClip` 移动重载（21MB BGM 免双拷贝）。
- **meta importer**：`AssetEntry` 增 loop 秒值 + preload 位；`ParseAudioImporter`（重扫重读 = 热改即生效）；新建音频 meta 写默认段；svr-test 八 meta 补段。
- **浏览器五处**：「音频」过滤钮（kLabels 11）、拖拽 kind 10、`IconKind::AssetAudio` 扬声器图标、青色着色、tooltip 烤制状态（Peek 头）。
- **双击试听**：`TogglePreviewAudio`（同曲再点停/换曲顶停旧；0.8 音量）——**Edit 态可响**（`EnsureClipLoaded` 按需现烤现载，解除竖切"试听须进 Play"限制）。
- **后台烤制**：惰性工作线程 + cv 队列；双钩位 = `RescanAssets` added/modified（sprite GPU 导入同款）+ 开项目 `WarmAudioBakes` 预热；worker 执行前复查 mtime 防重复烤；`StopAudioBaker` 幂等（Run 尾 + 析构收早退路径）。
- **smoke-audio 第一段**：`--smoke-audio --project <含音频工程>`——导入识别 → meta 段 → 后台烤制收敛（60s 上限）→ .baked 逐条 Peek → Edit 态试听起/停。

## 实测

- `editor-smoke audio: entries=8 baked=8 meta=8 preview=1/1 => OK`；`[smoke-audio] OK`。
- 引擎单测 **33609 checks**（+6：`0.01s/0.05s → 480/2400 帧` 精确锁 + 越界 loopEnd 钳尾）；ctest 3/3；全量构建零 error。
- svr 热跑 `8 成功，全部就绪`（零现烤）；MainMenu 300 帧 `errors=0`。

## 决策与发现

1. **BGM 流式拆出批①b**：SPSC 环 + 设备回调消费 + 循环回卷换位的线程正确性需要专注批验证，不与浏览器面同批塞。`audioPreload` 位已入库待批①b 消费。
2. **EnterPlay 兜底同步烤保留**：交互流首开大项目若撞上后台未烤完的文件，该文件同步烤（其余命中缓存）——正确性优先（原子写 + mtime 判定），无竞态。
3. 小坑两枚（编译器/自查即拦）：EditorEntry 裸 `launch` 局部误写 `app.launch`；smoke 分发点多写 `StopPlay()`（Edit 态链无 Play 可停）。

## 下一步

批②（2D 声源 + C# 正式化：AudioChannel/vtable 7 槽/AudioSource id 31 + 三档重录/BGM 单槽引擎侧/D4 淡出/Time.Scale=0 暂停）——批①b 流式可插其前后。
