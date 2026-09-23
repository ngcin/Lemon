# 2026-09-20 · 编辑器使用测试指南 + 一键自动化回归（9/9 PASS）

**`docs/EngineDesign/Editor-Manual-Test-Guide.md`**：按工作流分区（A 启动/项目 →
L 退出状态机）12 区 70+ 项，每项 操作步骤 + 预期 + 自动化覆盖标记（`[自动]`/
`[手测]`）；§3 登记已知观察项（spriteId 漂移自愈/dotnet 阻塞/ALC 泄漏）防止当缺陷
误报；§4 运行记录表。`[手测]` 项 = 真人专属路径（OS 拖拽/IME/Gizmo 手感/模态观感，
决议 R5 无 UI 录制回放）。

**`tools/editor-regression.sh`**（quick|full [build-dir]）：ctest + 基础冒烟 +
`--smoke-close` 双态 + 资产链 + 脚本链（缺 dll 自动 SKIP）+ `--final` + 场景 CLI
roundtrip（`--save-scene` → `--scene` 重开），末尾 PASS/FAIL 汇总（exit code 即判据）。
本轮实跑：full 9/9、quick 4/4 PASS。

**脚本坑**：macOS /bin/bash = 3.2——`$MODE，`（变量名紧跟全角标点）会把高位字节
并进变量名报 unbound（C locale 高位字节可入 name 判定）；全部展开改 `${BRACED}`
规避。参数解析一并修正（单参 quick 曾被赋给 BUILD）。

---
