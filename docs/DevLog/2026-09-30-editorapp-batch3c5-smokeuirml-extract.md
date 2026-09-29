# EditorApp 批③c-5：smoke-uirml 族外迁（三挂点 + 独立 TU）

- 日期：2026-09-30（批③c-4 次日续）
- 性质：Run 正文机械外迁（挂点原位逐位不变）；③c 第五族、Run 尾**最大遗留**。
  形态延续 ③c-4：独立新 TU EditorAppSmokeUirml.cpp（一族一 TU）；状态收敛
  进本 TU 匿名 ns（仅 Frame/Verdict 两面访问，无需 header 共享）。

## 改动

- EditorApp.cpp：2115 → 1595 行（累计 7212 → 1595，-77.9%）。
- 新 TU `Editor/App/EditorAppSmokeUirml.cpp`（608 行）：
  - `UirmlSmokeState g_uirmlSmoke`（批③d 前置 T5 断言位 20 字段收敛；注释
    逐项随迁；`smokeUiEvText_` 是 EditorApp 成员，不在此列）；
  - 三函数：`SmokeUirmlEnterPlay`（批③a 独立进 Play + UiProbe 播种 +
    UIDocument 声明 + MountSceneUiDocuments + 翻页；**守卫
    `launchCopy_.smokeUirml && !ctx_.Playing()` 留挂点原位**，体含 2 处
    `return 1`→`false`）、`SmokeUirmlFrame`（342 行：合成点击 60/61 / 负面
    契约 / 通道 B 兜底 / 层序两拍 171/191 / 三局往返 200-430 全链 / 删除逐出 /
    僵尸三形态 / 形态 D 两段 / 热重载 100/140；守卫随体——`launchCopy_`
    成员读取原样零变换）、`SmokeUirmlVerdict`（126 行：像素四通道 + dp + C#
    API + 双通道三局聚合；尾部 `if (!uiOk || ...) exitCode = 1;` 单处改
    `return uiOk && uiOk3c && uidocOk && dpRatioOk && dpBoxOk;`）。
- EditorAppSmoke.h：+6 行（`CountPixelsNear` 声明 + `<vector>`）。
- EditorApp.h：+5 行（三私有声明）。
- Editor/CMakeLists.txt：+1 源。
- **留驻 EditorApp.cpp**：L129 看门狗、批③a 两 seed 调用（函数本就在
  EditorAppSmoke.cpp）、playPaced/悬停扫掠豁免两处共享条件、渲染段
  midUirmlCapture/wantCapture 捕获块（同 tpl capReq 待遇）。
- 连带修正：`CountPixelsNear` 定义自匿名 ns 移出（内部→外部链接，与 header
  声明合一；EditorApp.cpp 留驻的 overlay 断言与新 TU 共用）。

## 缺陷与拦截（本批真值所在）

- **行内 `return 1;` 漏变换（真 bug）**：EnterPlay 的
  `if (!ctx_.EnterPlay()) return 1;` 是行内形态，拼接脚本只按整行匹配漏掉
  ——bool 函数里 `return 1` 隐式转 **true** = EnterPlay 失败时函数返回
  成功 → 调用点不退出 → Run 继续跑（HEAD 行为 = 退出码 1）。编译器不警告
  （int→bool 无 -Wconversion）、成功路径回归测不到——**程序化逐行比对实抓**
  后修补 `return false;`。与 ③c-4 的 #ifndef 漏行同属"守卫/返回形态特例"，
  已在两处连续出现：后续批次拼接脚本必须枚举**行内 return** 与**预处理分支**
  两形态。
- 编译器拦下两处：①新 TU 帧 412 帧 `launch.script` 裸名（→`Launch().script`）；
  ②CountPixelsNear 匿名 ns 歧义（如上连带修正）。
- 复核脚本口径错误若干轮（Frame 对比范围漏守卫闭行、Verdict 只滤一侧、
  struct 闭锚点撞第二 `};`、挂点邻域偏移、smokeUiEvText_ "应留驻"断言基于
  过时假设——其读写两面均已随族外迁，成员声明保留在头文件属正当）。

## 验证

- 构建：修补轮后链接零错误零新警告。
- 回归 full：**修补前二进制 15/16**（一步 FAIL，日志 tail 截断未留步名；该
  二进制含上述 return-1 bug）→ 修补后终树三轮排查后 **16/16 全绿**。
- **三轮排查实录（T1 形态新变体，值得留档）**：
  ① 终树 14/16（smoke-drag + smoke-ui 挂；ui 分项全 1 却总判 FAIL、drag
  selΔ=19.8px 超阈——几何断言值本身在正常量级附近抖）；② 零并发重跑仍
  14/16 但**失败步漂移**（smoke-ui + anim-chain；uirml/template/final 等长
  链恒绿）；③ 两步单跑全绿。→ 定位到真凶：**前一晚泄漏的自动化编辑器
  进程**（1h47m 无人值守渲染循环，~9% CPU + 常驻 Vulkan 窗口）与回归并发
  抢 GPU/窗口资源，帧号锚定的 ImGui 注入链错帧。清杀后全量 16/16。
  教训：回归前 `pgrep -fl lemon-editor` 查残留实例（建议后续给
  editor-regression.sh 加前置守卫，另批处理）。
- 程序化 review：三函数体对 HEAD 逐行等价重建（29/342/126 行）、T5 结构体
  25 行一致、无裸 launch/exitCode/return 1 残留、挂点序
  enter@L378(守卫内) < while@L481 < frame@L576 < verdict@L1352、
  CountPixelsNear 定义在匿名 ns 外 + header 声明一致。

## 后续

- 批③c-6：final/bench 指标族（Run 尾剩余大头）+ Run 三挂点收口。
- 批④：GameUiBridge / ScriptReloadPipeline。
