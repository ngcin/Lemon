# WIP（冷启动文件——永远只记当前态）

- **停在哪**：动画创作面打磨系列——批⑨（`8ff48d1`）与批⑩ 两提交（⑩a `123e240` + ⑩b `f659fc9`）已落；批⑩ review 修复（B1 把手单击误建自环 / B2 Play 态相机被只读域冻结 / B3 保存后回读清选择+撤销栈 / B4 相机键缺 WantTextInput 守卫 / B15 Play 态提示文案）**机器面全绿，待提交**：证据 = 构建零警告/单测 34,959 持平/ctest 4/4/smoke-anim zh+en `errors=0` `ctrl(...ro=YES zoom=YES click=YES) => OK`（帧预算 134）/smoke-template OK/**三变异验证**（B1 去阈值→自环复现 click=NO；B2 画布回 disabled 域→zoom=NO；B3 去 loadedHash_ 同步→gate=NO）/金回放零重录；DevLog 已写（`2026-10-16-m8-b10-review-fixes.md`），review 报告入提交。
- **下一步第一个动作**：`git add`（6 改 + review 报告 + 新 DevLog + 本文件）→ 提交 `fix(m8-b10): review 修复——…`；然后向用户报批⑩ 收口（三提交）与遗留登记（B5–B7/P3 不修、Out 候选池、撤销栈统一），真人 15 分钟观感过目（归批⑫）。之后开工批⑪ 帧事件 UI。次选：D 组 #M4 Windows 真机轮 / M9；B 档基建（分级门禁 L0/L1 + bisect 包装）待排期。
- **踩到一半的坑**：ImGui 事件 trickle（`ConfigInputTrickleEventQueue` 默认开）——注入 pos 后同帧 wheel 必顺延一帧（`imgui.cpp:11302`），smoke 注入帧与生效帧恒差一；smoke-anim 帧预算 134（edit→Play 分段：B1 编辑态、B2 Play 态）。⑩b 遗留登记：面板本地撤销栈与场景结构轨 Undo 不互通——后续统一；Play 参数实时值浮层 = v1.1 触发式。
