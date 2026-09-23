# Lemon 引擎 `Engine/` 代码 Review 报告

- **日期**：2026-09-23
- **范围**：仅 `Engine/`（约 11.8K 行 C++/C#，9 个子系统）。未审 `Editor/`、`Samples/`、`tests/`、`spike/` 等。
- **结论一句话**：内核整体工程质量高、注释与「本机坑」纪律清晰；未发现会导致常规路径立即崩溃的 Critical。但有 **3 个 High**（渲染排序错乱、粒子 spriteId 越界、ECS 悬垂引用）与一批 **Medium**（脚本宿主空转/双重 emplace、C# ALC 卸载注册表未清、SDL 泄漏、Quality 构造缺陷等）值得优先处理。

---

## 一、Review 过程（方法）

1. **分域并行**：按子系统拆成 6 条 review track 并行深读（Renderer-RHI、Renderer-高层、C++ 脚本宿主、C# SDK、ECS+Core、Systems/Components/Serialization/Physics2D/Platform）。
2. **只报实证**：要求每条 track 只报能引用真实 `file:line` 与真实代码的问题，禁止猜测。
3. **主控复核**：**每一条 High/Medium 发现都由我重新读取真实源码核实**后才采信；子代理的错误判断已当场纠正（见 §六）。
4. **过程插曲**：ECS track 曾派生子代理去「核实 EnTT 版本号」卡住；该事实已由 `AGENTS.md`（EnTT 3.15.0）与 ECS 结论本身确认，子代理冗余，已停止。RHI-core / Systems 两条 track 曾静默停止，已 resume 取回结论。

> 采信标准：标注 ✅ 者为我亲自读码确认；子代理报但我未能独立证实者为「待复核」，不计入正式结论。

---

## 二、结论总览（按严重度）

| # | 严重度 | 子系统 | 位置 | 问题 |
|---|--------|--------|------|------|
| 1 | **High** | Renderer | `Renderable.cpp:165-184` `Particles.cpp:143-153` | 分批按「首次遇到序」而非 `sortKey` 排 → 跨层/跨图集绘制顺序错 |
| 2 | **High** | Renderer | `Particles.cpp:116` | 粒子 `spriteId` 默认 0，`GetSprite(0)` 无保护 → 越界/断言 |
| 3 | **High** | ECS | `Hierarchy.cpp:97-106` | `SceneSetParent` 持 `c` 跨 `Emplace<Hierarchy>` → EnTT 重分配后写悬垂引用 |
| 4 | Medium | Renderer | `Quality.h:39`(+:70) | 构造器未用 `start` 初始化 `params_` → 非 High 启动长期用 High 预算 |
| 5 | Medium | Renderer | `Renderable.h:23-24` / `SpriteBatcher.cpp:56-61,16-17` | 位宽(blend:4/filter:2) 超过数组(4/2)，`MakeBatchKey` 不钳 → OOB |
| 6 | Medium | Renderer | `Renderable.cpp:10-29`(vs :38) | `Create()` 不 bump `simVersion_` → 新实体在缓存命中帧不可见 |
| 7 | Medium | Renderer | `Renderable.cpp:138` `Particles.cpp:115` | 键表上限仅靠 `LEMON_ASSERT` → release 下栈越界写 |
| 8 | Medium | Renderer | `Renderable.cpp:114` `Particles.cpp:116` | 未调 `IsValidSprite`，只查 `SpriteCount` → 空洞 sentinel 漏过 |
| 9 | Medium | Renderer | `SpriteBatcher.cpp:25-35` | 设备丢失回调捕获裸 `this`，无析构/反注册 → UAF |
| 10 | Medium | Scripting(C++) | `ScriptHost.cpp:346` | `char buf[4096]` 未初始化即当字符串解析 |
| 11 | Medium | Scripting(C++) | `ScriptHost.cpp:335,406,426` | attach/destroy 回调未设 `g_world/g_scene` 窗口 → 托管 Awake/OnDestroy 内 native 调用静默空转 |
| 12 | Medium | Scripting(C++) | `ScriptHost.cpp:413,425` | `ApplyStructural` 无条件 `Emplace` → 双重 emplace 池损坏 |
| 13 | Medium | Scripting(C++) | `ScriptHost.cpp:272` | `countFn` 为空仍跑 `forEachFn` → 预留不足→realloc→块指针悬垂→C# 野读 |
| 14 | Medium | C# SDK | `DomainManager.cs:133,173-177` | Unload/Reload 未 `Reset()` Scripting/Events/SceneOps → 旧 ALC 类型被根住，卸载永判 0 |
| 15 | Medium | C# SDK | `Events.cs:37-41` | 无 `Unsubscribe` → 订阅表无界增长 + 根住已毁/旧域实例 |
| 16 | Medium | Serialization | `SceneArchive.cpp:264`(vs :322-370) | `Save` 写 `name`、`Load` 从不读回 → 场景名往返丢失 |
| 17 | Medium | Platform | `Window.cpp:67` | SDL drop 文件 `ev.drop.data` 未 `SDL_free` → 每次拖入泄漏 |

> 另有约 20 项 **Low**（见 §三-C），以及若干「已复核不成立」的子代理误报（见 §六）。
>
> **2026-09-24 修复轮**：14 条 Medium 全部修复（批A `ff16bd3` / 批D `45b4da8` /
> 批B `14f95df` / 批C `91d5b31` / M9 `00a7179`），逐条状态见 §三-B 各条目标注；
> 核实与修复过程见 DevLog 2026-09-24 五条目。Low 段未动（观察项台账）。

---

## 三、详细发现

### A. High（建议优先修）

**H1 — 渲染分批顺序错乱（绘制次序错误）** ✅
- 位置：`Engine/Renderer/Renderable.cpp:165-184`、`Engine/Renderer/Particles.cpp:143-153`
- 机制：`Extract` 按键 `(图集, blend, filter, layer)` 分桶，但**桶按「首次遇到序」排布**；桶内 `std::sort`（`Renderable.cpp:178-184`）只修桶内次序，**修不了跨桶**。`sortKey` 虽把 `sortingLayer` 放在最高位（`:149`），最终数组顺序却是创建序。
- 触发：高 `sortingLayer` 实体比低层实体先创建（如 player 先于 floor）→ 高层被画到低层**下面**。直接违背 `Renderable.h:98` 的「SortingLayer → 批键 → order → seq，稳定」契约。
- 修复：在偏移分配前先按 `(key.layer, key.hash)` 排 `slots`；或对 `packets_` 末次全量 `std::sort(by sortKey)`。

**H2 — 粒子 `spriteId` 越界** ✅
- 位置：`Engine/Renderer/Particles.cpp:116`
- 机制：`atlas.GetSprite(p.spriteId)` 无任何校验；而 `ParticleData::spriteId`/`EmitterConfig::spriteId` **默认 0**（`Particles.h:24,43`）。任何未显式设 sprite 的发射器都会 `GetSprite(0)` → 断言，或（关断言后）读 `sprites_[-1]` 把垃圾 `atlasIndex` 送进 bindless 槽。精灵路径有保护（`Renderable.cpp:114`），粒子路径没有。
- 修复：仿精灵路径加 `if (p.spriteId==0 || p.spriteId>atlas.SpriteCount()) continue;`（或 `!atlas.IsValidSprite(...)`）。

**H3 — `SceneSetParent` 悬垂引用（内存损坏）** ✅
- 位置：`Engine/ECS/Hierarchy.cpp:97-106`
- 机制：`Hierarchy& c = ...Get/Emplace(child)`（:97-98）取好后，`:99-100` 可能对 `newParent` 执行 `s.Emplace<Hierarchy>(newParent)`；EnTT 3.15 的 packed storage 在扩容时**令所有同型引用失效**，于是 `:101-103` 的 `c.parent/prev/next` 写入落到已重分配的存储。仅当 `newParent` 原本无 `Hierarchy` 且触发扩容时出现，是间歇性 heisenbug。
- 触发场景：场景 `Hierarchy` 存储增长后，对一个「还没有 Hierarchy 的父」`SetParent`。
- 修复：先分别确保两组件存在（先 emplace newParent），**再**取两个引用；切勿持 `c` 跨 `p` 的 emplace。

### B. Medium

**M4 — `QualityManager` 构造缺陷** ✅ `Quality.h:39`(+:70)` → 已修（批A `ff16bd3`）
- `: tier_(start)` 只初始化 `tier_`，`params_` 用默认成员初始化器恒为 `TierParams(High)`（:70）。`QualityManager(QualityTier::Low)` → `Current()==Low` 但 `Params().particleBudget==100000`。且 `Low` 在 `Update()` 提前 return（:48）永不再降，故**启动档非 High 时粒子预算永远错**。修复：`: tier_(start), params_(TierParams(start)) {}`。

**M5 — 批键位宽 > 数组** ✅ `Renderable.h:23-24` / `SpriteBatcher.cpp`` → 已修（批A `ff16bd3`）
- `blend:4`(0–15) vs `pipelines_[4]`；`filter:2`(0–3) vs `samplerSlots_[2]`。`MakeBatchKey`（:42-43）不钳值，数据驱动配置给 `blend≥4`/`filter≥2` → `pipelines_[5]`/`samplerSlots_[3]` 越界读 → 传垃圾句柄给 `BindPipeline`。修复：`MakeBatchKey` 内 `& 3u`/`& 1u`，或 `Record` 加断言。

**M6 — `Create()` 不失效提取缓存** ✅ `Renderable.cpp:10-29`(vs `Destroy` :38)` → 已修（批A `ff16bd3`）
- `Destroy()` bump `simVersion_`，`Create()` 不 bump。暂停态（无 `BeginSimTick`）下 debug 生成实体 → 缓存命中返回旧 `packets_` → 实体直到下次 sim tick 才可见。修复：`Create()` 里 `++simVersion_`。

**M7 — 键表上限仅靠断言（release 栈越界）** ✅ `Renderable.cpp:138` `Particles.cpp:115`` → 已修（批A `ff16bd3`）
- `slots[kMaxSpriteKeys=64]`/`[kMaxParticleKeys=16]` 仅有 `LEMON_ASSERT`；关断言后超限写 `slots[si]` 越过栈数组。两个上限都可达（10 图集×2 混合×3 层=60；8 粒子精灵×2 层=16）。修复：超限时 `++stats_.droppedKeys; continue;` 而非断言。

**M8 — 未过滤空洞 sprite** ✅ `Renderable.cpp:114` `Particles.cpp:116`` → 已修（批A `ff16bd3`；粒子半边此前已随 H2 覆盖）
- `Atlas.h:39-40` 明确「空洞页采样越界，渲染侧须先过滤」，`AddSpriteAt` 用 sentinel `kHoleAtlasIdx=0xFFFFFFFF` 预留空洞。渲染只查 `SpriteCount()` 上界，注销后的 sprite id 仍落在界内但 atlasIndex=0xFFFFFFFF → 采样不存在槽。修复：改用 `atlas.IsValidSprite(id)`。

**M9 — 设备丢失回调裸 `this`** ✅ `SpriteBatcher.cpp:25-35`` → 已修（`00a7179`，排查轮分批漏排后补）
- `device.AddRecreateCallback("SpriteBatcher", [this]...)` 捕获裸 `this`，`SpriteBatcher` 无析构/反注册。若 batcher 先于 device 销毁（按场景/热重载），设备丢失时回调写垂悬 `this` → UAF。修复：加析构/`Shutdown()` 反注册，或用 generation token。

**M10 — 未初始化缓冲当字符串解析** ✅ `ScriptHost.cpp:346`` → 已修（批B `14f95df`）
- `char buf[4096];` 未初始化，`n=behavioursListFn_(buf,...)` 后 `(void)n` 丢弃返回值，直接 `for(const char* p=buf; *p;)` 走栈。托管侧失败/写空 → 走未初始化内存 → 垃圾类型名或（无 NUL 时）栈越界读。修复：`= {}`，`n<=0` 早退，强制 `buf[n]='\0'`。

**M11 — native API 窗口未覆盖 attach/destroy** ✅ `ScriptHost.cpp:335,406,426`` → 已修（批B `14f95df`，行为变更经金回放三档零重录证明）
- `ApplyStructural`/`AttachBehaviour` 调 `scriptsAttachFn_`/`scriptsDestroyFn_` 时未像 `TickBatch`(:315-319)/`DispatchEvents`(:454-461) 那样设 `g_world/g_scene`。托管 `Awake`/`OnDestroy` 内调 `Ui.Set`/`Time.Scale`/`Spawn` → 静默空转（`NativeRtUiSet` 等早退）。修复：在这两处也设窗口。

**M12 — `ApplyStructural` 双重 emplace** ✅ `ScriptHost.cpp:413,425`` → 已修（批B `14f95df`）
- `case 2`/`case 4` 无条件 `Emplace`，而异处（`NativeWrite`:42-43、`AttachBehaviour`:329-334）都做 get-or-create。对已有组件二次 emplace = entt 池损坏 → AV（正是周边注释反复强调的根因）。修复：emplacing 前先 `TryGet`。

**M13 — 预留/悬垂指针隐患** ✅ `ScriptHost.cpp:272`` → 已修（批B `14f95df`）
- 预留总量全凭 `countFn`；代码容忍 `countFn==nullptr`（`n=0`）却仍无条件跑 `forEachFn`。若 `countFn` 为空/少报而 `forEachFn` 仍产出实体 → vector 扩容 → 已存于 `blockBuf_` 的 `blk.entities/comps` 与 `fr.blocks` 指针全部悬垂 → C# 线性步进野读。修复：`countFn` 为空即跳过该系统并告警，或 `forEachFn` 后重取块指针。

**M14 — ALC 卸载未清注册表** ✅ `DomainManager.cs:133,173-177`(vs `LoadScript` :100-104)` → 已修（批C `91d5b31`，先清根后卸载）
- `UnloadScript` 只置空 `s_tickFn/s_asm/s_alc`；`ReloadScript` 只 `Behaviours.Reset()`。二者都不清 `Scripting.s_systems/s_queries`、`Events.s_handlers`、`SceneOps` 就 `alc.Unload()` → 旧 ALC 类型被 Entry-ALC 静态根住 → `weak.IsAlive` 恒真 → `UnloadScript` 每次都返回 0、`LeakCount` 永不归零（即便 runtime 将来修复 pin）。`LoadScript` 是清的（:100-104），属不对称疏漏。修复：Unload/Reload 的 `Post(...)` 里补 `Scripting.Reset(); Events.Reset(); SceneOps.Reset();`。
- *保留*：本机 .NET 10 域线程模型下已知必 pin（ADR-010），故此缺陷当前部分被掩盖；但它是「runtime 修好后卸载仍失败」的根因，应修。

**M15 — 无 `Unsubscribe`** ✅ `Events.cs:37-41`` → 已修（批C `91d5b31`，Subscribe 助手 + Detach 自动退订）
- `Subscribe` 只加不删，`Reset()` 仅换域时跑。`WaveBannerBehaviour` 构造期按实例订阅 → 实体反复生成/销毁时 `s_handlers` 无界增长、且捕获 `this` 根住已毁实例（跨热重载还会根住旧 ALC 实例直到下次 `LoadScript`）。修复：加 `Unsubscribe` 并在 `Detach`/`OnDestroy` 调，或改 `ConditionalWeakTable`。

**M16 — 场景名往返丢失** ✅ `SceneArchive.cpp:264`(vs :322-370)` → 已修（批D `45b4da8`）
- `Save` 写 `doc["name"]`，`Load` 只读 `schemaVersion`/`entities`，从不恢复 `name` → 存"Level3.scene"再读回，编辑器标题显示默认名。修复：`Load` 里 `if(doc.contains("name")&&...is_string()) scene.SetName(...)`。

**M17 — SDL drop 文件泄漏** ✅ `Window.cpp:67`` → 已修（批D `45b4da8`）
- `m->drops.emplace_back(ev.drop.data)` 拷贝了路径，但 SDL3 要求应用 `SDL_free(ev.drop.data)`，此处未释放 → 每次拖入泄漏一个字符串。修复：emplace 后 `SDL_free(ev.drop.data)`。

### C. Low（记录，择要处理）

**Renderer**
- `Particles.cpp:125`：`t = age/lifetime`，`lifetime≤0`（配置 `lifetimeMin==lifetimeMax==0`）→ NaN → 颜色错一帧。钳 `lifetime` 与 `t`。
- `Camera2D.h:24`：`SnapTo(c, 1.0f/zoom)`，`zoom==0`（直接设/反序列化未过 `ApplyPixelPerfect`）→ inf → NaN 中心。加 `zoom>0?1/zoom:1`。
- `BitmapFont.cpp`：无设备丢失重建路径；`AtlasRegistry::Reset()` 后 `glyphSprites_` 持失效 id；`Init` 恒 `return true` 与文档「槽位占用返回 false」不符，重复 Init 泄漏贴图。
- `Particles.cpp:100`：视口剔除固定 `Expanded(64)`，不记粒子自身尺寸 → 大粒子边界突现。

**Scripting(C++)**
- `ScriptHost.h:157`：`scriptsNeedTick_=true` 从不改写 → 「有实例才 tick」闸门恒真（死状态/误导注释）。
- `ScriptHost.cpp:31-33`：`NativeRead` 返回 `m.sizeOf` 而非实写字节数 → `cap<sizeOf` 时多报。
- `ScriptHost.cpp:203-208`：`LoadUserAssembly` 未像 `HotReloadAssembly`(:217) 那样绝对化路径 → 相对路径可能在托管侧抛未捕获 `ArgumentException`→abort。
- `ScriptHost.cpp:392-445`：两个 drain 循环仅靠「短读」退出，native 持续回满缓冲 → 潜在死循环；`Push` 失败后仍继续拉。
- `ScriptHost.cpp:362-366,340-345`：`gcAllocFn_`/`behavioursListFn_` 惰性解析无同步 → 多线程竞争数据竞争。
- `CoreCLRHost.cpp`：hostfxr `ctx`/`Fxr` 在失败路径泄漏；错误写入器注册线程与 `hostfxr.h` 的线程局部语义不符。
- `CoreCLRHost.cpp:144-146`：窄 `char*` 传 `const char_t*`（Win32 为 `wchar_t*`）→ 移植 Windows 编译失败/乱码（当前仅 macOS 构建，故为移植项）。

**C# SDK**
- `Exports.cs` 多个 `[UnmanagedCallersOnly]` 导出无越界/空指针保护：`lemon_events_received`(:146)、`lemon_batch_query`(:124)、`lemon_blit_copy`(:265)。native 传越界 index/空指针 → 托管异常逃逸 UCO → coreclr abort。多为测试/诊断路径。
- `Exports.cs` 若干导出无 try/catch：`lemon_dm_tick`(:110，**未接宿主**)、3 个 diag 探针(:210,:218,:227)。违背本文件 :52-54 自述纪律；因非活动路径，实际暴露有限。
- `NativeApi.cs` `(byte)char` 字符串截断；`SceneOps.cs` `CompId=(byte)typeId` 静默截断 >255。
- `DomainManager.cs:294-305` `LoadMinimal` 覆盖 `s_alc` 前不卸载旧域。
- `Components.cs:225-228,247-251` `Slot(i)` 返回 `fixed` 指针派生 ref，仅栈内安全（当前调用点安全，潜在）。

**ECS + Core**
- `Scene.cpp:23-35`：`Destroy` 对同实体重复入队（tag 去重了标记，未去重队列）→ 当前被 `CommitDestroys` 的 valid 复核兜底，但虚增 `PendingDestroyCount`。
- `Hierarchy.cpp:61`：`SubtreeHeight` 周期守卫回退值 `kMaxHierarchyDepth+2` 永远达不到（守卫在 100001 才触发），「回大值触发拒绝」契约对 8..100000 深度是死的（`ComputeWorldTransform` 另有 `n>kMaxHierarchyDepth` 兜底，故非活 bug）。
- `ComponentRegistry.h:141-146`：`Register` 对重名断言 → 第二个 `World` 重跑 `RegisterAllComponents` 会 abort（靠目录侧幂等守卫，需目录 owner 确认）。
- `World.cpp:44-45`：`SystemRng` 用 `&(size()-1)`（表 256）但以原始 `systemId` 播种 → >256 系统时 id 混叠。
- `Math.h:33,102,163`：`Vec2::operator/`、`Ortho`、`SnapTo` 除零无保护（调用方契约，文档化或加护）。
- `JobSystem.cpp:129-133`：`(b==blockCount-1)` 三元不可达（循环 `b<blockCount-1`），死代码（值仍正确）。

**Systems / Serialization**
- `Window.cpp:56-65`：仅凭 KEY_DOWN/UP 跟踪按键，无 focus-lost 复位 → 切窗后按键卡死。
- `Window.cpp:22-34`：`SDL_CreateWindow` 失败路径未 `SDL_Quit()`（且析构因 `window==null` 也不调）→ SDL 引用计数泄漏。
- `ComponentCatalog.cpp:296-300`：`ForEachComponentRange` 钳 `end` 不钳 `begin` → `begin>view.size()` 时迭代器越界（UB）。
- `Systems.cpp:761-767`：M2 路径非 loop 时 `an.time` 无界（该路径不消费 time，良性；与 :761 注释「保证 time 有界」不符）。
- `Systems.cpp:301,353`：`Flee`/`Patrol` 对可能为零的向量 `Normalize`（已排除自身，但未排除他物同位）→ 若 `Math::Normalize` 不护零则 NaN 污染 `Transform2D.pos` 与回放哈希。加 `LengthSq>1e-8` 闸。
- `SceneArchive.cpp:234-238`：`sb.className` memcpy 后未显式补 `'\0'`（当前安全因调用点皆新实体）；`:50,59,237` 缺 `<cstring>/<cstdlib>`（现靠 nlohmann 传递包含）。

---

## 四、已核验 clean 的区域（✅ 逐行或重点读码）

- **Renderer**：`SpriteTypes.h` 布局 static_assert 正确；`Atlas.cpp/.h` UV 数学/打包/空洞处理正确；`SpriteBatcher` 环形缓冲增长路径**正确无溢出**（`EnsureCapacity` 恒先于写、`written<total≤capacity`）、批刷新与 CPU→GPU 上传正确；`Camera2D::ClampToBounds` 正确处理「视口大于世界」；`Quality` 的 EMA/降级状态机（除 M4 构造缺陷）正确。
- **ECS + Core**：`Pool.h`/`RingQueue.h`（下标数学、环形、增长与上限钳）正确；`Random.h` PCG32 + 拒绝采样正确且确定；`FunctionRef.h`/`Guid.h`/`Log` 正确；`Mat3x2` 乘/Apply 手算正确；`Events.h`/`Input.h`/`TeamTable.h` 边界安全。**变换层级无缓存陈旧路径**——`ComputeWorldTransform` 每次从父链重算且 TRS 数学正确，`ComputeStateHash` 每次读活数据。
- **C# SDK**：`Pcg32.cs` 与参考实现逐位一致；`Vec2/Components` 全部镜像 struct 的 `sizeof`/偏移手算复核通过（`Transform2D` 20B、`WaveDirector` 1260B 等）；`LayoutTables.cs` 用指针算术推偏移不会漂移；`Time/StateBag/GameObject/Input/Ui/Assets` clean；`Batch.cs` 异常隔离与 60 帧禁用逻辑正确。**委托生命周期处理正确**——唯一跨边界托管委托 `s_tickFn` 存静态、卸载前置空；`NativeApi` 是原生函数指针按值拷贝，无 pin。
- **C++ 脚本宿主**：**无 ALC pin 缺陷**——跨边界仅持默认 ALC 的 `loadAssembly_`，无 `GCHandle`/托管委托/`AssemblyLoadContext` 引用；`coreclr_delegates.h`/`hostfxr.h` 签名与调用次序正确；`hostfxr_close_handle` 可选符号处理正确；`ScriptBox.h` clean；`BatchBlock`/`BatchSystemFrame`/`SceneOpC` 布局 static_assert 正确。
- **Systems**：`SpatialHash.cpp/.h` clean（cell-key 打包、`CellRange` 二分、`OverlapCircle/Box` 闭区间边界、`PassFilter` 的 valid+计数兜底均正确，无漏候选/off-by-one）；`Systems.h` clean。近期特性热点均正确：`Health.iFrames` 每 tick 递减（`StatSystem`:728，`HitboxSystem`:573 设置）**无永久无敌 bug**；投射物命中记忆为有界 4 槽环；XP 入账有界（几何增长不卡）；`WaveDirector` 运行时每波重置、`waveIndex` 有钳制守卫。

---

## 五、进行中 / 待补

- **Renderer-RHI（`RHI.cpp`，1574 行）**：Vulkan/VMA 资源生命周期、描述符/每帧缓冲、同步（信号量/fence/swapchain）、句柄类型安全的 review **进行中**（已 resume，尚未回传）。这是最可能藏资源泄漏/同步竞态的模块，结论到手后补入 §二/§三。

---

## 六、说明与保留意见（子代理误报纠正）

review 中我对子代理结论做了实证复核，以下为其**初始判断被证伪/需下调**之处，记录以免误导：

1. **JobSystem `ParallelFor`「worker 不被唤醒」——不成立**。代码确实把 `blockCount-1` 块 `Schedule` 进工作池（`JobSystem.cpp:129-133`），仅最后一块由调用线程内联执行（:134-135）；worker 经 `TrySteal`（:100-113）取活。并行模型正确，该 finding 已丢弃。
2. **C#「behaviour 构造器抛异常→abort」（lemon_scripts_attach）——机制不成立**。该导出走 `PostBatch`（`DomainManager.cs:215-222`）**刻意吞掉** `cmd.Error`，不会 abort；残留问题仅是「附加失败且无日志」。已从 Medium 下调。
3. **C#「lemon_dm_tick 无 try/catch→用户异常 abort」——实际暴露有限**。宿主只绑 `lemon_scripts_tick`（`ScriptHost.cpp:191`，走异常隔离的 `PostBatchTick`/`Batch.Tick`），`lemon_dm_tick` 未接宿主；诊断探针亦仅测试用。仍建议按其自述纪律补 try/catch，但非 High。
4. **M14 当前影响被部分掩盖**：本机 .NET 10 域线程模型下 ALC 已知必 pin（ADR-010 修订），故「卸载永判 0」在当前 runtime 是既有现象；M14 的真正价值是「runtime 修复后卸载仍失败」的根因，属应修但不改变当前可观察行为。

---

*方法学声明：本报告所有 High/Medium 条目均经主控重新读取真实源码确认（标注 ✅）；Low 条目多为子代理实证、择要复核。RHI-core 结论待补。修复建议供参考，未改动任何代码。*
