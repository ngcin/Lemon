# 2026-09-23 · M5 批②：导演波次（WaveDirector 组件 + DirectorSystem + WaveStart 事件）

T1–T5 全完（分解见[批②计划](../Plans/M5/2026-09-23-b2-director-waves.md) §11–§15）。03 §8 原案修订为**组件内联波次表 + 直接出生
通道**（D1/D2：数组段序列化零新基建、导演直接经 SpawnFn 出生、Spawner 保留常驻
环境刷怪——修订注已写入 03 §8）。

- **组件**：WaveDirector 1260B（16 波 × 每波 4 条目，WaveDef 76B/WaveEntry 16B；
  RT=time/waveIndex/cd[4]/spawned[4]）；目录 13 字段行 + 数组段 19 元素行（entries[4]
  扁平 e{0..3}* 命名，单层数组段不支持嵌套）；REGISTER_ED_SEG 新宏（ED + seg 并持
  首例）；C# 镜像三结构 + 探针行全绿（探针计数断言 27→28 两处同步）。
- **系统**：DirectorSystem 真实现（占位 #2 转正，**管线零重编号**）；顺序相位状态机
  （波重叠=后波接管）、capAlive 同队闸门（30 tick 普查 + 乐观自增）、RNG 子流 1
  启用（Spawn 的 2 不受扰）、prefab 失败即废止条目。timeScale=0 冻结波次（批① D5
  一致）。
- **发现并修复（引擎缺口）**：`ScriptHost::DispatchEvents` 期间 `g_world/g_scene`
  未置位——事件回调内 `Ui.Set/Time.Scale/Instantiate` 静默空转（批① xp 样例恰在
  Update 内调用故未暴露）。修为与 Update 同窗口（04 §4 已记）。
- **Inspector**：数组段只读 Text → 按 FieldType 裸派发编辑控件（DragFloat/
  InputScalar/Checkbox），active/deactivated 汇入 M4.7d 属性轨；StatusEffects/
  Inventory 调试编辑同受益。WaveDirector 作者路径 = 改 waveCount 出槽位 + 表格
  填值（M6 波次表编辑器前过渡形态）。
- **测试**：engine-tests 13098 → **13127**（+29，TestWaveDirector 六件套：波时刻/
  事件契约/出生环/team 覆盖/晚波、ramp 加速 + 重叠接管、capAlive 精确闸、timeScale
  冻结、归档 roundtrip + RT 不入档 + 超容钳、孪生世界哈希）；script-tests **1460**
  （+TestWaveStartToUi：人工推 WaveStart → C# 订阅 → RtUi 槽断言）。
- **金回放（执行勘误）**：分解预期"零重录"**判断失误**——`ComputeStateHash` 对注册表
  组件名无条件入哈希（schema 漂移绊线，含零实体组件）→ 新增组件必致全帧哈希漂移。
  m5b0 旧档 replay 全帧 mismatch 而**终态逐项一致**（alive 9682/created 11203/
  destroyed 1521）= 行为零漂移旁证 → 按 09 §7 重录 **m5b2** 三档，replay
  mismatches=0 ×2 + script PASS。**推论已沉淀 09 §7：加字段可零重录、加组件必
  重录**（批次规划按此预判）。
- **bench-survivor 导演化（三跑逐位一致）**：Spawner 闸 10000→8000 让位 + BenchDirector
  3 波 × 4 条目（180/s/波，t=1/6/11s）→ alive **10435**、frameAvg **15.11~15.20ms**
  （≤22.2）、fps **66/66/66 PASS**、waves=3、teamAlive **10002**（顶满 capAlive
  10000，突破 Spawner 闸 = 导演通道实证）；**Director avg 0.068ms**。首跑教训：
  波容量估算漏了"后波接管"语义（3 波各 2 条目仅 ~1170 出生，alive 9565 FAIL）→
  4 条目/波 + 波前移后 PASS——重叠语义的容量预算按"仅末波满速"算（M5.md T4 注）。
- **回归**：ctest 3/3；editor-regression full **11/11**。文档回写：03（§3.3 组件行/
  §4 表 #2 行/§8 修订块/§11 payload）、04（§4 事件窗口 + 样例）、05（§5 可编辑
  数组段）、08 §3 表注、09（§6.10 导演化行 + §7 重录推论）。

---
