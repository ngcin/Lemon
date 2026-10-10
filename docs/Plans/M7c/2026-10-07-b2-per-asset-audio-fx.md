# M7c 批② —— per-资产音频参数（听感覆写三件：节流窗/并发上限/微扰幅度）

Status: done 机器面（2026-10-07 当日开工当日收口——单测 34,402→**34,435**（+33：覆写回退链/meta roundtrip/双扫描器夹具）+ ctest 4/4 + 回归 **full 20/20 首跑全绿**（含注入族，本会话后台未复现假红）+ bench-survivor 门禁 **fps=85 ≥ 76.5**；[DevLog](../../DevLog/2026-10-07-m7c-b2-per-asset-audio-fx.md)；**真人听感 ✅ 2026-10-10 用户过（[验收记录](../../DevLog/2026-10-10-acceptance-m7c-b2-b3.md)）**；**review 轮 ✅ 2026-10-08**（核查七面无功能缺陷；三小项全修——钳域常量单源化 `kFxRetriggerMaxSec`/`kFxPitchJitterMax` + 两处注释刷新，复测 34,435 逐位不变，[review DevLog](../../DevLog/2026-10-08-m7c-b2-review.md)）

> 来源与动机：M6c 两轮听感热修（破音→母带软限幅 + 同 clip 并发上限；"放鞭炮"→重触发节流 + 音高微扰）的三个参数至今**全引擎一份全局值**（Audio Mixer 工具窗滑条 = 过渡试验台）。拾取音想节流宽、挥砍想密；UI 点击想零微扰、脚步想大变化——全局值按下葫芦浮起瓢。本批把三参数做成**每个音频资产可覆写**，.meta 为作者真源。
>
> 动工前核对：ADR-015（音频架构与 .baked 格式）、06 §2.1（.meta importer 段规约——本批同款口径）、M6c 批③ 裁定注记（缓议理由与本批启用的边界）。

## 0. 设计定案（侦察后锁定）

- **透传路线 = preload 同款（meta → AudioItem → 注册期入 Clip），不动 LBA1 烤制格式**。侦察结论：运行时 AssetIndex 本就逐资产小 IO 读 .meta（preload 先例，包内含 .meta），fx 参数照走此路 → 零格式版本迁移、零 SeekBakedFrame/头长改动。loop 冻结在 .baked 头是烤制期换算帧的特殊性，fx 是注册期查表值，无需冻结。
- **覆写语义（哨兵约定）**：
  - `retriggerCdSec`：**< 0 = 继承全局**（Mixer/默认 45ms）；**0 = 该 clip 关节流**；>0 = 覆写秒数（meta 钳 [0, 4]）。
  - `voiceCap`：**0 = 继承**（kMaxVoicesPerClip=4）；1..64 = 覆写（=1 即"此声永不叠发"；钳 [1, kMaxVoices]）。
  - `pitchJitter`：**< 0 = 继承全局**（默认 ±2%）；**0 = 该 clip 关微扰**；>0 = 覆写幅度（meta 钳 [0, 0.25]）。
- **meta JSON 键**（importer 段内，与 loop/preload 平级）：`"retrigger"`(秒) / `"voiceCap"`(整数) / `"pitchJitter"`。缺省键 = 继承（老项目零迁移）。
- **生效链**：右键菜单编辑 → SetAudioImporter 原子写 .meta → Rescan 热改即"modified"（#8 同款语义）→ **下次进 Play 生效**（clip 注册期消费；Play 中当前局不变——与表资产同口径，弹窗提示行交代）。
- **不碰**：LBA1 头/BakeAudioFile 签名；Audio Mixer 全局面（继续当全局默认 + 试验台）；C# 逐调用覆写 API；EQ/混响/新混音特性。音频 = 表现层，不入 StateHash/回放（本批零 C# 面，红线天然不触发）。

## 1. 改动分解（文件 → 落点）

| # | 文件 | 落点 | 内容 |
|---|---|---|---|
| 1 | `Engine/Audio/AudioEngine.h` | PlayParams 邻域 + ClipData + Register 族签名 | `ClipFx` 结构（三字段哨兵语义）；`ClipData::fx`；移动重载/`RegisterStreamClip` 尾参 `const ClipFx& fx = {}`；§重触发注释区"per-资产覆写归 meta（批③）"字样收口为本批 |
| 2 | `Engine/Audio/AudioEngine.cpp` | Impl::Clip（:50）+ PlayLocked 三查表点（:236 节流 / :286 上限 / :312 微扰）+ RegisterClip×2/RegisterStreamClip（:589-634） | Clip += `ClipFx fx`；三查表点改"clip 覆写优先、哨兵回退全局"；注册路径拷贝 + 钳域（voiceCap→[0,64]，负浮点归一 -1） |
| 3 | `Engine/Audio/AudioMount.h/.cpp` | AudioItem（:26）+ EnsureLoaded 两注册点（:83/:91） | `AudioItem::fx`；注册调用透传 |
| 4 | `Editor/Assets/AssetDatabase.h/.cpp` | AssetEntry 音频段（:60）+ ParseAudioImporter（:137）+ Rescan 变更检测（:659）+ 新 `SetAudioImporter`（SetGridSlice :866 同款读改写原子） | 三字段解析（宽容 + 钳域，坏值 = 继承）；fx 变更并入 #8 modified 分支；写器全五值（loop 对 + preload + fx 三件），内存 entry 同步刷新 |
| 5 | `Engine/Assets/AssetIndex.h/.cpp` | IndexedEntry（:37）+ ReadAudioImporter（:86） | 运行时侧同款解析（与编辑器解析"故意双份"既有口径一致） |
| 6 | `Engine/Entry/GameEntry.cpp` | IdxAudioSource::EachAudio（:321） | item.fx 填充（包运行时路） |
| 7 | `Editor/App/EditorAppScripts.cpp` | DbAudioSource::EachAudio（:196）+ EnsureClipLoaded（:278） | item.fx 填充（编辑器 Play 路） |
| 8 | `Editor/Panels/AssetBrowserPanel.cpp` | asset_ctx 菜单（:369）+ 新模态 | Audio 类型加「音频参数…」：循环点两输入 + 预载勾选 + 三覆写（勾选启用 + 滑条/输入，显示"继承全局(当前 X)"基线值）→ SetAudioImporter → Rescan；提示行"改动下次进 Play 生效"；tooltip 加参数摘要行 |
| 9 | `tests/engine/AudioTests.cpp` | 新 `TestAudioPerClipFxOverrides` | 三段：节流覆写（100ms 覆写 vs 全局 50ms——窗内丢/窗过收/无覆写 clip 走全局）；并发上限覆写（cap=1 第二发偷第一发镜像 TestAudioVoiceCapSteal 断言形态；无覆写仍 4）；微扰覆写（全局 0.05 + clip 覆写 0 → 两连播逐位相同；覆写 0.1 → 相异）——ramp PCM 观测同 :902 先例 |
| 10 | `tests/engine/AssetsTests.cpp` | 音频夹具（:834）+ 快/回退两路断言（:876/:912）+ 新 SetAudioImporter roundtrip | 夹具加三键；解析断言等值 + 越界钳域反例；写器 roundtrip（含 inherit = 键缺省不写）|

## 2. 工作量与顺序

引擎侧（1–3）+ 引擎单测（9）先行独立可验 → meta/装载透传（4–7）→ UI（8）+ 资产测（10）。预估 1 日内。

## 3. 出口判据（08 §0 M7c 行对表）

- 单测新增 ≥ 两件全绿（覆写回退链 / meta roundtrip+钳域），checks 34,405 → N（数字记 DevLog）；ctest 4/4。
- 回归 full 20 步（本会话后台跑注入族假红 = 09 §8/§9 既有口径复跑判读）；bench-survivor 门禁不降（fx 查表在 Play 路径三分支，sim 零接触）。
- 真人面（待用户）：svr-test 浏览器右键任一音效（建议 pickup）设覆写（如 retrigger 150ms + pitchJitter 0）→ 进 Play 听感对比；Audio Mixer 全局面行为不变。
- 文档落账：06 §2.1 音频 importer 字段表补三键；M6c.md 登记项清账注记；DevLog 条目。

## 4. 登记不排（砍单防线）

C# `Lemon.Audio` 逐调用覆写重载；per- AudioSource 组件级覆写（组件级已有 volume/pan/group 足够）；微扰曲线/立体声微扰；Mixer 全局值持久化；LBA1 头冻结 fx（meta 透传已闭环，格式不动）。
