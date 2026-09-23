# ADR-010：M3 范围与四项基础决策（线程模型 / 热重载延期 / 随机与浮点纪律 / SDK 分期）

- 日期：2026-09-19
- 状态：已采纳（M3 开工前定稿，用户确认）
- 影响：`04-CSharp-Scripting.md`、`08-Development-Roadmap.md`（M3 出口判据修订）

## 背景

M2 完成后盘点确认桥接表面就位（ComponentMeta 函数指针 + FieldMeta 偏移、27 组件 POD、
EventPacket 48B、管线 #14 占位槽保序、EventSink 挂点、StateHash 回放武器、两阶段销毁、
系统 RNG 子流）。M0 spike-03 已烧掉 hostfxr/托管指针/ALC 三大技术风险，但留有一个
架构级未知（教训 8：跨 UCO 调用 pin 可回收 ALC）与四个开工前必须定稿的设计点。

## 决策

### D1 线程模型：域线程统一执行

DomainManager 常驻托管线程统管「ALC 生命周期 + 每帧执行」。C++ 管线线程（#14/#15）
构造好数据后发命令、等栅栏。每帧一次线程往返 ~10–50µs，低于 04 §5 全部预算线。

- 依据：M0 教训 8 的已知解法即此形态；主线程直调（spike 形态）实测 pin ALC，
  热重载永远不可用且 M4 要返工线程模型。
- 附带收益：单线程执行天然确定性（回放武器前提）、托管调试单线程。
- 放弃项：C# 侧自发并行（引擎 ParallelFor 只跑 C++ lambda；档② 性能来自块内
  托管循环摊薄，不来自脚本并行）。

### D2 热重载推迟 M4；M3 保留卸载自检

完整热重载（文件监视 + StateBag 状态迁移 + ≤2s 判据）移入 M4 编辑器——热重载的真实
消费者是编辑器 Play 循环，M3 headless 无从验收。M3 仍实现 DomainManager 的
Load/Unload 命令，并在测试中执行「装载 → 运行多帧 → 卸载 → WeakReference 确认回收」
自检，当场烧掉 M0 遗留 pin 风险。

- **08 M3 出口判据随之修订**（原「热重载 ≤ 2s」移出）：
  环绕弹幕 5k 弹运行（C# 批量系统净时间 ≤ C++ 等效 1.5× 且整步 ≤8ms）；
  确定性回放双档 PASS；异常脚本不崩引擎；示例脚本托管分配 0；断点可下；卸载自检 PASS。

### D3 随机与浮点：RNG 位对齐 + 分层浮点纪律

- **随机（硬约束）**：PCG32 在 C# 位级移植，golden 值与 C++ `TestRng` 同源锁定；
  `System.Random` 全域禁用（测试 grep 防线）；脚本只从引擎子流取数
  （CSharpBatch 固定 #14 子流，注册序冻结约束不变）。
- **浮点（分层）**：
  1. 算术 / `MathF.Sqrt` / floor / ceil / 比较——IEEE 754 硬件位同，两侧放行；
  2. 超越函数（sin/cos/exp/pow 等）——放行 .NET 自洽实现，**不承诺与 libm 逐位一致**
     （N7「同机逐帧」口径扩展到 C#）；
  3. 金档回放绑定随引擎分发的固定 .NET 运行时版本——runtimeconfig 开发期
     `rollForward: latestMinor`，随引擎分发时收紧为禁用 rollForward。

### D4 SDK 门面分期：M3 核心子集（~50 导出）

M3 交付 headless 可测核心集：Chunk / 组件 CRUD（注册表驱动）/ 事件 drain+push / RNG /
Time / Log / Scene 创建销毁查找 / LemonBehaviour 生命周期 / 异常隔离。
Unity 对齐面的 Input / Audio / Assets / Instantiate(prefab) / LemonAwait / Profiler 随
M4/M5 消费者（编辑器 / 窗口 / 资产管线）落地。04 §3 的「~150 导出」口径分两期。

## 后果

- 正面：M3 带已知解法进入（无线程模型返工项）；出口判据全部可 headless 验收；
  热重载的 M0 风险在 M3 内闭环。
- 负面：M4 需补完整热重载（状态迁移协议 + 文件监视 + dotnet watch 集成）；
  C# 侧无自发并行（接受，热路径本就降档③）。
- 同批地基修复（盘点发现，非本 ADR 决策但同日落账）：
  27 组件补编译期 `static_assert` 布局冻结（原仅运行期测试断言）；
  `RegisterAllComponents()` 改由 World 构造自动调用（**ISSUE-9**：bench-sim 漏调注册，
  StateHash 遍历空注册表逐帧恒等，M2 回放验收恒真空转——修复后重录真基线）。

## 修订（2026-09-19，M3-2b 实测后）

### D1/D2 修订：域线程执行维持；ALC 卸载能力降级为"已知 runtime 限制"

M3-2b 卸载自检跑出**否定性结论**（比 M0 文档乐观的"DomainManager 已验证可行"更完整）。
本机 .NET 10.0.12（macOS x64）Collectible ALC 卸载实测矩阵：

| 场景 | 装配（创建 ALC+Load+反射） | 执行 | 卸载线程 | 结果 |
|---|---|---|---|---|
| UCO 探针（零反射） | UCO 主线程 | 无 | UCO 主线程 | **OK**（能力存在） |
| 域线程裸周期 | 域线程（零反射） | 无 | 域线程 | TIMEOUT |
| 域线程装配 + UCO 卸载 | 域线程 | 无 | UCO 主线程 | TIMEOUT |
| spike clean（对照） | 一次性线程（已退出） | 无 | UCO 主线程 | OK |
| spike 正式（对照） | 一次性线程 | UCO 主线程 | UCO 主线程 | TIMEOUT |

结论：**活着的线程只要触碰过 ALC 类型系统（装配/反射/执行）即永久 pin，线程死亡前
无法回收**（compacting GC 亦无效）。推论：任何"常驻执行线程"模型下可回收 ALC 必然
无法卸载——**M0 教训 8 的"DomainManager 方案"只对"一次性装配线程"形态成立**。

落地处置：
- **M3 架构不变**：域线程统一执行（D1 的性能/确定性/单线程调试目标全部成立）；
  装配与执行都在域线程，卸载在 UCO 调用线程发起（命令清引用 + Unload + GC 轮询）。
- **卸载从 M3 验收项移除**，`lemon-script-tests` 固化现状断言（unload=pin）+
  四条诊断探针（runtime 升级后重跑即知是否修复）。
- **M4 热重载改走"整域重建"路线**：换装 = 新建 ALC 加载新程序集，旧域泄漏
  （每次重载 ~百 KB 级，编辑器会话可接受；红色告警登记泄漏计数）；或 runtime
  修复后恢复 ALC 换装路线。M4 动工前以诊断探针复测为准。

## 修订（2026-09-19，M3-7 验收实测后）

### D5 性能判据分档：≤1.5× 定在 100k 规模；5k 档以整步预算为准

原口径"5k 弹 C# 批量净时间 ≤ C++ 等效 1.5×"在小规模被**每帧固定往返开销支配**，
测的是 macOS 调度器不是桥效率。bench-script 实测（Release，6C，--cpp-compare；
csNet 已按"预热后差分"剔除 Tier0 慢帧污染）：

| 规模 | C++ 等效 | C# 净时 | 比值 |
|---|---|---|---|
| 5k | 0.131ms | 0.252ms | 1.92× |
| 10k | 0.264ms | 0.442ms | 1.67× |
| 30k | 0.792ms | 1.206ms | 1.52× |
| 100k | 2.674ms | 3.823ms | **1.43×** |

两点拟合（5k↔100k）：固定往返 ~66µs/帧（域线程 post/栅栏；D1 预算口径 10–50µs 的
实测落地值，macOS 唤醒延迟为主）+ 边际每实体 37.6ns vs C++ 26.7ns（**边际比 1.41×**，
与 M0 spike 参照 1.45–1.62× 同量级）。

**修订后判据**（取代单点 1.5× 口径）：
1. 100k 弹净时间比 ≤1.5×（实测 1.43×）；
2. 5k 弹整步 avg ≤8ms（实测 0.25ms，余量 32×）；
3. 边际每实体开销比 ≤1.5×（实测 1.41×，两规模差分拟合）。

### D6 GC 零分配判据的度量前提：bench 进程关 tiered compilation + 导出指针全缓存

M3-7 逐层剥离出两个与脚本无关的分配来源（均 ~8.2KB 级不定时注入）：
1. **`GetExport` 每次调用都在托管侧分配**——`ScriptHost::GcAllocated` 曾每采样一次就
   查一次导出指针（违反自家"启动期一次取全"纪律）。已修：指针缓存成员，惰性解析一次。
2. **tiered JIT 分层记账**——缓存修复后默认分层下仍 9/10 进程出现非零增量；
   bench 在 `main` 起点先于 CoreCLR 初始化 `setenv("DOTNET_TieredCompilation","0")`。

TC=0 = Tier1 全优化从头编译，120 帧预热后与分层稳态码质等价，性能判据不受影响。
最终形态（TC=0 + 缓存）实测 15/15 进程硬 0（16 帧采样窗逐窗可诊断，无任何豁免口径）。
**编辑器/游戏进程不设此环境变量**——分层编译照常，该记账属运行时正常行为。

## 修订（2026-09-20，M4.5 动工前探针复测 + A 线交付）

### D2 修订收口：A 线整域重建定案交付；B 线挂起待 runtime

按本 ADR "M4 动工前以诊断探针复测为准"的既定程序，M4.5 动工前重跑
`lemon-script-tests` 全部四条探针（runtime 10.0.12，macOS x64）：

| 探针 | 结果 | 与 M3-2b 对照 |
|---|---|---|
| UCO 线程一次性 load→unload | **OK** | 一致（卸载能力存在） |
| 域线程单命令 min-cycle | TIMEOUT | 一致 |
| 裸 LoadFromAssemblyPath 跨命令 unload | TIMEOUT | 一致 |
| 域线程 load→tick→unload | TIMEOUT | 一致 |
| **pin 后再 load（换装核心前提）** | **OK** | 一致（新域可用、状态归零） |

结论：runtime 行为与 M3-2b 矩阵完全一致，**A 线（整域重建）定案**。已按
M4.md §3.7 交付：

- `DomainManager.ReloadScript`（M4.5）：域线程捕获 StateBag + 丢引用 → UCO 线程
  短轮询（3×GC+5ms，不赌 300ms 全轮询占换装预算）尽力卸载 → 未回收计泄漏（红字）→
  新 ALC 装载。**实测编译+换装+重装配 1.22–1.24s ≤ 2s 判据**（Play/Edit 双态各一例）。
- **B 线（ALC 换装）无需代码变更即可启用**：`ReloadScript` 的泄漏计数在 runtime
  修复后自然归零（`LastCollected` 转 true 路径已接好）；届时重跑探针确认后
  在本 ADR 记录 B 线启用即可。
- StateBag 白名单（04 §6 回填）：基元值类型/枚举/`Lemon.Vec2`（SDK 常驻 ALC 身份）；
  装箱值若携带旧域类型即 pin——用户自定义 struct 一律不可迁移。类型不匹配/缺失 =
  丢弃（TryGet false），不抛异常不阻断换装。
