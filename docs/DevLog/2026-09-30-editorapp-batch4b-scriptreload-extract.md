# EditorApp 批④-2：脚本编译与热重载管线八函数外迁

- 日期：2026-09-30（批④ 同日续；批④ 未提交续作——两批共用 EditorAppScripts.cpp，
  提交时合并为一个 commit 或按 DevLog 拆注均可）
- 性质：EditorAppScripts.cpp 主题收敛收官——脚本编译/热重载链独立成 TU 后，
  原文件只剩项目打开管线 + Play 守卫/清场 + 两模态（更名"项目与 Play 管线"）。
  全批为纯函数搬移：八函数皆 EditorApp 成员（声明在 EditorApp.h 不动），
  无文件级状态、无全局、无等值变换、调用点零变化——是历批中最简的一批。

## 改动

- 新 TU `Editor/App/EditorAppScriptReload.cpp`（240 行，CMake +1 源）：
  - 编译排队：`QueueScriptRebuild`（§5-5 先画提示帧再阻塞）；
  - 错误红字：`LogCompileErrors`（§5-6 CSxxxx 绝对路径裁项目相对）；
  - csproj 发现：`FindGameProject`（项目打开/Play 阻断/换装三处共用的判定辅助）；
  - 宿主装配：`InitScriptHostFrom`（含 `#ifdef LEMON_SCRIPT_DIR` 双分支原样）；
  - 源码变更检测：`ScriptSourceChanged`（obj/bin/隐藏目录剪枝 + 基线化）；
  - 编译+换装：`TryHotReloadScripts`（无宿主首装恢复路径 + 复用宿主 A 线）；
  - 手动触发：`MenuRebuildScripts`；状态读取：`HotReloadCount`。
- EditorAppScripts.cpp：426 → 244 行。留驻六函数（OpenProjectPipeline /
  PlayBlockedByScripts / TryEnterPlay / StopPlay / MenuNewProject /
  DrawRecoveryModal）；段旗"项目/脚本管线"改题"项目管线"（脚本半边随迁，
  登记处置）；文件头注释更名"项目与 Play 管线" + 批④-2 外迁注记。
- EditorApp.h：零改动（批④-2 唯一头文件零触碰批）。

## 验证

- 构建：首过零告警。
- 回归：`tools/editor-regression.sh full` 16/16 PASS（跑前 pgrep 确认无残留
  实例）。script-chain（CoreCLR/spawn/play byte-exact/--validate）与 final
  （Play 中热重载 + StateBag 续跑）直接覆盖新 TU 全链，均一次过。
- 等价复核（程序化，对 HEAD 7237da5——八函数在批④中未被触碰，可直接比对）：
  - 迁八函数逐字节一致（零等值变换——本批无 return/守卫/命名任何形态改动）；
  - 留六函数逐字节一致；新 TU 函数序 = 原文件相对序；
  - 三处切缝干净（段旗居 OpenProjectPipeline 前 / OpenProjectPipeline 直接
    PlayBlockedByScripts / DrawRecoveryModal 直接收 namespace 尾）；
  - EH 仍为批④ 的净增 5 行（本批零触碰）、CMake 累计净增 2 行、
    新 TU include 块与 ES 逐字节同。
  - 复核脚本三处初始 FAIL 均为提取启发式边角（段旗紧贴函数、单行函数体的
    列 0 `}` 匹配到下一函数），精确重验逐字节通过。
