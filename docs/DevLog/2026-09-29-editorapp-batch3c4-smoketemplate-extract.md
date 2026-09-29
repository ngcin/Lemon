# EditorApp 批③c-4：smoke-template 族外迁（六挂点 + 独立 TU）

- 日期：2026-09-29（批③c-3 同日续）
- 性质：Run 正文机械外迁（挂点原位逐位不变）；③c 第四族，两处与前族不同的
  形态决策：①**独立新 TU**（EditorAppSmoke.cpp 已 1900 行超 ~1500 软上限，
  本族再塞将破 2400）；②**状态跨 TU 共享**（EditorApp.cpp 留两处原位读点，
  结构体定义进 EditorAppSmoke.h + extern，实例定义随函数族落新 TU）。

## 改动

- EditorApp.cpp：2544 → 2115 行（累计 7212 → 2115，-70.7%）。
- 新 TU `Editor/App/EditorAppSmokeTpl.cpp`（495 行）：
  - `TplSmokeState g_tplSmoke;`（批③b 已收敛的结构体，字段逐项原样随迁）；
  - 六函数：`SmokeTplSeedProject`（向导复制 tempdir + OpenProjectPipeline
    前置，`return 1`→`return false`）、`SmokeTplSeedScene`（Main.scene + 预置
    存档双载体 + 选玩家；**else-if 场景开链守卫留原位**，只外迁分支体）、
    `SmokeTplPlaySetup`（uiLoads 基线 + 无捕获 event sink lambda——文件级
    存储刚需之一，跨 TU 合法）、`SmokeTplSteer`（kDeathArm 环绕风筝/站桩
    转向注入，挂点 ApplyInput 紧前）、`SmokeTplSample`（HUD 文档六要素 +
    进度条盒 + 受击切段 + fx + 卡片直灌点击三帧 + 层序三拍 + 死亡/复活链 +
    低频诊断快照，226 行）、`SmokeTplVerdict`（tplOk/存档落盘/第二项目
    spriteId 记账三段聚合）。
- EditorAppSmoke.h：+38 行（TplSmokeState 定义 + extern）。
- EditorApp.h：+9 行（六私有声明 + `ecs::InputState` 前置声明）。
- Editor/CMakeLists.txt：+1 源。
- **留驻 EditorApp.cpp 的两处原位读点**（共用经 header extern）：
  渲染段 capReq 捕获请求块（L1230-1232，与 uirml 捕获块同段待遇）+
  FeedGameUiInput 的 pointerHold 让位窗（L1931；批④ GameUiBridge 再迁）。

## 等值变换说明（非逐字部分）

- SeedProject/SeedScene 的 `return 1` → `return false`（挂点 `if (!f()) return 1;`）。
- Verdict 三处 `if (!x) exitCode = 1;`（tplOk/savOk/idOk）→ `verdictOk`
  聚合返回（挂点 `if (launch.smokeTemplate && !SmokeTplVerdict()) exitCode = 1;`）；
  printf 顺序与内容逐位不变（失败态诊断行不丢失——早退方案会吞 saves/第二
  项目两段打印，已否决）。
- 守卫 `launch.` → `Launch()`（③a 已核证的等值前提）。

## 缺陷与拦截（本批真值所在）

- 拼接脚本漏行：`SmokeTplSeedProject` 的 `#ifndef LEMON_SCRIPT_DIR` 分支丢了
  `return false;`——脚本构建（LEMON_SCRIPT_DIR 已定义）下为死分支，编译与
  16/16 回归均绿；**程序化逐行比对实抓**（HEAD 25 行 vs 新 TU 24 行），
  无脚本构建将落"无返回 UB + 行为漂移"。修补后重建 + 回归重跑。
- 复核脚本自身四轮伪差（函数体提取 off-by-one、头文件切片 1-based 混用、
  实例定义行带尾注释用全等、读点计数把 capReq 三行当一个）——均为脚本
  口径错误，逐一修正后复核通过；代码侧唯一真缺陷即上条。

## 验证

- 构建：修补轮后链接干净（唯一 warning 为 ③c-3 既存项，与本批无关）。
- 回归 full 16/16：修补前一轮全绿（缺陷在死分支）+ **修补后终树重跑全绿**
  （template-chain 直判：wizard copy/build/play hud/cards/save/death-revive）。
- 程序化 review：六函数体 + 结构体对 HEAD 逐行等价重建、EditorApp.cpp 恰余
  两读点、挂点序/邻居正确（287 < 337 < 461 < while 533 < 1017(ApplyInput 紧前)
  < 1258(firstFrameMs 紧前) < 1629(anim verdict 后)）、新 TU 无 exitCode/裸
  launch 残留（仅函数头注释提及变换）。

## 后续

- 批③c-5：smoke-uirml 族（Run 尾最大遗留；T5 局部已识别留驻）。
- 批③c-6：final/bench 指标族 + Run 三挂点收口。
- 批④：GameUiBridge（pointerHold 读点随 FeedGameUiInput 迁走后，g_tplSmoke
  可退回 EditorAppSmoke.cpp 单 TU 内部）。
