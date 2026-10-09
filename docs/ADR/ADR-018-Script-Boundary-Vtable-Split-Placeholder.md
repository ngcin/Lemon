# ADR-018: C++/C# 脚本边界 vtable 按域拆表（占位）

Status: 占位（未决策，未排期）——触发条件与登记见 [08 路线图 M8 段架构债登记](../EngineDesign/08-Development-Roadmap.md)；来源 [引擎评审 2026-10-09 #M23](../Reports/2026-10-09-engine-code-review.md)，D4 追认（用户 2026-10-09）。

## 背景

C++/C# 边界现以单张手工双侧逐字节镜像的 vtable 通信（56→59 槽，批⑦–⑧），已从「批量 API + 事件队列」两通道扩张为多通道 god-interface。当前管理纪律：表尾追加、LegacyFrozenBytes 冻结区、判空降级——未出功能问题，但每加一个 C# 能力两侧各长一份镜像面，同步成本与漂移风险随槽位线性增长。

## 待决策内容（触发后填写）

- 拆分形态：按域分表（render / assets / scene / ui / fx / …）+ 分组版本化； ABI 兼容策略（金回放与热重载是否受影响）；
- LegacyFrozenBytes 冻结区处置；迁移步序与双侧回归面。

## 触发条件

- 槽位数 ≥ 72，或
- 发生一次因双侧 vtable 不同步产生的回归。

触发即转正式 ADR 流程（推翻/修订既定边界设计，按仓库规矩须 ADR）。
