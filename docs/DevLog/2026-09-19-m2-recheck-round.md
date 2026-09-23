# 2026-09-19 · M2 复核轮（只读审计：Engine 零修改，21 组新测试 + 7 项问题登记）

**约定**：应用户要求本轮不修改任何 Engine 代码，只新增测试与文档。上轮 11 项修复的
验证延续（18000 帧双档回放双 PASS 后进行）。

**产出**：
- 单测 11545 → **12821 checks**（新增复核节 21 组 / +1276 checks），Release +
  ASan/UBSan + TSan 三套全绿零报告。
- 新文档 `../Reports/2026-09-19-m2-review-checklist.md`：代码结构地图、分层纪律 grep 实证、
  模块不变量清单（Core/ECS/序列化/空间/系统/回放共 60 项）、问题登记、覆盖矩阵、缺口。
- **新登记 7 项问题（全部未修，证据测试固化现状）**，其中 P1 两项：
  - **ISSUE-5（P1）Flee 特性完全失效**：AISystem 调 NearestAny 未排除自身 →
    自己 d²=0 恒为最近威胁 → 逃逸速度恒零。bench-sim 无 Flee 实体从未暴露。
  - **ISSUE-2（P1）恶意档越界**：数组段 count 无数组键时不受截断 →
    StatSystem 越界读写（StateHash 有 clamp 安全——验证轮更正；ASan 已实锤）。
  - ISSUE-1（P1）schemaVersion 字符串抛异常抛穿 Load；
    ISSUE-3（P2）AddSystem 重名断言空转 + RunStage 未排序越界；
    ISSUE-4（P3）SpawnSystem 无工厂告警无条件触发；
    ISSUE-6（P3）Patrol 折返帧速度滞后一帧；
    ISSUE-7（P3/设计）触发器无层过滤，重叠互触发。
- 分层纪律 grep 实证全合规（entt 封装/Vulkan 零泄漏/nlohmann 收敛/随机源统一）。

| 验证 | 结果 |
|---|---|
| 单测三套 | 12821 OK ×3，ASan/UBSan/TSan 零报告 |
| Engine 改动 | 0 行 |

---
