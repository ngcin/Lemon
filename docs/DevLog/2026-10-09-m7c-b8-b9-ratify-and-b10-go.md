# M7c 裁决落账：批⑧ D1–D3 + 批⑨ D1–D5 全追认 + 批⑩ 方向拍板

- 日期：2026-10-09（批⑨ 后修同日，svr-test 复测过后）
- 关联：[b8 批文件](../Plans/M7c/2026-10-08-b8-loadscene-async.md) · [b9 批文件](../Plans/M7c/2026-10-09-b9-svr-test-multiscene.md) · [b9 后修 DevLog](./2026-10-09-m7c-b9-post-fix.md)（复测 26074 帧零错误）· [M7c.md](../Plans/M7c/M7c.md)
- 性质：用户拍板事件——两批共 8 个"未应答按推荐推进"裁决点正式追认 + M7c 下一步方向定案。

## 追认范围（全部按推荐项，零翻案）

- **批⑧ D1–D3**（LoadSceneAsync）：D1 staging 暂存 registry + 激活帧原子集成 / D2 Resolve·Assets 双段占位（Assets 段承担 DOM 分帧回收）/ D3 跨同步/异步单槽 last-wins。
- **批⑨ D1–D5**（消费者迁移）：D1 code-mount 壳 + DDOL GameFlow 种子 + 重装自毁守卫 / D2 战斗场自含实体 / D3 键盘位回放（UI 指针入流留候选池）/ D4 模板 MainMenu+Grass 两场景 / D5 async 进场 + 同步回菜单。
- 追认时点基线：单测 34,716 / script-tests 1,818 / ctest 4/4 / 回归 21/21 / 金回放三档 mismatches=0 / bench fps=88；批⑨ 真人走查首轮两缺陷已修、复测零错误（用户未再报）。

## 方向拍板

- **批⑩ 转正**（原"可选"）：编辑器打磨——Play 态 Hierarchy 场景组显示 + DDOL 徽标 + i18n 词条 + smoke-template 双 Play 段钉板（批⑨ 后修登记的顺手项，帧预算 3400→~3700 需评估）。
- **批⑩ 后 M7c 收尾转 M8**（光照与打磨）；执行序维持 M7c → M8 → M9 → M7b。

## 落账面

- b8/b9 批文件 Status 行、b9 §2 标题、M7c.md 状态行与批次表批⑩ 行、AGENTS.md 状态行——「待追认」标记全数清除，指向本条目。
- DevLog 历史条目（2026-10-08/09 两份）按快照惯例不改——其中"待追认"字样为落笔时事实，以本条目为准。

## 备注

- 批⑨ 真人走查在 AGENTS.md 待用户清单的正式销项仍留待用户（复测已实证过，见 b9 后修 DevLog §复测）。
