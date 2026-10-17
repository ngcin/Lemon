# M8 批⑮a：终态（terminal）显式建模——any 边在终态不命中

- 日期：2026-10-17
- 批文件：[b15 §2 行 1](../Plans/M8/2026-10-17-b15-anim-runtime-semantics.md)（排期 §6：⑮a 紧随批⑬b 同窗口，⑮c 顺手清顺延下批，⑮b 挪 M9 首批）
- 关联：ADR-013 D4 增补（terminal 段，2026-10-17）；04 §3.2 注记；批⑪ any-state（[DevLog](./2026-10-16-m8-b11-any-state.md)）

## 改了什么

- **schema**（`ControllerAsset.h/.cpp`）：`ControllerData.terminal` 终态白名单 +
  顶层可选 `terminal: [...]` 解析（条目须列于 states、不重名——首错即返）+
  序列化（仅非空写出，插在 states 与 transitions 之间；空档不落键 = 旧格式
  逐位不变）。旧引擎读新档忽略 / 新引擎读旧档缺省空——双向兼容，schemaVersion
  不动。
- **词表/编译**（`ControllerTable.h` + `PlayCaches.cpp`）：`ControllerDef.terminal`
  states 序位图 + `IsTerminal(idx)`（越界/空位图 false）；BuildControllerCache
  白名单 → 位图编译（悬空防御回退）。
- **运行时**（`Systems.cpp` AnimGraphSystem 出边循环）：单条件守卫——
  `t.from == kAnimAnyFrom && ctrl->IsTerminal(cur)` → skip。any 边组内序/状态边
  路径不动；无 terminal 档位图全 0 → 逐位零漂移。
- **lint**（`ControllerStructuralWarnings`）：终态挂**条件**出边 → info 级黄字
  （"复活流请确认"）；无条件 exitTime 出边不告警（合法自动续）。
- **编辑器**（`AnimControllerPanel.cpp` + `BuiltInPanels.h` + anim.json 双语）：
  节点终态徽记（双圈 + 角标"终/T"）+ 检查器复选（tooltip 语义）+ 右键 toggle
  （勾态显示）+ `ToggleTerminalState` 单一落点（撤销/dirty 同通道）；改名/删状态
  白名单引用传播；TrySave roundtrip 比对面补 terminal；i18n 四键双语。
- **消费者**：`Duelist.controller` 落 `"terminal": ["Death"]`；
  `DuelBehaviour.Strike` 删对手 HP>0 受击门控（"死亡中不受击"由图侧声明式挡下，
  脚本只剩存活/组件检查）+ HUD 对手血量行死亡口径补 `Cur > 0`；
  `AnyStateProbeBehaviour` ⑦ 观察位升格为计分断言（6/6 → **7/7**）：
  "Death 中再发 hit → 驻留 Death（终态 any 边不命中）"。
- **测试**：EditorTests 批⑮a 块（terminal 解析拒例 ×2 + 缺省空不落键 + 序列化
  字面 + roundtrip 等值 + lint 三例 + 评估三例——表级模拟 AnimGraphSystem 单遍
  扫描含守卫：对照组无守卫 any 先命中 / 守卫开 any 不命中且显式复活边胜出 /
  空位图全非终态）；smoke-anim f138-140 终态链（toggle walk 黄字 +1 → 还原 →
  toggle hit（纯 exitTime 出边）零告警 → 基线还原 + 关窗），旗标三位进聚合行
  `term(toggle/lint/clean)`。

## 机器面证据（2026-10-17）

- 构建 Release 零编译警告（ld duplicate-library = b1 既有登记）。
- 单测 **35,024 checks OK**（+12）；script-tests **1,918 OK**（无 C# SDK 面改动）；
  ctest 4/4。
- `--smoke-anim` zh+en：`term(toggle=YES lint=YES clean=YES) => OK`，既有位零回归
  ——**帧预算 138 → 141**（f138-140 终态链占用，回归脚本双步已重锚）。
- 回归 full **26/26 PASS**（bench-survivor 门禁 fps=82 ≥ 76.5；首轮全绿无复跑）。
- **金回放三档零重录**（现录现放，09 §6.8 口径）：sim-st
  `replay=PASS mismatches=0`（alive=8099/created=16003/destroyed=7904）+ sim-mt
  `replay=PASS mismatches=0`（threads=5 终态三元组同）+ bench-script PASS。
  基准场无 terminal 档 → 位图全零路径逐位不变，理论口径实跑坐实。
- **anytest.scene 无头 7/7 PASS**（lemon-game --scene Scenes/anytest 600 帧）：
  含 ⑮a 核心断言"Death 中再发 hit → 驻留 Death"。
- **DuelBehaviour 行为对拍零漂移**（09 §6.8 口径）：git stash 旧版（门控在 +
  Death 无标记）vs 新版（门控删 + 落标记）各跑 ani.scene 1500 帧，slot_0.sav
  键集全等 + `duel.winner` 同值——1v1 场景攻击者在对手死亡当拍即停（Update
  对手亡分支不再触发 attack），删除的 HP 门控本无可触发窗口，伤害时间线/RNG
  消费序逐位不变（m8b5 档 sim 两档零漂移与对拍互证，无需重录）。

## 裁决与登记

- "缺省空 = 旧行为"由 schema（空档不落键）与运行时（空位图守卫恒 false）双面
  保证——terminal 是 additive 块，加字段零重录（09 §6.8 先例三口径：零实例
  字段迭代不发生）。
- lint 分级拍板：终态**条件**出边黄字（info 级、复活流确认）、exitTime 出边
  不告警——"图不越权"（b11 验收语）延续：显式边永不被拦。
- anytest 探针 _total 6→7（⑦ 转计分）；smoke-anim 帧预算 141 登记（f141 空位
  一枚）。
- 下一步：⑮c（`Anim.IsFinished` 直读 ended + IsPlaying 注释三态口径——批文件
  §2 行 3，零引擎改动）→ ⑬c payload 类型化 → 批⑭（样本+手册+模板，带 ⑮d
  Play 热改提示横幅）。
