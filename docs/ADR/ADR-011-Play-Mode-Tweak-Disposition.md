# ADR-011：Play 模式调参改动处置——显式不回灌 + Inspector 横幅提示

- 日期：2026-09-23
- 状态：已采纳（M5 批④落地：行为沿用 M4.3 决议 #5，本 ADR 补齐"显式提示"与决策记录，闭合 08 §2 M5 验收第三条"'Play 中调参→改动回灌'或显式禁用提示（ADR 记录）"）
- 影响：`05-Editor.md`（Inspector Play 横幅）、`08-Development-Roadmap.md` M5 验收、`Editor/Panels/InspectorPanel.cpp`

## 背景

Unity 风格编辑器的经典痛点：Play 模式中改的组件值（调参手感、数值试错）在 Stop 后
全部丢弃，用户白调一场。Lemon M4.3 决议 #5 已实现同款语义（Play 中编辑落 Play
World、dirty 不置位、Stop 即丢、编辑场景按进 Play 前快照逐字节重建），但一直没有
**显式提示**与**决策记录**——用户在不知情下 Stop 丢失改动，属于可避免的挫败。

M5 是玩法调参的密集期（模板三选一数值/波次表/战斗参数），本 ADR 把处置定形。

## 决策

### D1：显式不回灌（Stop 丢弃），Inspector 顶部 Play 横幅常显

- **行为**：维持决议 #5 不变——Play 中所有编辑（Inspector 字段/数组段/结构操作）
  落 Play World，Stop 后丢弃，编辑场景逐字节还原（`LastExitVerified` 既有断言）。
- **显式提示**：Play 中 Inspector 顶部橙色横幅"▶ Play 模式：改动随 Stop 丢弃
  （ADR-011）"；GameView 既有"输入已路由至 Play World"提示行保持。
- **理由**：
  1. **回灌语义在结构性变化下不成立**——Play 中实体增删（刷怪/死亡/脚本
     Instantiate）、prefab 实例化、ScriptBox 托管态（C# 字段无法序列化）都会让
     "哪份改动回灌到哪个实体"失去良定义；只回灌"存量实体字段 diff"则制造
     "部分回灌"的更深陷阱。
  2. **逐字节重建是不可放弃的验收资产**——Stop 后编辑场景 == 进 Play 前快照是
     M4 §3.4 验收 #5（`play-roundtrip byte-exact=YES` 在每轮回归里跑）；任何回灌
     都会破坏该不变量，替代方案 = 字段级 diff 审计轨，成本远超 M5 收益。
  3. **行业同款**——Unity 默认丢弃（该痛点靠第三方 "Play Mode Persist/Recorder"
     插件生态解决）；Godot 4 同为默认丢弃。M5 用户群（自用 + 模板作者）可用
     "改前 Ctrl+S 进快照 / 改后手抄数值"过渡。

### D2：调参的正路 = 编辑态改 + Play 验证（数据驱动面已齐）

M5 的调参主路径本来就不依赖 Play 内编辑：波次表/战斗数值/成长参数全部是组件字段
（WaveDirector 数组段 Inspector 可编辑），**编辑态改 → Play 验证 → Stop（改动已在
编辑场景）**；三选一池等脚本侧常量改 `Game/*.cs` 走热重载（M4.5，Play 中生效）。

### D3：M6+ 重评条件（届时另立 ADR 或本 ADR 修订）

满足其一才立项"Play 调参回灌"：

- 用户实测（手测轮/模板作者反馈）出现高频"Play 中调对了想留住"诉求；
- 引擎已具备**编辑操作录制轨**（(guid, compId, bytes) 流 + Stop 重放），且
  ScriptBox 托管态快照（StateBag 持久化）已落地——两者是字段级回灌的前置。
- 候选形态：Unity "Recording Mode" 式显式开关（默认关），仅回灌 Play 前已存在
  实体的字段级 diff，结构操作（增删/挂摘）一律不回灌并逐条提示。

## 验收与回归资产

- Inspector 横幅：`--smoke-template`/`--smoke --play` 会话肉眼可见（无独立像素
  断言——横幅是纯提示件，回归由 smoke-ui Play 段间接覆盖）；
- Stop 丢弃语义：`editor-smoke play-roundtrip byte-exact=YES`（每轮回归既有）。
