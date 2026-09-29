# 2026-09-29 · M6b 批③d-1 样板批——dp 坐标系 + theme token 单源 + HUD/卡片两屏

## 概要

③d 前置（UIDocument 场景挂载）真人验收编辑器侧过审后开工。本批交付三个新约定的
首证与模板前两屏：**dp 坐标系**（B1）、**theme.rcss token 单源**（出口判据②）、
**层序语义在真实两屏验证**（D1 甲-轻量）；vs-survivor HUD + 卡片（升级三选一/死亡
对话单条形态）全走 `.rml` 文档 + UIDocument 挂载，RtUi 通道在模板退役（兼容层本体
保留，svr-test 等继续消费，ADR-014 D5）。

## 交付面

- **引擎（Engine/Ui）**：`SetDpReferenceHeight/DpReferenceHeight/DpRatio` +
  `Render()` 内 ratio 喂入（变化才喂）+ 冒烟探针 `TryGetElementBox`。EditorApp 装配
  参考高 720（L2 皮设计基准；项目级覆写登记 ③d-2）。
- **模板资产三件**（`Assets/UI/`，7e5730 段 GUID）：`theme.rcss`（token 区全 dp +
  L2 首件套 .panel/.scrim/.title/.hint/.hud-row/.bar/.card）+ `hud.rml`（六行 + 血/
  经双条）+ `cards.rml`（scrim + 居中面板 + data-template 克隆）。场景实体 `UI_HUD`
  （showOnStart=1）/`UI_Cards`（showOnStart=0）。
- **C# 改写**（WriteGameSources 内嵌源随生成器）：PlayerHud 全量转 UI.SetText/
  SetStyle + Apply（进度条 = width 百分比）；PlayerCombat 三选一/死亡对话转
  GameMain.ShowCardsDoc（UI.Show modal + SetItems），选择 = UI.Events 静态订阅
  （Configure 一次，③c 先例）消费式回读；数字键通道退役（Input 按钮位语义 + UI
  交互不入输入快照纪律——批文件设计定案 5）。
- **smoke-template 断言随迁**：HUD 探针 RtUiChannel → TryGetElementText；卡片链
  cards.pick 直写 → 合成点击；`uidoc=0` 护栏 → `uidoc=2`（通道 A 装载恰 2——bench
  场景零装载口径不变）；新增层序三拍像素断言。

## 实现期发现（偏离预设计的落账）

1. **B1 兜底问题机器答案**：RmlUi `SetDensityIndependentPixelRatio` 变化时
   `OnDpRatioChangeRecursive` → `DirtyPropertiesWithUnits(DP_SCALABLE_LENGTH)`
   原生触发全文档 dp 属性重排（`Context.cpp:158`/`Element.cpp:3064` 源码核）——
   M6b.md B1 预设的"resize 时 ReloadStyleSheets() 兜底"**不需要**。
2. **RmlUi 全屏覆盖元坑**：静态 body 下的 absolute/fixed 子元素**百分比尺寸解析到
   RootBox（非盒容器）= 零包含块**——scrim 实测 0×0、flex 居中把面板甩出画布
   （卡片中心 y=-87）。且 `right/bottom` 对向偏移不参与尺寸推导（`Element::
   UpdateOffset` 只做单边锚定）。修 = theme.rcss body 画布约定：`position: relative;
   width: 100%; height: 720dp`（720dp × dp 比率恒等于画布全高——坐标系自洽的
   全屏表达），scrim 以百分比吃 body 盒。
3. **合成点击两连坑（smoke-template）**：①冒烟悬停扫掠逐帧重设鼠标位，盖掉注入
   位置（smoke-uirml 当年 60/61 帧让位的同款问题，但本批时点动态）；②让位后
   mouse 落位正确而 GameView `IsItemHovered` 仍恒假（根因未明，仅 template 会话
   复现）。落点 = **引擎直灌**：SetPointer + 指针保持窗（FeedGameUiInput 让位）+
   两帧 down/up，绕 ImGui 悬停链——ImGui→RmlUi 路由链由 smoke-uirml 的 60/61 帧
   点击独立覆盖，职责不丢。
4. **死亡链时序余量**：文档化卡片的选择有几帧事件往返（RtUi 直写同帧），局内时序
   后移 → 波 2 走近余量变薄（实测一轮贴边过、一轮超时）。修 = 武装后 50 帧未死则
   追击怪贴脸传送（保 Hazard 接触真实路径，只省走路）；两轮复跑结果逐字节一致
   （kills=177 levelUps=1 层序 86/0/89）。
5. **画布尺寸跨会话可变**（824×464 与 1308×736 均实测出现）——层序断言采样区由
   写死 px 改为 DpRatio 派生（左上 242×120dp 窗口计 #f0f0f0 近色）。

## 验证

- smoke-template ×2 连跑全绿：`hud(doc)=YES saveLoad=YES wave=YES cards(doc
  seen/pick/hidden)=YES layer(86/0/89=OK) death(seen/revive)=YES tables=YES uidoc=2
  => OK`（两轮数值一致）。
- smoke-uirml 双模式全绿含新 dp 位：`dp(ratio=0.644 box=64.4x32.2/OK)`——
  100dp×0.644=64.4 端到端机器证明；既有 px 断言零回归。
- **回归 full 15/15 首跑全绿**（零抖动）。
- bench/replay 口径：零改动（bench 自播种场景零 UIDocument；装载点只在
  MountSceneUiDocuments；UI ops 不入哈希；金回放三档在 bench-sim 与模板无关）。

## 真人验收（余用户）

模板 Play 视觉走查（HUD 六行/双条/升级三选一/scrim/死亡对话框/层序观感）+ 换肤
演示（改 theme.rcss 单 token 重跑）。

## 关联

- [批文件](../Plans/M6b/2026-09-29-b3d1-sample-screens.md) · [M6b.md](../Plans/M6b/M6b.md)
- [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D3 L2 / D7）
