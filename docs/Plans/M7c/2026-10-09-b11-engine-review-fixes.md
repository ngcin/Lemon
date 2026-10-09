# M7c 批⑪ — 引擎评审修复（review 2026-10-09，65 项四梯队）

Status: planned（2026-10-09 制定；裁决点 D1–D5 待用户追认后开工）

## 0. 背景与目标

来源 = [Reports/2026-10-09-engine-code-review.md](../../Reports/2026-10-09-engine-code-review.md)（基线 main@ab0d35f，commit f1a0275 落盘）：9 专项评审员 + 逐条独立复核，去重 65 项——高危 3 / 中危 23 / 低危 39，复核成立 64（L35 复核不成立关闭）。

**目标**（按主题一「同构双实现，修复只落一半」的教训，本批每处修复都要过一遍 §5 同构对照清单）：

1. 崩溃/数据损坏面清零：H1–H3 + 中危缺陷 14 项（坏数据 abort 面、设备丢失边角、语义错误）。
2. 性能两条线：热路径零分配（M4/M7/M8/M11/M12/M19）+ 装载期索引（M16/M17）。
3. 低危卫生顺手清 + 设计债登记不修码（M23/L34/L39）。

**硬约束**：引擎内核行为面改动的项必须配对应用例；基线 = 单测 34,716 / script-tests 1,818 / ctest 4/4 / 回归 21 / bench fps≥76.5 / 构建零警告，收批时全绿并附增量。修复引用记法 `review 2026-10-09 #N`，DevLog 一条目一文件。

## 1. 子批 b11a — P0 高危 3 项（当日清）

| # | 位置 | 修法 | 验证 |
|---|---|---|---|
| H1 | `Engine/Scripting/ScriptHost.cpp:104`（另 ApplyStructural case2、AttachBehaviour） | 三处 emplace 前加 `g_scene->Alive(ent)` 闸：失效 = 红字 WARN + 返回默认值不注入（口径见 D1）；`GameObject.SetComponent` 同步文档化「失效句柄 = 默认值」契约 | 新增单测：死/版本失效句柄 SetComponent/AddComponent/AttachScript → 池规模不变、registry valid 断言；script-tests 补 C# 侧用例 |
| H2 | `Engine/Assets/PlayCaches.cpp:49-65` | 删内联弱解析器改调 `AnimAsset::ParseClipJson`（注意 loopMode→loop 映射与 SheetAndCell 寻址），或最低限度对 fps/loop/sheet/cell 补 is_number/is_boolean/is_string 预检 | 坏档用例：`"loop":1`、`"fps":"8"`、sheet 数字 → 红字跳过不炸 Play（契约 PlayCaches.h:6） |
| H3 | `Engine/Ui/UiSubsystem.cpp:986`（关联 216-224/386-402） | SetText/SetInnerRml 命中任一追踪容器自身或其祖先时同步 `InvalidateContainers`；或容器态改持弱引用 + 使用前存活校验（二选一，实现时按 Rml 观察者机制成本定） | 新增用例：SetInnerRml 命中容器 id 后 SetItems/SetText 正常、无 UAF；ASAN 门（mac-san）复跑 |

## 2. 子批 b11b — P1 中危缺陷 14 项

**坏数据 abort 面（先清，四条崩溃缝 + 一条 boot 面）**：

| # | 位置 | 修法 |
|---|---|---|
| M13 | `Engine/Assets/AssetIndex.cpp:274` | 装载期对账：sprite 条目 sliceCount ≠ gridCols×gridRows → 清零转全幅（维持「坏账清零、宁缺勿错」口径） |
| M14 | `Engine/Assets/FontBake.cpp:298` | LBF1 头域检：pageW/pageH 非零且 ≤ kPageHMax（仿 LAT1 的 64 位回绕防线，AtlasBake.cpp:354 先例） |
| M15 | `Engine/Assets/AssetIndex.cpp:156` | stoull → HexToGuid 同款手写非抛 hex 解析（AssetTypes.cpp:57），坏段 = 默认 |
| M3 | `Engine/Renderer/RHI.cpp:714` | ImmediateSubmit 对 VK_ERROR_DEVICE_LOST 特判：跳 VK_CHECK abort、WaitIdle 容错、触发 HandleDeviceLost（帧循环同款分支下沉） |
| M21 | `Engine/Serialization/SceneArchive.cpp:49-52` | strtoull 配 endptr 要求消费到串尾，拒前导空白/负号（"eabc"/"e5x" 返回 false 红字） |

**设备丢失边角（两条，接 M3 后同场验证）**：

| # | 位置 | 修法 |
|---|---|---|
| M1 | `Engine/Renderer/RHI.cpp:1589` | BeginOffscreenPass 获取屏障 srcStage/srcAccess 改 FRAGMENT_SHADER+SHADER_READ（对在途采样建立跨提交依赖）；或离屏 RT 按 kFramesInFlight 轮换 |
| M2 | `Engine/Renderer/RmlUiBackend.cpp:573` | GeoAlloc 加世代号，RecreateAfterLoss 递增；ReleaseGeometry 见旧世代直接丢弃句柄只 delete |

**语义/纪律类（五条）**：

| # | 位置 | 修法 |
|---|---|---|
| M5 | `Engine/ECS/FxChannel.cpp:19` | 定长 memcpy → `snprintf(t.text, sizeof t.text, "%s", text)`（World.cpp:26 RtUiChannel 同款） |
| M10 | `Engine/Scripting/dotnet/Lemon.SDK/Events.cs:76` + `DomainManager.cs:303` | 迭代前快照/for 索引容忍尾部增删（GameUI.cs:369 同款，兼顾 Events.cs:4 无分配纪律可走 for 索引方案）；RunPooled 补 cmd.Error 红字 |
| M22 | `Engine/Entry/GameEntry.cpp:170-198` | InputCollector 补 SDL_EVENT_WINDOW_FOCUS_LOST/MINIMIZED 清空 keyDown（Window.cpp:63 同款） |
| M9 | `Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:143` | lemon_scene_event 改 DomainManager 池化域线程投递（lemon_ui_events_dispatch 同款）；同步修正 SceneManager.cs:56/94「域线程同步续跑」注释口径 |
| M18 | `Engine/Audio/AudioEngine.cpp:861`（另 :329/:715） | 锁内 shared_ptr 末引用释放 → swap 到局部、锁外析构（fclose 出临界区） |
| M20 | `Engine/Ui/UiSubsystem.cpp:614` | backend Init 失败分支就地 `impl_.reset()`（或 rmlInitialised 标志守卫 Shutdown） |
| M6 | `Engine/Systems/Systems.cpp:874` | 高速弹扫掠判定：speed·dt > 2·hitRadius 时记录上帧位置做 segment-圆查询（SpatialHash::Raycast 已有休眠实现接入）——**裁决点 D2** |

## 3. 子批 b11c — P1/P2 中危性能 8 项（两条线）

**热路径零分配线（6 项，多数 = 把同文件现成先例搬过去）**：

| # | 位置 | 修法 | 实测口径 |
|---|---|---|---|
| M7 | `Engine/Systems/Systems.cpp:28` | DeclareTeams 差量同步：teamIds 一致时仅 list.clear() 不重建 TeamList（容量复用） | bench-sim 万实体帧分配（F3 面板/计数器归零） |
| M8 | `Engine/Systems/Systems.cpp:269` | all_ 复用 Grid::Build 建桶（Nearest 环搜同款） | 万级 Flee 场帧时长对比 |
| M11 | `Lemon.Entry/Exports.cs:209` | attach/destroy/detach 命令参数打包 → 单次域线程投递（PostBatchTick 池化同款） | 清屏帧尖峰（压测 20k 用例） |
| M12 | `Lemon.SDK/Behaviours.cs:191` | Detach 增实体→(slot,index) 索引（与 M11 同场） | 同上 |
| M19 | `Engine/Audio/AudioEngine.cpp:408` | 流式声部每回调块一次 SPSC Read（min(frames, avail) 批量） | 原子流量计数 |
| M4 | `Engine/Renderer/RHI.cpp:699/1005` | UploadTexture 批量重载（一次 ImmediateSubmit 多张）或共享持久 staging 环 + 延迟回收 | 装载帧时长（111 张 svr-test 帧图 dev 启动） |

**装载期索引线（2 项）**：

| # | 位置 | 修法 | 实测口径 |
|---|---|---|---|
| M16 | `Engine/Assets/AssetIndex.h:108` | Open 时建 guid→下标 unordered_map（spriteId 表注意切片区间语义，可后置） | 万实体 ResolveSpriteRefs 装载帧 |
| M17 | `Engine/Assets/PrefabCache.cpp:68` | Build 期预解析存 Entry——**选型见 D3**（pimpl 封装绕开「json 不出头文件」红线，或二进制组件快照） | 连发 spawn 帧分配 |

## 4. 子批 b11d — 低危卫生 + 登记（不阻塞收批）

- **同构对齐优先清**（主题一遗留的另一半，各 ≤10 行）：L2（RHI 映射写补 flush）、L11（SpatialHash NaN 剪除，查询侧同查）、L27（Rng 断言或改注释）、L15（Fx.Crit 换 Pcg32 + grep 防线测试补上）。
- **其余 L 类**：按报告明细逐项小修或登记（L1/L4/L6/L7/L9/L13/L14/L17/L18/L19/L21/L22/L23/L25/L28/L29/L30/L31/L36/L37）；L35 已复核否决 → 关闭。
- **设计债登记不修码**：M23（59 槽 vtable）、L34（Assets↔Renderer 双向依赖）、L39（World 通道 hub）→ ADR 候选池/08 路线图登记（D4）。

## 5. 同构对照清单（改 A 必查 B，本批落地后沉淀为规约）

| 改动侧（A） | 必查同构（B） |
|---|---|
| PlayCaches 解析（H2） | AnimAsset::ParseClipJson |
| AssetIndex importer 解析（M15） | 同文件其余非抛解析纪律 |
| RHI 映射写路径（L2） | RmlUiBackend flush 纪律 |
| 输入采集（M22） | Window::IsKeyDown 失焦清键 |
| FxChannel 写法（M5） | RtUiChannel snprintf |
| Events 迭代（M10） | GameUI 快照迭代 |
| lemon_scene_event（M9） | lemon_ui_events_dispatch 投递 |
| SpatialHash 坐标转换（L11） | TargetBoard Grid #19 NaN 防御 |

## 6. 裁决点（待用户追认）

- **D1（H1 口径）**：死句柄 SetComponent = 红字 WARN + 返回默认值（建议），还是抛 C# 异常？建议前者——与 SDK「句柄失效 Alive=false 自查」契约一致，不炸游戏。
- **D2（M6 扫掠）**：本批修（Raycast 休眠实现现成、接入成本低；出厂模板 speed=320 不触发但参数空间允许 8192），还是登记到 M8 gameplay 批？建议本批修。
- **D3（M17 选型）**：pimpl 封装预解析（最小改动）vs 二进制组件快照（更彻底、动 Spawn 半边）？建议先 pimpl，快照留 M8。
- **D4（设计债去向）**：M23/L34/L39 三条登记 ADR 候选池（08 路线图 M8+ 段），不修码。
- **D5（b11c 范围）**：8 项全清 vs 先清每帧热路径 5 项（M7/M8/M11/M12/M19；M4/M16/M17 三项装载期可后移——M4 纹理逐张上传属装载/批量导入成本，非每帧路径）？建议全清 8 项，M4 设为唯一可单独后移项（优化非缺陷，滑到 M8 不阻塞收批）。

## 7. 出口判据

1. 构建零警告（mac；H 类与 M1/M2 修复在 mac-san ASAN 构建复跑 smoke）。
2. 单测/script-tests 全绿且**每项行为面修复配有对应用例**（坏档红字、死句柄拒入池、endptr 拒绝、失焦清键、Events 重入、设备丢失模拟路径等）；总数 ≥ 基线 34,716 / 1,818。
3. ctest 4/4、回归 21 步全绿、game/scene/template 三 smoke OK。
4. bench-survivor fps ≥ 76.5 维持；b11c 各项附修复前后实测数字（装载帧、帧时长、分配计数）。
5. §5 同构清单逐行勾销；DevLog 收批条目 + 08 路线图批⑪ 登记。

## 8. 执行序

b11a（当日）→ b11b（坏数据面 → 设备丢失 → 语义类，各子步独立提交）→ b11c（两线并行）→ b11d（顺手清 + 登记）→ 收批出口判据全跑 → DevLog + 路线图落账。预计 b11a 半日、b11b 一日、b11c 一日、b11d 半日。
