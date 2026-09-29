# M6b 批③d-1 T8 后修：HUD 进度条填充恒空（RmlUi 默认 display:inline）

- 日期：2026-09-29（③d-1 代码面收口同日，T8 真人验收首轮反馈）
- 性质：缺陷修复（模板资产 + 烟测断言盲区），引擎零改动
- 触发：用户模板 Play 走查截图——HP 61/100、XP 22/60 而双条仅见底槽不见填充。

## 现象与根因

- 现象：HUD 六行文本全部正常更新，血/经双进度条只有半透明底槽，填充任何分数下
  都不出现。
- 根因：theme.rcss 的 `.bar-fill` 没写 `display` 规则。RmlUi 的 `display` 属性
  **默认值 = inline**（`StyleSheetSpecification.cpp` 注册默认；官方样例的
  rml.rcss 全量 `div{display:block}` 即为规避此点），inline 元素的 width/height
  被布局忽略——`UI.SetStyle("hp-fill","width","61%")` 写入成功、读回也成功，
  但盒子恒 0×0。`.title/.hint` 同源潜伏问题（inline 上 `text-align:center`
  无效，标题/提示实为左对齐）。
- 烟测为何没拦住：smoke-template 的 `fx(text/bar)` 位断言的是场景侧 FxChannel
  （M6a 批① 飘字/世界血条通道），不是 RmlUi HUD；HUD 文本探针
  （TryGetElementText）只能证明文本链通，测不出"样式写入但布局未生效"。

## 修复

1. `EditorApp.cpp` 内嵌 theme.rcss（vs_template 段，模板再生成真源）：
   `.bar-fill` 补 `display:block`（附教训注记）；`.panel/.title/.hint` 同步
   显式化（panel 此前靠 flex 子项块化隐性成立，显式声明意图）。
2. `--gen-vs-template Templates/vs-survivor` 再生成（场景 GUID / meta 时间戳
   噪声为历次再生成固有）。
3. smoke-template 补 `hud(bar=)` 断言位：`TryGetElementBox` 读回 hp-fill/xp-fill
   盒子，宽 = 文本分数 × 120dp × DpRatio（±3px 容差吸收整数舍入）+ 高 > 0；
   并入 tplOk 判据。

## 验证

- 阴性验证（断言有效性）：临时回退模板 `.bar-fill` 的 display →
  `hud(doc=YES bar=NO) ... => FAIL`（文本位仍 YES = 盲区实证）→ 复原。
- smoke-template ×2 全绿数值一致：
  `hud(doc=YES bar=YES) saveLoad=YES wave(row=YES n=2) kills=177 levelUps=1
  cards(doc seen=YES pick=YES hidden=YES) layer(86/0/89=OK) death(seen=YES
  revive=YES scriptOk=YES) hitClip=YES fx(text=YES bar=YES) tables=YES uidoc=2
  => OK`
- 回归 full 16/16 首跑过。

## 教训（并入 L2 皮纪律）

- **凡参与布局/对齐的元素必须显式 display**——RmlUi 默认 inline 与 Web 直觉
  相反，属性静默失效（写入/读回全成功，盒子 0）。③d-2 四屏与 ③e 组件照此。
- **布局类状态必须用盒子断言**（TryGetElementBox），文本/样式回读不构成
  "视觉生效"证明。

## 关联

- 批文件：[docs/Plans/M6b/2026-09-29-b3d1-sample-screens.md](../Plans/M6b/2026-09-29-b3d1-sample-screens.md)（发现 6）
- 用户侧生效方式：重跑新建项目向导（复制新模板），或既有项目手改
  `Assets/UI/theme.rcss` 的 `.bar-fill` 一行。
