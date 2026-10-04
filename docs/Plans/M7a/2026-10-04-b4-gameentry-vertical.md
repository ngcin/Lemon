# M7a 批④：GameEntry + lemon-game 竖切

Status: **done**（2026-10-04：装配序列/主循环/CLI 全落 + 三下沉件（PlayCaches/GameFx/运行时适配器族）+ AssetTypes·AssetIndex 音频勘误 + 回归 18 步（game-smoke 新步，重排后复验全绿）+ ctest 3/3 + 单测 34220 + `--validate` 直渲染路径零验证层报错（D8 A 案关账，见 §6）；完成情况见 §4）

> [M7a.md](./M7a.md) §4 批④ 落名批文件。出口判据：**mac `lemon-game --project demo/svr-test` 四屏全流程零 C++（真人初验——已过 2026-10-04，项目按下方勘误 = vs-survivor 模板副本）**；`git clean -xfd`（删 `.lemon/` 缓存）后再跑成功（manifest 回退扫描机器复验过）；fps 采样 ≥60（自动化档实测 484–1010，交互档 Fifo vsync 60 钳）。回归 game-smoke 步（17→18）绿。
>
> **2026-10-04 验收勘误**：上段验收项目 `demo/svr-test` 系误指——demo 的对战 HUD/升级三选一/首死复活对话全押 RtUi（编辑器叠层，lemon-game 无呈现面，见 §5 勘误行）。**真人验收项目 = vs-survivor 模板副本**（玩法 UI 已全迁 RmlUi）；demo 仅菜单/暂停/设置/结算四屏可用于 lemon-game。

## 0. 开工现场核对（2026-10-04，决定设计的事实）

- **模板/项目入口场景都是"纯 UI + Flow"形态**：`Templates/vs-survivor/Scenes/Main.scene` 与 `demo/svr-test/Scenes/MainMenu.scene` 均只有 UIDocument 声明实体 + GameFlow 脚本实体，**零 SpriteRenderer**——玩法实体（Player/Director/怪）全部运行时经 `Instantiate.Prefab`/SpawnFn 生成。推论：game-smoke 不能拿"visible>0"当无条件断言，必须**注入点击驱动流程进局**（smoke-template 引擎直灌三帧机同款：150 定位 `main.rml#btn-start` 盒中心 →151 down→152 up）——这反而把「UI 点击→UiEvent→C# 流程→prefab spawn→渲染提取」整条竖切变成了机器面端到端。
- **AssetTypeName 自 M6c 起漏 `Audio` 分支**（default → "generic"）：编辑器读侧靠扩展名重派类型无感，但 **manifest 写侧把音频全记成 "generic"**（svr-test 9 件/模板 7 件实证），AssetIndex 快路径信 manifest 类型串 → AudioMount 漏装全部音频。本批一并修（写入面补分支 + 读取面 generic→TypeOf 自愈，双测锁）。
- **manifest 不载音频 importer 字段**（loop/preload 只在 .meta）：AssetIndex 对 Audio 条目补 `.meta` 一次小 IO 读入（Sprite 网格声明同款口径，批③移交清单"manifest 扩字段或回读 .meta"两案取后者——零编辑器写侧改动）。
- **SpriteBatcher::Init 有 colorFormat 参**（动态渲染管线唯一格式依赖，默认 BGRA8UnormSrgb）：直渲染 swapchain 传 `SwapchainFormat()` 显式对齐；UiSubsystem::Init 的 rtFormat 同口径。
- **本机 resize 触发整设备重建**（非仅 swapchain）：TextureStore 无重建路径 → 补 `RebuildAll`（AssetGpuCache 同款 pages_ 清空重导入；前置 = 调用方 AtlasRegistry::Reset + 程序化页重建，编辑器两段式同序）。
- **RtUi/Cards 是编辑器 ImGui 叠层**（ViewportPanels 呈现）：模板已迁 .rml+UIDocument（③d-1/②），lemon-game 不携带（`Lemon.Ui.Set` 兼容 API 在 lemon-game 无呈现面——编辑器特权，登记见 §5）。
- **输入无 ImGui 中间层**：Window 事件观察者单源表（scancode 电平 + 指针/文本/IME 事件），UiKey 差分边沿与 InputState 快照同表派生；InputState 位键与编辑器 Play 路由同款（WASD/箭头→轴、Space=bit4、R=bit5、Esc|P=bit6；WantsKeyboard/AnyModalShown 让出）。

## 1. 件清单

| # | 件 | 要点 |
|---|---|---|
| 1 | `Engine/Entry/GameEntry.cpp` + `Engine/Entry/CMakeLists.txt` | `add_executable(lemon-game)` 链 lemon-engine + lemon-csharp + SDL3（**不链 editor-core/ImGui**——imgui-isolation 断言过）；CLI `--project/--scene/--frames/--smoke/--validate`；装配序列见 ADR-016 M2（ScriptHost 先于窗口，失败响亮退出）；POST_BUILD 暂存 `${LEMON_SCRIPT_DIR}` → exe 旁 `runtime/`（dev 形态；批⑤ packager 同款落位） |
| 2 | entry dll 解析链（清障 #6） | `exe 旁 runtime/` → `LEMON_SCRIPT_DIR` 构建树回退（编译期宏降级为 dev 回退位，包形态零宏依赖）；用户程序集恒 `<root>/.lemon/bin/Game.dll`（构建归编辑器/packager，lemon-game 只消费——缺失红字指路） |
| 3 | pipeline cache（清障 #6） | 显式传 `<root>/.lemon/game/pipeline-cache.bin`（绝对路径，cwd 相对默认不触发）；DeviceDesc 持指针——串生命周期钉在栈变量 |
| 4 | 字体链（清障 #6） | `<root>/Fonts/NotoSansSC-Regular.otf`（packager 拷入位）→ `LEMON_ENGINE_FONT_DIR` 源树回退 → 项目字体（Assets 下 otf/ttf/ttc fallback，LoadProjectFonts 同款） |
| 5 | `Engine/Assets/PlayCaches.{h,cpp}`（下沉①） | 三缓存构建（BuildClipCache 含 .override 集登记 / BuildControllerCache / BuildTableCache）自 EditorContext 下沉，日志字符串与宽容度逐字节保留；`PlayCacheSource` 轻虚接口（Each 按 type / FindSprite / HasClip）；EditorContext 三方法改薄壳（DbPlayCacheSource 适配，scratch 单槽） |
| 6 | `Engine/Renderer/GameFx.{h,cpp}`（下沉②） | Play 表现层段（血条层 251 白精灵双四边形 + 飘字层 252 位图字体）自 ViewportRenderer 下沉；`AppendGameFx(fx, scene, view, whiteSprite, font, fxDt, bars, texts)` 纯函数；渲染帧时钟（lastFxTime_）归调用方；ViewportRenderer Play 路径改薄壳 |
| 7 | AssetIndex 侧运行时适配器族（批③移交清单兑现） | IdxPrefabSource / IdxPlayCacheSource / IdxUiDocSource / IdxAudioSource / IdxSpriteRefSource（SpriteRefSource 同款纪律——AssetIndex 双实现的运行时半边） |
| 8 | hooks 三族 + UI 桥 | SetEditorAssetHooks（spriteOfGuid=AssetIndex / instantiatePrefab=guid→json→InstantiateJson+ResolveTreeScripts——编辑器 InstantiatePrefabAsset 的 Play 态等价）/ SetScriptIoHooks（三档 SaveStore）/ SetUiHooks（ApplyOps/DrainEvents）；SetTextureResolver（guid: 协议 + 相对路径两形态）/ SetDocumentResolver（通道 B） |
| 9 | AssetTypes 勘误 + AssetIndex 音频面 | `AssetTypeName` 补 `Audio → "audio"`（manifest 写入面）；LoadFromManifest 对 "generic" 条目按 `TypeOf(relPath)` 自愈（旧账音频复活）；`IndexedEntry` 增 audioLoopStart/End/audioPreload + `ReadAudioImporter`（快路径/回退扫描两路同值） |
| 10 | TextureStore::RebuildAll | 设备丢失重建（pages_ 清空 + LoadAll；前置 = AtlasRegistry::Reset + 程序化页重建——GameEntry 单回调内同序执行：程序化页 → textures → ui.ReloadAllDocuments） |
| 11 | World 装配 | 默认 20 系统 + `RenderExtractSystem`（SystemStage::Extract 管线驱动形态——批③移交件首次真消费）+ ResolveOrder；相机 follow（CameraFollowState，默认位 640,360） |
| 12 | 主循环 | 编辑器 Play 路径同款纪律：固定步累加器（1/60、追帧 5 步、空转帧 Step(0)）/ 交互 vs 自动化（--frames/--smoke = 每渲染帧恰一步 + alpha=1 + Immediate present，自动化链口径）/ audio tick + 监听器（相机位+视口半宽，一帧延迟口径）/ UI 喂入（指针 Retina 点→像素换算 + IME 锚点）/ D8 A 案直渲染（sprite pass + `ui.Render` 同一动态渲染块内、EndPass 前——离屏块同款唯一合法插入点）/ 存档兜底落盘（退出时三档，编辑器 ExitPlay 同款） |
| 13 | smoke RESULT 行 | `frames/fps/alive/visible/batches/ticks/uidoc/audio/scriptSys/contractErr/hud/cards`；判据 = 装载链在场 + 点击进局（hudShown）+ 动态弹卡（cardsShown——验收日补强）+ 可见精灵 + 零契约错误 |

## 2. 回归（17→18 步）

- `tools/editor-regression.sh`：template-chain 后新增 **game-smoke** 步——夹具 = `cp -R Templates/vs-survivor ${TMP}/game` + `dotnet build Game.csproj -o .lemon/bin`（"构建归编辑器/packager"口径由脚本代行）→ `lemon-game --project … --frames 900 --smoke` grep `game-smoke: .* => OK`；夹具编译失败 = FAIL 步（输出可读）。
- 残留实例前置守卫扩 `lemon-game`（同争 GPU；交互局不自动杀、headless 旗标残留清）。

## 3. 门格

构建 0 error（含 imgui-isolation——批④ 曾被自注释"ImGuiKey"字样误中，改措辞）→ ctest 3/3 → 单测 **34220**（34210→+10：manifest 写入面 "audio" 类型串锁 + 快路径 importer 字段 + generic 自愈 + 回退扫描同值）→ 回归 **full 18/18**（game-smoke 首跑绿）→ manifest 删除（git clean 判据模拟）后 game-smoke 复跑绿（回退扫描 + guid 归一）→ svr-test 拷贝交互档启动全链就绪（Fifo/相机跟随/零错误）→ 自动化档 fps 484–1010（交互档 vsync 钳 60）。

## 4. 完成情况（2026-10-04 收口）

| # | 状态 | 验证摘要 |
|---|---|---|
| lemon-game 目标 | ✅ | 构建/链接/暂存全绿；exe 旁 runtime/ 齐备（Lemon.Entry/SDK/runtimeconfig） |
| 装配序列 | ✅ | svr-test 拷贝：clip 表 5 + 动画集 7 + 状态机 1 + 表 4 全建（引擎 PlayCaches）；uidoc 4/4、音频 9（流式 2）、脚本系统 1、相机跟随命中 |
| 竖切端到端 | ✅ | 模板夹具 900 帧：点击进局（hud=1）→ Gem.prefab 运行时实例化（HookInstantiate 链）→ alive=39 visible=25 batches=6 contractErr=0 |
| manifest 回退 | ✅ | 删 manifest+.bak → 红字警告 + 36 条回退扫描 + guid 归一 → smoke 仍 OK |
| 音频勘误 | ✅ | 单测四锁（写入串/快路径字段/自愈/回退同值）；svr/模板音频 9/7 全装载（修复前快路径会漏装全部） |
| 回归 18 步 | ✅ | full 首跑 18/18（game-smoke 绿；既有 17 步零扰动——PlayCaches/GameFx 下沉的行为对拍面） |
| 真人验收 | ✅ **过 2026-10-04** | ①`lemon-game --project build/mac/game-fixture`（vs-survivor 模板副本；**验收日勘误：原写 demo/svr-test 系误指**——demo 玩法 HUD/卡片/复活走 RtUi 无呈现面，且首死即卡死）四屏全流程零 C++ **用户实测过**②窗口 resize 后渲染复原（设备重建链）**用户实测过**③`--validate` 验证层零报错——机器复验（300 帧 smoke 零 VALIDATION-ERROR，§6） |

## 5. 登记项（移交）

- `Lemon.Ui.Set`/`Ui.ShowCards` 兼容 API 在 lemon-game 无呈现面（编辑器 ImGui 叠层特权）——项目侧迁移 .rml 已是既定方向（③d-1 口径），不改；
  **验收日放大后果（2026-10-04）**：`demo/svr-test` 对战 HUD（hp/xp/time/kills/飞剑数行）、升级三选一、首死复活对话**全在 RtUi 通道**（`Ui.Set`/`Ui.ShowDialog`/`Ui.CardPick`）→ lemon-game 全缺，首死无复活对话直接卡死——**demo 不可用于批④真人验收**（玩法面验收用 vs-survivor 模板副本）。demo 的 RtUi→RmlUi 迁移归 ADR-014「M8 前定去留」线。据此补强 game-smoke：RESULT 增 `cards=` 观察位并入 ok 判据（模板四跑 900 帧确定性 cards=1，堵动态弹卡链回归缺口）；
- 交互档 fps 打印 120 帧一拍（`[lemon-game] fps=… alive=…`）——Profiler 级面板归 M8；
- `--scene` 覆盖入口场景已可用（诊断形态）；
- smoke 点击驱动的按钮 id（`main.rml#btn-start`）与 smoke-template 共源——模板改版两处同步（smoke-template 先例纪律）；
- **review 2026-10-04 登记三项**：① `--smoke` 不带 `--frames`（含负数）= 自动化档（Immediate present）+ 无帧上限 → 无限循环烧 GPU——现行消费面（回归/脚本）恒带 `--frames`，CLI 校验归后续微批；② game-smoke 步在 lemon-game 缺席（`LEMON_BUILD_SCRIPTING=OFF` 构建树）时 SKIP 不 FAIL——"18 步"契约在无脚本树弱化为 17，mac 预设恒构建备查；③ `HookInstantiate` 每次读盘 = 编辑器 `InstantiatePrefabAsset` 逐字面同款（parity 非回归），高频 spawn 的 guid→json 缓存化归 M8 性能线；
- **`lemon-game --validate` 的验证层解析本机坑（机器级，非代码缺陷）**：brew 的层清单 `library_path` 为裸文件名（`/usr/local/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`），本机新版 dyld 默认回退表已不含 `/usr/local/lib` → dlopen 失败 → `VK_ERROR_LAYER_NOT_PRESENT`（RHI.cpp:273 断言；5 行最小复现件同症）。lemon-editor 在同机稳定可过（机制未完全定位——同为裸名 dlopen 却命中 Cellar，疑似编辑器进程启动期 env 差异 + SIP 对 `ps eww` 的 DYLD 变量隐藏）。**绕行 = `DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib lemon-game … --validate`（实测全绿：300 帧 smoke + 零 VALIDATION-ERROR）**；AGENTS.md 本机坑同记。产品路径（无验证层）不受影响。

## 6. --validate 实测（D8 A 案关账）

ADR-016 风险 #1（RmlUi 直渲染 swapchain 未知数）：`DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib ./lemon-game --project <模板夹具> --frames 300 --smoke --validate` → 验证层启用 + `RESULT … => OK` + **零 VALIDATION-ERROR/WARN 输出**——双 pass 直渲染（sprite Record + ui.Render 同一动态渲染块）在验证层下干净；`SpriteBatcher::Init(colorFormat=SwapchainFormat())` 与 `UiSubsystem::Init(rtFormat=SwapchainFormat())` 的显式对齐成立。

