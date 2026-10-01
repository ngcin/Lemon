# M7a 详细规划出台：独立运行时 + 最简出包（mac 先行 → Win 真机收口）

- **日期**：2026-10-01（M6c 收官同日接续）
- **事件**：M7a（`Engine/Assets` 运行时资产层 + GameEntry/`lemon-game` + 目录拷贝式 packager）详细规划落位。[M7a/M7a.md](../Plans/M7a/M7a.md)：现状盘点 11 条（开工前探索核实，含 EnterPlay 装配搬运清单与打包破坏点清单）+ 决策点 D1–D8（待开工日拍板 → ADR-016）+ 批⓪–批⑧ 拆解（≈16.5–20.5 工作日）+ 出口判据映射 + 范围边界/风险/登记项。
- **用户拍板（本日）**：**M7a 内平台顺序 mac 先行 → Windows 真机收口**。目标平台集合不变（windows-x64 仍为发布 v1）；"Windows 优先"（2026-09-30 重排）指出包线优先于玩法特性，非 M7a 内部实现顺序。理由：开发机即 mac，全部判据先本机闭环；Windows 依赖真机窗口（M7 批⓪ 尾巴：MSVC 从未真机编译），末批一次验证终态。落账：06 §6.1 注记、M7.md 批① 行、本页。
- **批次一览**：⓪ ADR-016+entryScene 字段 → ① 缺陷第二批（D6/D7/D8/M21/M22–M25；M13/M14 已证伪）→ ② Engine/Assets 资产读取核心（stb TU/AssetIndex/TextureStore/解析器三件下沉/GUID 归一）→ ③ Play 装配下沉（PrefabCache+D5 护栏/SaveStore/UiMount/AudioMount/CameraFollow）→ ④ GameEntry 竖切（mac `lemon-game --project demo/svr-test` 四屏零 C++）→ ⑤ packager + mac 干净包 → ⑥ 图集 `.baked` v1（LAT1）→ ⑦ Windows 真机收口 → ⑧ 收官（CI 每日回归/性能基线门禁 + 文档五区 + 真人验收）。
- **开工前置核对**：demo/svr-test 已知 WIP 既有 FAIL（script-spawn：SpawnerBehaviour 未注册 + prefab guid 低 32 位碰撞，[2026-10-01 DevLog](./2026-10-01-m6c-master-limiter-and-clip-voice-cap.md) 归因用户 WIP 层）——批⓪ 与用户对齐游戏侧修复；回归夹具改用 vs-survivor 模板拷贝件（hermetic）。
- **纪律预告**：M7a 全程 vtable/组件 id/系统序零变动 → 金回放零重录预期成立（例外仅批⑥ 若编辑器侧采纳图集，届时三档重录批内闭环）。
