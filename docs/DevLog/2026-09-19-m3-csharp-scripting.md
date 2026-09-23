# 2026-09-19 · M3 C# 脚本层收官（bench-script 验收 + 三个深坑）

M3-0~M3-6 已全绿（布局护栏 27 组件 / blit roundtrip / PCG32 位对齐 / 域线程 / 批量 /
事件桥 / LemonBehaviour / 结构命令缓冲，1264 checks）。本日收官 M3-7 验收，过程中
连环踩出三个值得留档的坑——**全部是"10k 规模全绿、更大规模必崩"或"假绿"形态**。

## 坑 1（P0）：deque 不连续 × C# 线性步进 = 野指针

症状：bench-script ≥15k 弹 SIGSEGV（AV in `BoomerangSystem.ForEach`），10k 全绿；
单线程档同崩（排除并发竞态）。之前为修 vector 扩容悬垂把块缓冲改成了 deque——只看了
"push 不搬移元素"，漏了 **deque 分块存储、跨 chunk 不连续**；而 C# 侧
`fr->Blocks + b`、`Comps + slot*stride` 全是线性指针步进。
铁证：libc++ deque 对 24B `BatchBlock` 每块 ~170 元素，10k 弹 = 157 块（chunk 内，
碰巧合法）、15k = 235 块（第 170 块跨 chunk）——阈值正好卡在 170×64≈10.9k。
修复：三缓冲改回 `std::vector` + **每帧构造前按 countFn 预留总量**（容量足够 ⇒
连续与不搬移同时成立）。调试期加的构造校验通道先误导了一轮（校验自身没按
compCount 分槽读，自己就是崩溃点）——校验代码也要按被校验的不变量写。

## 坑 2（P0）：reserve 公式漏了末块补齐 → 构造尾部 realloc → 全帧悬垂

坑 1 修复的第二天形态：script-tests AV（`AddVelocitySystem.ForEach`），bench 反而全绿。
`FlushBlock` 每块恒插 `compCount×64` 个指针（末块不足 64 也整块插入），最坏指针数是
`ceil(n/64)×comp×64`；我只按 `n×comp` 预留，短 `comp×64`。bench 侥幸全绿是因为
72 万字节 reserve 被 malloc 按页取整**碰巧**盖住缺口（720000→720896B 恰 ≥90048 槽）；
script-tests 小规模无取整余量 → 立崩。教训：**"大数侥幸通过"本身就是分配余量
错误的信号**；预留公式必须按插入协议的最坏形状推导，不是按元素计数。

## 坑 3（P1）：GC 零分配判据的两个污染源（都被 16 帧采样窗抓出）

- `ScriptHost::GcAllocated` 每次采样都 `GetExport` 查导出指针——**每次调用在托管侧
  分配 ~8.2KB**（违反自家"启动期一次取全"纪律）。修复：指针缓存成员。
- tiered JIT 分层记账：缓存修复后默认分层下仍 9/10 进程出现 ~8.2KB×N 次非零增量
  （30k×300 帧实测 3 次）。处置：bench `main` 起点先于 CoreCLR 初始化
  `setenv("DOTNET_TieredCompilation","0")`（Tier1 全优化从头编译，120 帧预热后与
  分层稳态码质等价）。编辑器/游戏进程不受影响。
- 另修 csNet 口径：#14 profile 均值曾含 120 帧预热的 Tier0 慢帧（csNet > 整步 avg 的
  不可能值暴露问题）→ 预热后差分。这正是此前 cpp-compare 比值噪声大（1.27–2.35×）的主因。

## 验收终测（Release / 6C / .NET 10.0.12，方法见 09 §6.9）

| 判据 | 结果 |
|---|---|
| 5k 弹整步 ≤8ms | avg **0.248ms**（18000 帧全程） |
| C# 净时比（ADR-010 D5 分档） | 5k/10k/30k/100k = 1.92/1.67/1.52/**1.43×**；边际比 1.41×；固定往返 ~66µs |
| 确定性回放 | 18000 帧 × 双档（--threads 1/4）mismatches=0 |
| 毒脚本 | 恰 60 条红字后自动禁用，引擎不崩 |
| 托管分配 | **0 B / 18000 帧**（15/15 进程硬 0） |
| 断点通路 | 诊断 IPC socket + `Lemon.Domain` 线程名 + mac-debug 全量测试通过 |
| 回归 | engine-tests 12830 + script-tests 1264 + M2 bench-sim 重录金档双档 PASS（threads=1/4 mismatches=0；多线程档 avg 3.21ms） |

判据修订入 [ADR-010](../ADR/ADR-010-M3-Scope-Thread-RNG.md) D5/D6；汇总入
09 §7.6。其余已留档的坑（NativeWrite 对已有组件二次 emplace 损坏 entt 池、
C# 静态字段文本序初始化假哑火、ScriptAlc 类型身份唯一性）见对应代码注释与
M3-2b 诊断记录。

---
