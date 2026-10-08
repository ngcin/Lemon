# M7c 批② —— per-资产音频参数落地（机器面全绿 + svr-test 实装路径就绪）

[批文件](../Plans/M7c/2026-10-07-b2-per-asset-audio-fx.md) · 登记来源 [M6c.md](../Plans/M6c/M6c.md) §登记项

## 事件

M6c 收官时缓议的「per-资产音频参数」当日开工当日机器面收官。三参数从全引擎一份全局值（Audio Mixer 试验台滑条）升级为**每个音频资产可覆写**：

1. **引擎**：`AudioEngine.h` 新 `ClipFx`（哨兵语义：浮点 <0/voiceCap 0 = 继承全局，浮点 0 = 显式关）；`ClipData`/移动重载/`RegisterStreamClip` 带参；`Impl::Clip` 存覆写；`PlayLocked` 三查表点（节流窗 :236 / 并发上限 :286 / 微扰 :312）改"clip 覆写优先、哨兵回退全局"；`SanitizeFx` 域归一（NaN→继承、voiceCap 钳 [0,64]、retrigger [0,4]s、jitter [0,0.25]——与 meta 钳域同源）。
2. **透传（preload 同款，零烤制格式改动）**：`AudioItem::fx` → `AudioMount::EnsureLoaded` 两注册点透传。**决策点：不扩 LBA1 头**——侦察确认运行时 AssetIndex 本就逐资产读 .meta（preload 先例、包内含 .meta），fx 是注册期查表值无需烤制期冻结（loop 冻结头里是"秒→帧换算需 frameCount"的特殊性）→ 零版本迁移、零 SeekBakedFrame/头长改动。编辑器 `AssetEntry` 与运行时 `IndexedEntry` 各加三字段（解析故意双份的既有口径），`DbAudioSource`/`IdxAudioSource`/`EnsureClipLoaded` 三适配点填充——**编辑器 Play 与包运行时双路同源**。
3. **meta schema**：importer 段三键 `"retrigger"`(秒)/`"voiceCap"`/`"pitchJitter"`；`ParseAudioImporter`（编辑器）与 `ReadAudioImporter`（运行时）宽容解析 + 钳域（坏值 = 继承，与 loop 段同款不红字）；`SetAudioImporter` 写器（`SetGridSlice` 同款读改写原子，哨兵 = 键不写——老项目零迁移）；Rescan 变更检测 #8 分支扩 fx 三件（热改 = modified → 后台重烤 + 下次进 Play 生效）。
4. **UI**：浏览器瓦片右键「音频参数…」模态（循环点两输入 + 预载勾选 + 三覆写勾选/滑条，勾选关 = 继承全局）→ `SetAudioImporter` + 主动 `RescanAssets`；tooltip 加提示行；Play 中提示"改动下次进 Play 生效"（表资产同口径）。

## 实测（2026-10-07）

- **单测 34,402 → 34,435（+33）**：`TestAudioPerClipFxOverrides`（节流覆写压过全局/显式 0 关/无覆写继承；cap=1 偷最老 + 无覆写仍 kMaxVoicesPerClip 共存；微扰覆写 0 = 两连播逐位相同 + 无覆写继承全局相异）+ `TestAudioImporterConfig`（写入→Rescan 读回 / 哨兵 = 键缺省 / 域外值拒绝回继承 / 非 audio 拒绝）+ 双扫描器夹具扩 fx 三键（快路径/回退两路断言）。
- **ctest 4/4**；**回归 full 20/20 首跑全绿**（含 smoke-drag/smoke-ui 注入族——本会话后台跑未复现 09 §8 假红）；**bench-survivor 门禁 fps=85 ≥ 76.5**（fx 查表在 Play 路径三分支，sim 零接触——零回退直接证据）。

## 真人面（待用户）

svr-test 任一音效（建议 pickup.wav）浏览器右键「音频参数…」设覆写（如重触发节流窗 150ms + 音高微扰关）→ 确定 → 进 Play 对比听感（拾取连响变稀疏/不再"放鞭炮"）；Audio Mixer 全局面行为不变（继续当全局默认 + 试验台）。

## 边界（登记不排）

C# 逐调用覆写 / per-AudioSource 组件级覆写 / 微扰曲线/立体声微扰 / Mixer 全局值持久化 / LBA1 头冻结 fx——见批文件 §4。
