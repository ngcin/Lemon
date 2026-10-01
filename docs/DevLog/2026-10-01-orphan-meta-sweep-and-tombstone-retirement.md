# 孤儿 .meta 清扫落地 + 墓碑机制退役（Unity/Cocos 式收口，2026-10-01 用户拍板）

- **日期**：2026-10-01（同日第三轮，接续 [低 32 位防线](./2026-10-01-m7a-prereq-fix-low32-collision.md)）
- **决策链**：孤儿 meta 红字永续问题 → 用户问 Unity 对照 → 核查三家引擎（Unity 刷新即自动清；Cocos 3.8.0 不清 → 3.8.1 修复自动清；Godot 不清靠社区插件手动 + `.godot/` 可抛弃）→ 用户拍板：跟 Unity/Cocos 走、墓碑不是好设计（误删有版本管理兜底）+ 加手动清扫入口。
- **墓碑退役的代码事实**：实现本就会话级（跨进程不重建——Rescan 墓碑块只遍历内存 `entries_`，manifestCarry_ 只服务在盘文件），头注释"重启不回收"言过其实；且其保护的两场景均不靠它——误删恢复由 .meta 随文件走 + git 兜底（meta 皆删时同恢复），spriteId 稳定由 guid 真源 + `ResolveSpriteRefs` 归一兜底（M6a 批⓪ 口径）。

## 落地（`Editor/Assets/AssetDatabase.{h,cpp}` + 菜单/报告窗）

1. **墓碑退役**：Rescan 消失文件 → 条目同轮出表（`lastChange_.removed` 照推 → GPU 幽灵页 Evict 链路不变）。编辑器内 `Remove()` 不就地 erase（调用方浏览器瓦片循环持有 `entries_` 引用 = 迭代器失效），改 missing 位同帧隐藏 + `removed` 即时推送 + 下轮 Rescan 出表；`missing` 字段注释如实标注退役，30+ 处恒真守卫的物理清除随 M7a 批② AssetIndex 搬运批（登记项）。
2. **引用面判据**（保守保留的关键）：`GuidReferenced(guid)`——项目数据文本（scene/prefab/anim/override/controller/tab/rml/rcss/cs/asset；含资产扫描排除的 Game/Scenes/Data；**不含 .meta** = 自引用假阳性；单文件 16 MiB 上限）语料惰性装配，guid 三形态检索（hex 小写/大写/十进制——十进制全串 19-20 位子串命中 ≠ 巧合数字）；语料随 Rescan/OpenProject 起点失效。
3. **孤儿 .meta 三态**：源缺失 + **零引用 = 扫描期自动删**（一行日志；坏档/无 guid 解不出 = 无从被引用，判垃圾一并清）；**仍被引用 = 保留 + 红字**（"只恢复源文件"场景的复链钩子，盲删永久断引用）；源+meta **双删仍被引用 = 红字一次**（悬空可见性；与孤儿保留红不双报——按 meta 存在与否分流）。
4. **手动入口**：Assets 菜单「清理孤儿 .meta…」（`MenuSweepOrphanMetas`）→ 同判定立即执行 + 报告窗（清了什么/留了什么为什么留；保留区黄色警示，不盲清被引用项——比 Godot 社区插件多引用视图）。浏览器右键"删除（转墓碑）"文案同步改"删除"。

## 实测

- **阴性验证**：新语义测试先行落在旧代码上——`Expect` fail-fast（abort），首断言 `deleted asset entry dropped (no tombstone)` 红（旧行为保留墓碑）；桩 API 空返回同验。
- engine-tests **34040 → 34052**（+12）：`TestOrphanMetaSweep`（三态 12 断言：引用孤儿保留红恰一次 / 零引用清扫 / 双删引用红与保留红共存计数 = 2 / 手动 Sweep 报告与落盘复验）+ 生命周期测试墓碑段重写（出表 / 号不回收 102 / 零引用孤儿清 / 体检 0）。
- ctest 3/3；回归 full **17/17**（含 asset-chain/guid-chain——改名/导入/GUID 解析链零回归）。
- **svr-test 实弹（策略自证）**：开项目即日志 `清理孤儿 .meta（源已删且零引用）：Assets/player/player01/player001_nobg.png.meta`，盘上文件消失，**体检红字 1 → 0**；音频装载 9 成功（流式 2）、errors=0、cold-start 2590ms 零回归。

## 发现

1. `Expect` 是 fail-fast（abort）——阴性验证 = 首断言红即证，多断言需分轮（本轮桩验证补齐了 sweep API 面）。
2. `lastChange_.removed` 是 GPU 幽灵页回收（`RescanAssets → Evict`）的触发链——墓碑退役后编辑器内删除必须自行推送 removed，否则幽灵页泄漏（已补）。
3. 引用语料必须排除 .meta 自身（每个 meta 都含自己的 guid = 人人"被引用"）；引用面必须**包含**资产扫描排除的 Game/Scenes/Data（C# 常量/场景 spriteGuid 十进制恰在排除区——svr-test kPlayerPrefab 实例）。

## 落账

[06 §2.2](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) 修订注记（墓碑段就地标注）；[M7a.md](../Plans/M7a/M7a.md) 决策点 D9 + 现状盘点 #9 + 批② 登记项（missing 守卫清除 + AssetIndex 继承清扫判定）；AGENTS.md 阶段行。
