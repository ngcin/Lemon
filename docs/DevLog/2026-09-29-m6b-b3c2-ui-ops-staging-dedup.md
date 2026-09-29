# M6b 批③c-2：UI ops staging 同值去重（T8 后续卫生批）

- 日期：2026-09-29（T8 后修双联之后同日第三批）
- 性质：SDK 通道行为增强（GameUI.cs staging 层），引擎/UI 呈现零改动
- 动机：PlayerHud 每帧 10 op 稳态 ~95% 冗余 + RmlUi SetInnerRML/SetAttribute
  无等值早退（源码双证）——把主流 retained GUI 的 setter 级 `if(same) return`
  （UGUI Text / Godot Label / cocos setString 同位标配）补到 SDK 层。

## 改动

- **GameUI.cs**：`s_lastSent`（键 = `类型前缀|doc|key|限定名`，值 = 载荷原始串）
  + `DedupSkip()`；五个幂等 setter（SetText/SetAttr/SetClass/SetStyle/
  SetInnerRml）接入。**Show/Hide/SetItems 永不去重**——Show 重复 = D1 层序
  提顶语义，SetItems 全量替换非幂等（ReplayCards 重灌依赖）。
- **失效五处**（漏一处 = 静默丢写）：Reset / PlayReset / PullOps 两丢弃路径
  （-1 超容 + 无钩子弃置）/ DiscardPending / DispatchEvents 见
  DocumentReloaded 整表清（清在 handler 前——OnUiEvent → Refill 序列通过）。
- **TestScript.cs**：`UiDedupProbe()`（帧 2，靶 = body 行——夹具静态行两侧
  冒烟无内容断言）+ UiProbeBehaviour 帧 2 分支。
- **tests/script TestUiSdk** 扩两段：③ 帧 2 三写（A/同A/B）恰 2 op；④ 注入
  DocumentReloaded → 同值重灌恰 6 op 全发（复位契约）。

## 验证

- script-tests 全绿（① 6-op 字节对拍零变化 + ③ 去重 + ④ 复位）；ctest 3/3。
- **真阴性**：注释 DispatchEvents 清缓存 → ④ 红（重灌塌缩 6→3 op——两条同值
  SetText 被吞，实抓）→ 复原复绿。
- smoke-uirml 双模式 `=> OK`（scripted `ev=c1r4 contract=1/textOK`）；
  smoke-template 全绿**数值逐位一致**（kills=177 等——去重零行为漂移）；
  回归 full 16/16。

## 实现期发现（重要，防止复踩）

1. **预设计的 smoke-uirml 终帧 title 阴性判断错误**：结尾三局 Play 循环每局
   PlayReset 清缓存重灌，终帧 title 是最后一局帧 1 写的——中途重装载被吞
   终帧仍绿（假阴性实证）。真阴性 = script-tests 事件注入（④）。编辑器侧
   中途 title 内容断言留 ③d-2 再议。
2. **事件驱动 UI 写下一帧可见**（#16 派发插 CSharpBatch 后，晚于当轮拉取点）
   ——既有行为非本批引入；④ 用两步步进捕获。
3. **阴验证构建目标坑**：dotnet 侧（SDK + TestScript.dll）只随 `lemon-dotnet-asm`
   目标重编，`--target TestScript` 不存在——首轮假阴性即旧二进制照跑。

## 关联

- [批文件](../Plans/M6b/2026-09-29-b3c2-ui-ops-staging-dedup.md)
- 前因：[T8 后修双联](./2026-09-29-m6b-b3d1-t8-native-progress.md)期间的性能问询
