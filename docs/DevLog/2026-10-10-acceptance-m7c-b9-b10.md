# M7c 批⑨/批⑩ 真人走查通过：svr-test 多场景全流程 + Play 态 Hierarchy 场景组（2026-10-10）

[批⑨ 批文件](../Plans/M7c/2026-10-09-b9-svr-test-multiscene.md) · [批⑩ 批文件](../Plans/M7c/2026-10-09-b10-editor-polish.md) · [批⑨ DevLog](./2026-10-09-m7c-b9-svr-test-multiscene.md) · [批⑩ DevLog](./2026-10-09-m7c-b10-editor-polish.md)

## 事件

用户完成 AGENTS 行动项⑥⑦ 两项真人走查，**均通过、零缺陷实报**（构建 = HEAD 369b314，批⑪ b11c+b11d 收口后当日增量确认 no work to do）。走查口径 = 两批文件 §4 / 行动项原文：

- **批⑨ svr-test 多场景全流程**（行动项⑥）：编辑器开 MainMenu.scene 进 Play，菜单 R/点击 → 草地加载屏 → 战斗 → 死亡复活 → 结算 R 重开 → Esc 回菜单 → 火山（困难）；键盘 R/空格/Esc 与鼠标两路。首轮（2026-10-09）实报两缺陷已修（[后修 DevLog](./2026-10-09-m7c-b9-post-fix.md)，机器复测 26,074 帧零错误日志），本轮复验通过。
- **批⑩ Play 态 Hierarchy 场景组**（行动项⑦）：开局换 Grass 后组头切换（MainMenu 消隐 / Grass + DontDestroyOnLoad 两组可见、GameFlow 带蓝色 DDOL 徽标）→ 死亡/重开/Esc 回菜单组头复原 → 火山同链；Window→语言 切 English 过组头文案。

## 意义

M7c 换场体系（批⑤ ADR-017 → 批⑥⑦⑧ 引擎核心/SDK/异步 → 批⑨ 消费者迁移 → 批⑩ 编辑器可见性）真人面收口。待用户清单剩：① 批① 血条正式素材观感复验（等素材落地） / ② 批② 音频覆写真人听感 / ⑤ 批③ 编辑器 i18n 全面板走查（en 态选帧对话框「替换为」越窗已知项在 ⑤+ 候选池，非阻断）；W5 真机 GPU、M4.8 细化维持后移不设期。M7c 收尾仅余上述走查项与文档落账（M7c.md/08/AGENTS.md 状态收口）。
