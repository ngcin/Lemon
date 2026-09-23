# 2026-09-22 M5 清障②：编辑器 Play 的 Spawner/Shooter prefab 桥（SpawnFn）

此前 EnterPlay 不注册 `World::SpawnFn` → 编辑器 Play 中 Spawner/Shooter 哑火
（warnedNoFactory_ 一条告警了事；prefabId 仅 bench-sim 硬编码表 1=怪/2=弹 有意义）。

- **映射约定**：`Spawner.prefabId` / `Shooter.projectileId` 的 uint32 = **prefab
  资产 GUID 低 32 位**（03 §69 schema 恒 uint32；碰撞概率可忽略 + EnterPlay 构建
  时检测 WARN；M7 烘焙 dense id 表同语义替换）。
- **EnterPlay**：`BuildPlayPrefabCache()`（Prefab 资产 → {低 32 位 → guid+JSON
  文本}，进 Play 时刻快照——与 editSnapshot_ 同语义，资产变更不追）+
  `playWorld_->SetSpawnFn` → `SpawnPlayPrefab`（team 覆盖实例根 Meta、错绑 id
  去重告警）。
- **InstantiatePrefabAsset 重构**：抽无 IO/日志/dirty 的核心
  `InstantiatePrefabJson`——高频刷怪不能走交互路径（每 spawn 一条 LEMON_LOG 会
  刷屏）。性能边界：每次 spawn 仍 parse JSON（小树几十 µs），物化模板+池拷贝是
  03 §10 正式工作，bench-survivor 不及格再升级。
- **Inspector**：Spawner/Shooter 段尾反查标签（prefab 文件名 + 完整 guid / 无效
  id 提示）。拖拽绑定进 M5 编辑器批次（与 tag 资产化同族）。
- **测试**：`TestPlaySpawnPrefab`（16 断言）：桥直调（回链/team/位置覆盖/无效 id
  不崩）+ SpawnSystem 集成（12 帧 ≥2 burst 树实例）+ Stop 零泄漏（快照重建 +
  编辑场景 prefab 回链完好）。

**回归**：engine-tests **13026 checks**（+16）；script-tests 1281；
editor-regression full **11/11**。
