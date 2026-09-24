# 2026-09-24 · 全栈审查修复批：P0×1 + P1×11 + 存量 Debug 断言×2（回归 14/14）

来源：[Engine+Editor 全栈审查](../Reports/)（六路分模块深读 + EditorApp 补审，约 2.3 万行）
发现的按优先级逐项修复。本批全部为**代码修复 + 回归验证**，无设计变更。

## 修复清单（按修复优先级）

**P0**：`Rng::Range` span 为二次幂时拒绝采样 zone 截 0 → 永久死循环（`Range(0,1)`
抛硬币即中招，经 `lemon_rng_range` 直接暴露给 C#）。修复：二次幂整除 2^32 本就
无拒绝区间，直接取模；C++/C# 双端同修（`Random.h` + `Pcg32.cs`），engine-tests 补
二次幂覆盖、script-tests 补双端位级对拍。

**编辑器三件**：
- 多选 Delete：迭代 `Selection()` 中 `DestroyEntityTree` → `PruneSelection` 换掉
  底层缓冲 = range-for UAF（偶发漏删）→ 先拷贝再迭代（与 Ctrl+D 路径同款）。
- `NewScene`/`OpenScene` 不清 Undo 栈：切场景后 Ctrl+Z 把旧场景整份灌进当前场景
  → 成功后 `undo_.Clear()`（与 `EnterPlay` 对齐）。
- `RevertPrefabInstance`：`m->prefabId` 在销毁+`LoadEntityTree` 后从旧指针读
  （Meta 池扩容搬移即悬空）→ 按值保存。

**资产链三件**：
- Rescan 路径命中分支不补 spriteId：`.txt → .png` 改扩展名后 spriteId=0 直达
  `AddSpriteAt(0)` 必失败 → 命中分支末尾补 `if (Sprite && id==0) 发号`。
- `ImportSprite` 登记失败回滚不注销图集页：残留注册条目让下次同槽导入撞
  `RegisterAtlas` "atlas slot reused" 断言（下次导入必崩）→ 新增
  `AtlasRegistry::UnregisterAtlas`（页上有活 sprite 断言拒绝）回滚同步注销。
- manifest 健壮性：`SaveManifest` 改 `WriteFileAtomic`（.tmp + rename 原子替换；
  工具从 EditorContext 匿名命名空间提升到 `AssetDatabase.h` 共享）；装载侧逐字段
  类型校验（坏条目跳过 + 红字，不再 json::type_error 直达 terminate）+ 空文件
  显式告警。

**引擎悬空二件**（enTT 池扩容搬移同型）：
- `SceneSetParent`：两次取引用之间夹对同一 Hierarchy 池的第二次 `Emplace` →
  先确保两组件存在、再取引用写链。
- `SpawnSystem`/`DirectorSystem`：View 迭代中调 spawn 工厂（向组件池追加）→
  `sp/tf/wd/wave` 引用与 view 迭代器悬空。改为**请求表延迟执行**：迭代段只产
  请求（RNG 在请求时消耗、冷却/配额随请求记账），循环外按请求序统一 spawn +
  推 Spawn 事件 = 与旧实现逐位同序列（final 验收 StateBag=66 精确刷怪数不变证）。

**事件派发窗口**（P5）：#16 旧实现 `At(i)` 引用 + 末尾 `Clear()`——回调
（EventSink/C# 订阅者）内再 Push 的事件被整体清掉（静默丢失），Push 触发 `Grow`
扩容时引用/桥侧 `HeadSpan` 段指针悬空。改为 `RingQueue::TakeAll` 派发前整体取走
（快照与队列底层分离），窗口内新事件留队列下帧派发；`IScriptBackend` 拆
`PullPendingEvents` + `DispatchEvents(稳定快照)` 两阶段，C# 桥两段零拷贝改转发快照
（48B×N 拷贝代价可忽略）。语义核对：C# 侧再入队走 `s_pending` 缓冲本就不进 ring，
现有路径无行为漂移（金回放口径不变）。

## 顺带修复（存量 Debug 阻断，非本批审查引入）

mac-debug ctest 此前红（Release 的 EnTT 内部断言被编译掉掩盖）：
- `TestSpatialHashQueryFastPath`：`t2` 重复 `Emplace<Meta>`（Debug 撞 "Slot not
  available"，Release 静默重复入池）→ 改 `Get`。
- `TestPlaySpawnPrefab`：ExitPlay 快照重建后用进 Play 前旧句柄 `Get<Meta>(mob)`
  （Debug 撞 "Set does not contain entity"）→ 按 guid 重找。

## 验收

- Release（mac）：ctest 3/3 + `editor-regression.sh full` **14/14**（smoke-drag
  套件首跑偶发注入时序 flake 一次：同二进制单独跑 5/5 OK、复跑套件 14/14，与本批
  改动无关——拖拽路径零改动）。
- Debug（mac-debug）：ctest 3/3，engine-tests **23267 checks OK**（EnTT 内部断言全开）。
- determinism 证据：template-chain 3000 帧、final 验收（含 StateBag=66、Play
  byte-exact）全过 = 延迟 spawn 与事件派发改动序列逐位一致。

## 未修（留待后续批次）

审查报告 P2/P3 项：粒子 blend/filter 入口钳制、旋转精灵剔除半径（半对角线）、
`lemon_play_reset` 误清静态订阅、批量 systemIndex 错位占位、Inspector EndCombo/
Degree Undo、Play 态创建/删除落错世界、FocusSelection zoom 量纲、窗口焦点丢键、
Gizmo 父子世界→本地换算等（完整清单见审查汇总，按 P2→P3 顺序排批）。
