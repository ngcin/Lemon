# EditorApp 批③c-3：smoke-anim 族外迁（五挂点）

- 日期：2026-09-29（批③c-2 同日续）
- 性质：Run 正文机械外迁（挂点原位逐位不变）；③c 第三族，首族**多挂点**形态
  （预循环播种 + Play 前置 + 帧链 + 帧采样 + 末帧裁决五个调用位）。

## 改动

- EditorApp.cpp：3178 → 2544 行（累计 7212 → 2544，-64.7%）。
- EditorAppSmoke.cpp：1228 → 1877 行：
  - `AnimSmokeState g_animSmoke`：三段局部收敛（链旗标组 22 行 + Vec2 注入点
    三件 + 采样组 6 行，字段/注释逐项原样）；
  - 五函数：`SmokeAnimSeed`（夹具四档 + 编辑链 roundtrip + 面板入口，151 行）、
    `SmokeAnimPlaySetup`（Play 快照断言位 + 图驱动三段切换，62 行）、
    `SmokeAnimFrame`（模态点击/选择器/帧序/创建入口/胶片带，291 行）、
    `SmokeAnimSample`（curFrame/切片区间/切段回 walk/fx，50 行）、
    `SmokeAnimVerdict`（RESULT 行，57 行）。
- EditorApp.h：+7 行（五私有声明）。
- 期间两处脚本缺陷被断言/编译器拦下后修正（零错写入）：
  ①组间夹着 uirml T5 局部（留驻）与预循环块——"两组间全收"断言否决，改三段
  精确切割；②组1 声明行间夹注释致向上走注释提前停——残留 22 行死声明由
  编译器实抓后修补进结构体。

## 验证

- 构建：两轮（修补轮后）链接干净。
- **回归 full 16/16 全绿**（anim-chain 直判：切片记账/clip 建表/帧映射/切段/
  fx/编辑链/集/状态机/multiadd/create/drag/leftcol 全位）。
- 程序化 review：五函数体对 HEAD 逐行等价重建（611 行）、旧名零残留、
  五挂点位序正确（462 < 516 < 1254 < 1355 < 1947）。

## 遗留

- 批③c-4+：smoke-template 采样族（g_tplSmoke 随迁）→ smoke-uirml（最大，
  T5 局部族已留驻待收）→ final/bench 计量族 → Run 三挂点形态收口。
