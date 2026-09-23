# 2026-09-19 · M2 ECS 运行时完成（World/ECS 骨架 → 16 系统管线 → bench-sim 确定性回放）

**环境**：macOS 24.6 / Intel 6C（本机）；bench-sim 纯 CPU 无渲染（不开窗口/不初始化 Vulkan）。
**交付**：`Engine/Core`（JobSystem/Random/Pool/RingQueue/FunctionRef）→ `Engine/ECS`（Entity/Scene/World/
SystemPipeline/ComponentRegistry/TeamTable/StateHash/Events）→ `Engine/Components`（27 组件目录+登记表）→
`Engine/Physics2D`（SpatialHash 查询层）→ `Engine/Systems`（16 系统：12 真实现 + 4 里程碑占位）→
`Engine/Serialization`（.lscene v1 + 迁移链骨架）→ `Samples/bench-sim`。
**决策记录**（开工前与用户对齐）：完整 JobSystem（非单线程起步）；最小 .lscene 序列化（引 nlohmann/json v3.11.3）；
F3 = 统计层+文本（ImGui 版 M4）；Tracy 暂缓。

## M2 验收（08 §3）

| 判据 | 结果 | 实测 |
|---|---|---|
| bench-sim 1 万怪全系统 ≤8ms/步 | ✅ | **avg 5.10ms**（多线程 5 worker，引擎默认形态）@ alive 10053；单线程诊断档 avg 19.9ms（并行是 8ms 判据的必要条件——完整 JobSystem 决策的实证） |
| 确定性回放：同输入 5 分钟逐帧一致 | ✅ | 18000 帧 × **双档 PASS**（--threads 1 / 多线程 5 worker，逐帧状态哈希 mismatches=0；录制档 avg 9.7/2.6ms 含每帧全量哈希 ~3ms 开销） |
| F3 数据齐全 | ✅ | 每系统 μs（last/max/累计）+ 实体/事件/池统计，`--stats` 输出（ImGui 面板 M4 接同一数据源） |
| 单测 | ✅ | **11511 checks OK**（M1 167 → M2 11511：RNG/Job/池/环形队列/ECS 生命周期/序列化 roundtrip/哈希/管线端到端） |
| M1 回归 | ✅ | bench-mow 120.1fps（基线 107fps，无回退） |

## bench-sim 1 万怪分解（3600 帧，多线程 5 worker）

| 系统 | avg | 说明 |
|---|---|---|
| Separation | 1.57ms | 分离力（密度截断后，见优化 3） |
| AI | 0.26ms | 目标板最近邻（见优化 2） |
| SpatialHashRebuild | 0.18ms | 10k 实体重建（单线程 std::sort，预算 1.2ms 内） |
| Movement | 0.09ms | 积分+边界钳制（并行） |
| Hitbox/Spawn/Stat/回收等 | ~0.03ms | — |
| **合计系统时间** | **~2.1ms** | avg 5.10 含 ParallelFor 派发与调度开销 |

## 性能优化记录（1 万怪语境，保留过程）

1. **Spawner 配额失控**（首轮 alive 涨到 5820、分离力 6ms@1k）：SpawnSystem 无 maxAlive 语义，
   生成率 > 死亡率 → 怪无限增长 → 分离力 O(n²)。修复：per-team 存量普查（30 tick 周期）+ maxAlive 配额。
2. **AI retarget 尖峰 85ms**：Chase 用 aggroRange=全场 的 OverlapCircle 找最近目标 → 每 6 tick
   全员扫全部 cell（O(n×cells)，玩家队只有 1 个实体——大半径哈希查询是错误算法）。重构为
   **TargetBoard**：按目标 team 预收集位置（一遍 O(n)），逐怪线性最近邻 O(teamSize)。
   尖峰 85.6→6.7ms、avg 0.26ms。稀疏目标走板、密集查询（命中/分离/磁吸）走哈希——各得其所。
3. **分离力密度截断**（03 §14"密度上限"的实测落地）：万怪堆叠玩家时 cell 内遍历退化 O(n²)
   （实测 6ms@1k）。每实体只处理前 10 个有效邻居，哈希回调序 = cell→id 升序 → 截断确定（回放安全）。
   1 万怪分离力 → 1.57ms。

## 事故与修复（对齐 M1 风格保留过程）

| # | 事故 | 根因 | 修复 |
|---|---|---|---|
| 1 | 序列化 roundtrip 丢 parent | EntityRef 写出与实体编号同遍历（EnTT 遍历序 ≠ 创建序，父实体未编号即被引用） | 两遍式：先全部编号再写字段；输出按句柄排序（roundtrip 不动点成立） |
| 2 | 全部敌对判定静默失效（弹穿过玩家不命中） | World 的 TeamTable 默认构造（全 Ghost）未装 Default 表 | World 构造装默认表；教训：**关系表零值必须选安全方向**（Ghost），且默认表要进构造 |
| 3 | SpawnSystem 首帧 SIGSEGV | 普查倒计时初值溢出跳过首次普查 → teamCounts 空 → 下标越界 | 倒计时 0=本帧普查，首帧必查 |
| 4 | Shooter 弹体势力硬编码 team3 | 玩家弹幕队写死，怪射玩家的弹敌我判定反转 | 弹体势力继承射手 Meta.team |
| 5 | entt 空组件 emplace/get 返回 void | 3.15 对 is_empty 组件特化（tag 无数据） | Scene 封装层特判（共享空实例引用），业务无感 |

## 移植与依赖登记

- Luma `Event/JobSystem`（MIT，B 级）：结构移植 + 01 文档点名的 Schedule 值语义修正
  （packaged_task 移动入队，消 IJob* 生命周期陷阱）+ ParallelFor + 单线程诊断档；源文件头保留版权注记。
- nlohmann/json v3.11.3（MIT，CPM 锁 tag）：.lscene 序列化；THIRD_PARTY.md + 07 文档已登记。
- EnTT v3.15.0 从 spike 转正为引擎内核依赖（PUBLIC 链接，封装层 Engine/ECS 内允许、业务侧禁直用）。

## 复现命令

```bash
cd GameEngine/Lemon && cmake --build --preset mac
./build/mac/tests/lemon-tests                                   # 11511 checks OK
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 3600 --stats   # 性能
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --record r.rpl
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --replay r.rpl  # PASS
```

---
