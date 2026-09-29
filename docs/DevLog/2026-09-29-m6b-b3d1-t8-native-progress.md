# M6b 批③d-1 T8 后修②：L2 进度条改原生 <progress>（用户提案采纳）

- 日期：2026-09-29（T8 后修① display 修复同日，接续）
- 性质：L2 组件定义变更（bar 组件 = RmlUi 原生 progress 元素），机制 = 既有
  SetAttr 属性通道，引擎仅加一个冒烟探针
- 触发：用户贴 RmlUi 官方 `<progress>` 文档（ElementProgress：value/max 属性 +
  引擎定位 fill 子元素 + direction/fill-image 表盘能力），建议采用。

## 动机（相对手搓 div+width% 的收益）

1. **语义通道**：value/max 是数据属性不是样式——C# `UI.SetAttr` 直写原值，
   不再做百分比数学（Pct() 退役）；文本行与条同源同舍入。
2. **免疫布局坑**：fill 是引擎在 OnResize 里 SetBox 的非 DOM 子元素，
   display:inline 一类"样式写入但布局静默失效"（T8 后修①的根因）结构性
   不存在。
3. **白得能力**：direction（top/right/bottom/left）竖条 + clockwise/
   counter-clockwise 表盘 + fill-image 裁剪——③e 图鉴/头顶条/CD 圆环可复用。
4. **成本更低**：value/max 变更只置 geometry_dirty（fill 几何重建一个小
   quad），不脏布局——比每帧 SetStyle width（脏行布局）更便宜。

## 预检（对源码核实后才动手）

- `<progress>` 在 Core/Elements 且 Factory 注册（`Factory.cpp` "progress"
  instancer）——零构建改动、零新三方。
- 我们的 ApplyOps SetAttr → `SetAttribute(name, String)` → ElementProgress
  `GetAttribute<float>` 字符串转 float（RML 属性即此路径）——通路现成。
- 轨道默认 display 同为 inline——`.bar` 类显式 `display: inline-block`
  （③d-1 已有），fill 不受影响（引擎定位）。

## 改动

- `EditorApp.cpp` 内嵌 theme.rcss：`.bar-fill` 三条规则退役，改
  `progress.bar` 轨道 + `.bar.hp fill`/`.bar.xp fill` 着色（tag 选择器可
  寻址非 DOM fill 子元素）。
- 内嵌 hud.rml：`<div class="bar"><div class="bar-fill hp"/></div>` →
  `<progress id="hp-bar" class="bar hp" max="100"/>`（xp 同，max="60" 初始值）。
- 内嵌 PlayerHud.cs：`Bar(doc,id,cur,max)` 辅助（value/max 两 SetAttr；
  (int) 舍入与文本行同口径——"0" 格式四舍五入会与文本差 1）；`Pct()` 退役。
- `UiSubsystem`：加 `TryGetElementAttrF` 探针（NaN 哨兵区分属性缺席；供
  冒烟属性回读）。
- smoke-template `hud(bar=)` 断言换轨双证：① 轨道盒 = 120dp×10dp×ratio
  （布局在场）② value 属性回读 = 文本行同帧数值（C# 接线落地）。fill 盒
  不可达（非 DOM），fill 渲染归 RmlUi 自身行为（与 scrim 背景同举证层级）。

## 验证

- 阴性验证（断言有效性）：C# 改写错元素 id（hp-bar-x）+ 模板再生成 →
  `hud(doc=YES bar=NO) ... => FAIL`（文本位仍绿——盲区口径与前修一致）→
  复原复绿。**注意坑**：阴性必须连模板再生成一起做——smoke 复制源是
  `Templates/vs-survivor` 而非内嵌串，只改 EditorApp.cpp 不再生成时阴性
  不到位（首跑假阴性实证）。
- smoke-template ×2 全绿数值一致（restored 终跑）：
  `hud(doc=YES bar=YES) ... kills=177 ... layer(86/0/89=OK) ... uidoc=2 => OK`
- 回归 full 16/16。

## 纪律落点（③d-2 四屏起生效）

- L2 进度条一律原生 `<progress>`；禁止手搓 fill div + width%。
- 动态数值走属性（SetAttr），静态视觉走 RCSS——与 D7"机制之外的新 UI 需求
  默认答案 = 资产"同向。

## 关联

- 前修：[2026-09-29-m6b-b3d1-t8-bar-fill-fix.md](./2026-09-29-m6b-b3d1-t8-bar-fill-fix.md)
- 批文件：[docs/Plans/M6b/2026-09-29-b3d1-sample-screens.md](../Plans/M6b/2026-09-29-b3d1-sample-screens.md)（发现 7）
