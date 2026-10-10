# M7c 批⑪ b11d — 引擎评审低危卫生清账 + 设计债登记（review 2026-10-09 L 类）

2026-10-10 · [批⑪ 批文件](../Plans/M7c/2026-10-09-b11-engine-review-fixes.md) · 来源 [评审报告](../Reports/2026-10-09-engine-code-review.md)（39 低危，L35 复核否决关闭、L13 已随 b11c M11 修复）

## 事件

b11d 当日清：同构优先 4 项 + 逐项小修 21 项（批文件枚举 20 项中 L13 随 b11c 落地，另顺手清同类 L12/L16→登记/L24）+ 设计债登记。修复面全部 ≤30 行小修，行为面变化三处（L1 排序语义修正 / L23 重烤拒播 / L31 CLI 响亮拒绝）均有测试或走查覆盖。

### 同构对齐优先清（4 项）

- **#L2 RHI 映射写路径统一 flush**：UploadTextures staging memcpy 后 `vmaFlushAllocation`（RmlUiBackend:379 dGPU 黑屏先例同因）+ EndFrameAndPresent 提交前对全部 hostMapped 常驻映射统一 flush（SpriteBatcher 每帧写面；coherent 上 VMA 内部直通零成本）+ RHI.h:66 注释修正（原自称 HOST_VISIBLE|COHERENT 与实现不符——AUTO_PREFER_HOST + SEQUENTIAL_WRITE 下 COHERENT 非 required 非 preferred）。
- **#L11 SpatialHash NaN 防御**：Rebuild 收集循环 isfinite 剪除（#19 TargetBoard::Grid 同款口径——坏坐标实体静默跳过收录 = 不命中，只丢功能不炸）；OverlapCircle/OverlapBox/Raycast 三查询入口非有限参数空结果返回（PointQuery 经 OverlapCircle 自动覆盖）。
- **#L27 Rng 注释修正**：Next() 是 constexpr（CI win 热修④的常量求值要求），恒生效断言会破坏常量求值——按评审二选一取「修正注释」：移除不存在的断言承诺，说明未种子实况（固定流 state=0/inc=1，run-to-run 确定但与世界 seed 无关且忘播种实例共享同流）。
- **#L15 随机纪律防线做实**：Fx.Crit 的 `Random.Shared.NextSingle()` → 呈现层专用 Pcg32 子流（静态实例，不入 StateHash 但流位级确定）；svr-test PlayerBehaviour/DuelBehaviour 两处 `new System.Random()` → Pcg32（NextDouble/Next(int) 调用点适配 Float01/Range，demo 构建 0 错）；**ADR-010 D3 所称 grep 防线测试本体化**——CoreTests 新增 TestScriptingRandomDiscipline（LEMON_SOURCE_DIR 宏递归扫 SDK/Entry 全 .cs，禁 `new System.Random`/`Random.Shared`/`.NextDouble(`/`new Random(` 四使用形态；Pcg32.cs 纪律注释不含使用形态不误伤）。「防线缺失」正是 Fx.Crit 违规未被拦下的根因（评审复核发现防线测试从未存在）。

### 逐项小修（21 项）

- **L1 SortingLayer 负 order**：打包前 `(uint32_t)(order + 32768)` 偏移（-32768→0、32767→65535）——原 (uint16_t) 直转把 -1 变 65535 排到 0 之上，与 Unity「负 order 在下层」心智相反；仓库示例只用正值 = 出厂内容逐位不变。
- **L6 forEachRangeFn begin 钳制**：`begin >= view.size()` 早退（与 end 钳制对齐；休眠缺陷——并行切段钩子零调用点，防未启用时埋雷）。
- **L7 Scene::Destroy 契约改口**：头注释「线程安全/worker 直调」改为「仅主线程；并行销毁走收集意图→主线程归并（03 §4 契约 3）」——原点名方 ProjectileLifetime 已改主线程收集提交，锁内 registry 结构写与并行迭代仍是数据竞争。
- **L9 直调豁免注记**：SceneArchive.cpp / SceneSwitcher.cpp 两处坏条目直调 CommitDestroys 补「坏槽 = 零组件空实体，无 ScriptBox/OnDestroy 可漏」豁免理由（staging 侧原有注记保持）——防日后在可入队销毁的回调上下文模仿直调。
- **L12 Director/Spawn 请求表成员化**：两处每 tick 局部 vector + reserve(16)（空转期照样 malloc/free）→ 系统成员 clear() 复用（chunkIntents_ 同款「稳态零分配」纪律）。
- **L14 BindHostfxr 失败分支**：缺必需导出时 FreeLibrary/dlclose（候选链最多 8 次泄漏；进程退出回收但登记清账）。
- **L17 告警去重表随局/域清**：Anim.s_missed/s_paramMissed（键含实体 id 无界累积）+ Table.s_warned 新增 ResetWarnTables()，接入 lemon_play_reset 与 DomainManager 三处域 Reset 链（原清 Scripting/Events/UI/Behaviours/SceneOps/Time 六表，Lemon.SDK 钉在 Entry ALC 跨热重载持久存活）。
- **L18 FontBake 横向界检**：`cw > kPageW - 2*spacing` → ++dropped 截断（与纵向同款）——原换行后仍在 penX 放置，页合成 memcpy 每行越 512px 行界写（复核定性：页内相邻行互毁的视觉破坏，无堆越界）。
- **L19 ResolveScene 拒 ".." 越根**：词法归一后逐段检查 `..`（头注「越根 = 拒绝」原只兑现绝对路径半边；C# LoadScene 可越项目根读任意文件）。
- **L21 LoadBakedAtlasFile 拒载清 out**：Reject lambda 统一 `out = BakedAtlasBuild{}`——「失败 out 保证为空」头契约原靠唯一消费方弃局部变量巧合成立。
- **L22 ResetClips 锁外析构**：PCM 大块 free/munmap swap 出锁外（MountAll 每次进 Play 首步即调，设备回调持同锁混音被边界性卡顿；M18 退役 stream 同纪律）。
- **L23 流式起播重烤比对**：Play 锁外重读 .baked 头与注册期快照比对 frameCount/channels，不一致拒播红字「已重烤，需重进 Play 重注册」——原变短逐帧计欠载/变长提前截断/变声道帧字节错位三态静默劣化。
- **L24 ParseSceneDoc 延迟拷贝**：确认 `ver < kSchemaVersion` 才拷可变副本（v2 现版档占绝大多数，原无条件 MB 级深拷贝 × 5 调用点）。
- **L25 EntityCount 释放后回落**：`doc.is_null()` 回落 ledger.size()（槽账口径；原 at() 对 null json 抛 type_error 穿透 pimpl 边界）。
- **L28 SDL_CreateWindow 失败分支**：补 SDL_Quit（析构守卫走不进）。
- **L29 FastSin/FastSinCos 出域归一**：NormalizeRad（稳态两次比较快径；出域才 fmod 到 [0,τ)）——|rad| ≳ 3.29e6 的 (int) 截断 UB（Transform2D.Rot 脚本可写且全链无归一，增量品类挂机长跑可达阈）。
- **L30 WriteFileAtomic tmp 掺 pid**：`.tmp.<pid>`——固定后缀下双开编辑器/编辑器与运行时共用 projectRoot 交叉写同一 tmp 再各自 rename 可能把半档提升为正式档（破坏「中断不产生半档」契约）。
- **L31 CLI 响亮拒绝**：未知/残缺参数 ERROR + 用法行 + 非零退出；--frames strtol+endptr 全串消费校验——原 `--frames abc` 经 atoi→0 使 paced 反转为交互模式无限挂住。回归/CI 全部旗标（--project/--scene/--frames/--smoke/--smoke-scene/--validate）逐一核对放行。
- **L36 ParallelFor 异常重抛**：Complete 改 wait+get()——packaged_task 存入 future 的异常原无人观测（grep 全引擎零 .get()），并行块 bad_alloc = 部分完成的静默结果且 --threads 1 可见/多线程静默不一致；重抛对齐全引擎响亮失败口径。
- **L37 game-assets 回调 token 持有**：RecreateGuard（栈序晚于被捕获对象 = 析构先反注册）——按引用捕获 main() 栈对象的回调不摘除 = 设备丢失重建 UAF（RHI.h 自述契约；benign 埋雷因 recreate 仅设备丢失路径触发）。

### 设计债登记（D4 口径：登记不修码）→ [08 路线图 M8 段](../EngineDesign/08-Development-Roadmap.md)

- M23/L34/L39 三条已在 08 架构债登记块（批⑪ 开工时落）；本批补登 b11d 评估后不修的 10 项（L3/L5/L8/L10/L16/L20/L26/L32/L33/L38，各附触发条件）+ M4 Windows 验证项。

## 验证（机器面）

- 构建零警告（mac）；demo/svr-test Game.csproj 0 错误（L15 调用点适配）。
- 单测 **34,754** OK（b11c 后 34,752 + 2：TestScriptingRandomDiscipline 两断言——扫描非空 + 零违规；违规路径逐处响亮 Expect 定位）。
- script-tests **1,830** OK（L17 表清理接入 PlayReset/域 Reset 链零回归）。
- ctest 4/4；回归 **21/21** 全绿（含 game/scene/template/audio/uirml 全 smoke）；bench-survivor **fps=84**（≥76.5 门）。
- L31 行为验证：回归全旗标放行（21 步含 game-smoke/pkg-smoke 的 --frames 900 等数字实参路径无一遍伤）。

## 遗留登记

- 登记项明细见 08 路线图 M8 段架构债登记块（2026-10-10 扩充）。
- L16（飘字格式化分配）登记：真零分配需 native 表加 char*/float 直写变体（SDK string marshal 链改造，M8 候选）——评审自认「svr-test 同函数还有别的分配，单修此处不足以归零」。

## 追记：b11d review 轮（同日）

独立只读评审对 25 项逐项复核：**全部通过、无阻塞项**；三处行为面变化（L1 排序/L23 拒播/L31 CLI）无害性论证独立核实成立（L1"仓库内容只用非负 order"属实——场景档无 order 字段、代码面 0/1/2；L22 锁外析构在"DataCallback 全程持 mtx"同步模型下无窗口；L36 重抛与断言口径同构且重抛点全在主线程 C++ 栈；M4 回退零残留死代码）。已修 low 五条：

- **R-b1（low，已修）**：PlayerBehaviour kPlayerOrder 陈旧注释（"负值排最顶"陈述在 L1 后为假 → 改"负 order 在下层，Unity 语义"）。
- **R-b2（low，已修）**：NormalizeRad 对 NaN/±inf 恒等穿透（inf−inf=NaN 仍进 (int) 截断 UB）——快径失败分支补 `isfinite` 哨兵返回 0（sin(0)=0 确定值，不增稳态成本）。
- **R-b3（low，已修）**：L25 注释虚指的"SceneArchive.h 契约补注"做实（EntityCount 注释补"ReleaseDocChunk 后仍可查：回落 ledger"）。
- **R-b5（low，已修）**：grep 防线补 `"System.Random "` 目标类型 new 形态（`System.Random r = new();`）；Pcg32.cs 纪律注释用全角括号不误伤，测试实证零违规。
- **R-b7（low，已修）**：CoreTests 重复 include 清理；`--frames` 报错文案改"需为非负整数（0 = 交互模式）"（原文案"非正整数"与 0 合法放行矛盾）+ strtol 溢出上限钳 1e8。

登记两条（08 路线图 M8 段随批⑪ 注记）：**R-b4** L11 有限大值相加溢出为 inf 的理论缺口（radius+probeRadius 双有限大值；现实查询参数不可达）；**R-b6** L30 同进程并发写同一路径仍共用 tmp（pid 相同；现存调用面无此形态）+ 孤儿 `.tmp.<pid>` 不再被固定名覆写、按 pid 积累（清理归调用方）。

复验：构建零警告 / 单测 34,754 / script-tests 1,834 / ctest 4/4。
