# 2026-09-19 · M2 全量复审（提交后审计轮：11 项修复 + sanitizer 三件套零报告）

**背景**：M2 七提交（78d921f..efb78eb）落库后做从头复审——全部 M2 源文件净室读码 +
全新构建目录重建 + 双档回放复跑 + ASan/UBSan/TSan。**修复前基线全绿**（问题均为
潜伏路径：未覆盖的组件组合 / 畸形输入 / 并发时序），修复后 11545 checks、
双档回放 PASS、bench-mow 无回退、sanitizer 零报告。

## 修复清单（真问题 11 项）

| # | 问题 | 根因 | 修复 |
|---|---|---|---|
| 1 | **Scene::Destroy 数据竞争**（UB） | ProjectileLifetimeSystem 在 ParallelFor worker 里并发调 Destroy，裸 vector push_back | destroyMutex_ 保护队列 + DestroyQueueTag 打标同锁；CommitDestroys 锁内 swap 出队 |
| 2 | **AliveCount 虚高** | entt 3.15 实体池删除策略 = swap_only：销毁槽位以 tombstone 留在 packed 数组，storage size() 含回收位 | 改 `createdTotal_ - destroyedTotal_` 精确计数（bench alive 10015→10002 修正） |
| 3 | **Each() 遍历到死亡槽位** | 同上：tombstone 以换代句柄混进遍历 → Save 会把已销毁实体写进存档 | Each 内 registry.valid 过滤 |
| 4 | **.lscene 数组段丢失** | StatusEffects.active / Inventory.items 只写 count 不写内容；Equipment.relicIds[3] 登记成单个 UInt32（只存首个） | ArraySegMeta 元数据（元素字段表）+ SceneArchive 读写 + StateHash 统一走段表（relicIds 哈希补全 12B） |
| 5 | **PassFilter 越界判断反转** | `team<32 && !mask` 写法使 team≥32/layer≥16 反而跳过过滤被放行 | `>= 上限 ∥ 不匹配 → 不命中`（与注释语义一致） |
| 6 | **AISystem Chase 块缺守卫** | Pool<Chase> 切分不含伴生组件约束，缺 Transform2D/Velocity 的实体 try_get 解引用空指针 | all_of 守卫（与 Separation 同型） |
| 7 | **Load 异常抛穿** | 字段类型错（"pos":"x"）/entities 非数组/components 非对象 → nlohmann 异常直接炸编辑器 | 字段级 try/catch 降级 + is_array/is_object 结构校验 |
| 8 | **同帧多源双死** | Hazard 不设 iFrames，已死目标被多 Hazard/弹重复结算 → 多个 Death 事件 | 两处伤害路径 `cur<=0` 早退 |
| 9 | **XpProgress 死循环风险** | xpToNext 资产配 0 时 ceil 收敛卡死升级环 | `max(1.0f, ceil(...))` |
| 10 | **6 字段误序列化** | Spawner.cooldown / Hazard.tickPhase / Projectile.age+hits / Health.iFrames / Trigger2D.inside 注释标"运行时"但漏 kFieldRuntime | 全部补 FIELD_RT；Trigger2D._pad 复用 hack 改显式 fired 字段（RT） |
| 11 | World::Step 无 active 场景空指针；SpawnSystem 无工厂告警放循环内吞掉后续 spawner 冷却推进 | 边界 | 空步 return；告警外提 |

**单测新增 7 组 34 checks**（11511→11545）：数组段 roundtrip 保真、RT 字段不入档、
恶意 JSON 容错、越界 team/layer 不命中、并发 Destroy（4000 实体 4 线程）、
DestroyQueueTag 生命周期、无场景空步、双死事件恰一。

## 验证矩阵（修复后）

| 验证 | 结果 |
|---|---|
| 单测（Release / ASan+UBSan） | **11545 checks OK** ×2，sanitizer 零报告 |
| bench-sim 1 万怪 1800 帧 | avg 7.37ms（后台负载下，判据 ≤8；首轮实录 5.10） |
| 确定性回放双档（修复后重录 18000 帧，哈希函数已改仍逐帧一致） | **双 PASS**（单线程 avg 9.35ms 含逐帧全量哈希 / 多线程 2.67ms；mismatches=0） |
| bench-mow M1 回归 | PASS（cpuRender 4.83ms） |
| ASan+UBSan bench-sim 600 帧×双档 | 零报告 + 回放 PASS |
| TSan（tests + bench-sim） | 零数据竞争（对 #1 修复的直接验证） |
| 构建警告 | Lemon 自有代码 4→0（SDL 第三方 1219 条不属治理范围） |

**遗留讨论项（未修，见 M2 收官汇报）**：Hazard 命中半径 48 硬编码（组件无 radius
字段，M5 资产化时补）；Spawner.cooldown 读档回落 0 立即触发一轮（配额语义可接受）；
spike/03-csharp/dotnet 构建产物误入版本库（建议 .gitignore + untrack，待用户定）；
JobSystem 嵌套 ParallelFor 禁用约束已文档化（无作业图需求前不实现）。

---
