# 批③d-1：样板批——dp 坐标系 + theme token 单源 + L2 首件 + HUD/卡片两屏

- 日期：2026-09-29
- Status: **代码面收口（T1–T7 done 2026-09-29；smoke-template ×2 全绿 + smoke-uirml dp 位全绿 + 回归见验收判据 3；真人验收判据 6 余用户，不阻塞 ③d-2 开工）**
- 归属：M6b 游戏UI产品壳（总览页 [M6b.md](./M6b.md)；D2 两拆的样板半批——高风险新约定隔离在最小批，过了 ③d-2 再铺量）
- 关联：[ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D3 L2 层 / D7 范围红线 / 出口判据②）· [③d 前置](./2026-09-29-b3d-pre-uidocument-scene-mount.md)（装载双通道 = 本批消费面）· [纸面验证 ⓪](../../Reports/2026-09-28-game-ui-l1-paper-validation.md)（升级三选一卡片 = 本批验收样例底稿）· [③c C# API](./2026-09-28-b3c-csharp-ui-api.md)（M2 ops 全集已交付，本批零引擎 API 新增除 dp）
- 性质：三个新约定的首证批——**dp 坐标系**（B1 拍板）、**层序语义在真实两屏上的验证**（D1 甲-轻量）、**theme token 单源**（出口判据② "换肤 = 改单一 theme.rcss"）；交付 vs-survivor 模板六屏中的前两屏（HUD + 卡片/死亡对话）。

## 开工前摸底结论（2026-09-29，均已对码核实）

| 事实 | 位置 | 含义 |
|---|---|---|
| `SetDensityIndependentPixelRatio` 变化时 `OnDpRatioChangeRecursive` → `DirtyPropertiesWithUnits(DP_SCALABLE_LENGTH)` 全文档脏 | RmlUi `Context.cpp:158-174`、`Element.cpp:3064-3072` | **B1 的"ratio 变化是否触发重排"= 是**，resize 时 `ReloadStyleSheets()` 兜底**不需要**（落 ADR 注记候选） |
| `dp` 单位已注册（`{"dp", Unit::DP}`）、RCSS 变量声明在 `body`、用 `var(--name)` | RmlUi `PropertyParserNumber.cpp:11`、samples/basic/variables | token 语法照 sample 形态，无机制新增 |
| ③c 已交付 M2 ops 全集（SetText/SetAttr/SetClass/SetStyle/SetInnerRml/SetItems + Show/Hide） | `UiSubsystem.cpp:879+`、`GameUI.cs` | 两屏零新 op；HUD 进度条 = SetStyle width% |
| HUD/卡片现走 RtUi 兼容层（`Ui.Set`×6 每帧 + `ShowCards/CardPick/HideCards/ShowDialog`） | `Templates/vs-survivor/Game/PlayerHud.cs`、`PlayerCombat.cs:199-254,306-335` | 迁移对象；RtUi 通道本体保留（ADR-014 D5，svr-test 等继续用） |
| 模板 C# 源内嵌于生成器 raw string，资产/场景由 `GenerateVsTemplate` 产出 | `EditorApp.cpp:452+ WriteGameSources`、`:1076+` | 改模板 = 改生成器 → `--gen-vs-template` 再生成入库目录 |
| smoke-template 断言面：HUD 行读 RtUiChannel、卡片链写 `cards.pick`、`uidoc=0` 护栏 | `EditorApp.cpp:5198-5285`、`:5722+` | 本批随迁：文档探针（TryGetElementText/ContainerItemCount）+ 点击注入 + `uidoc=2` |
| bench-survivor/bench-scene 用自播种/用户场景，与模板场景无关 | `EditorApp.cpp:3526-3543` | 零装载基准护栏不受影响；模板场景无金回放档（sim 三档在 bench-sim），**零重录** |
| 键盘喂 RmlUi 不看 modal（`gameViewFocused_` 即喂）；游戏侧输入在 `WantsKeyboard()||AnyModalShown()` 让出 | `EditorApp.cpp:4022-4025,6107` | 卡片 modal:true 只影响游戏侧让出（模态语义正确），点击通道不受影响 |
| C# Input = 按钮位语义（Axis/GetButton），无数字键 | `Lemon.SDK/Input.cs` | 数字键选择通道退役（见设计定案 5） |

## 设计定案（实现前冻结）

### 1. dp 坐标系（B1 落地形态）

- `UiSubsystem::SetDpReferenceHeight(uint32_t refH)`（0 = 不缩放，ratio 恒 1——既有 px 文档零影响，默认态）；`Render()` 内每帧 `ratio = rtH / refH`（refH>0），与缓存值比较**变化才**喂 `ctx->SetDensityIndependentPixelRatio`。探针：`DpReferenceHeight()` / `DpRatio()`（冒烟断言源）。
- **参考高 = 720**（L2 皮设计基准：720dp 高画布）：EditorApp gameUi_ Init 段设定。项目级覆写登记 ③d-2（设置屏/工程档）；引擎不猜。
- 换算示例：烟测画布 464px 高 → ratio 0.644 → 20dp HUD 字 ≈ 12.9px（视觉对齐 RtUi 版 ImGui ~13px）；1080p 全屏 → 30px。token 值以 720dp 画布为基准设计。
- 只影响 `dp` 单位属性（RmlUi 语义）；px 值不动——smoke-uirml 既有像素断言天然免疫。

### 2. theme.rcss token 单源 + L2 组件首件（纯资产，引擎零改动）

- `Assets/UI/theme.rcss`（模板项目内资产；换肤 = 改它的 token 区）：
  - **token 区**（`body` 上声明）：色板（`--c-bg/--c-panel/--c-text/--c-text-dim/--c-accent/--c-hp/--c-xp/--c-time/--c-kill/--c-wave/--c-scrim`）+ 品级色（`--rarity-common/rare/epic`——③e 图鉴消费，先落 token 不落消费者）+ 字号（`--fs-title/--fs-hud/--fs-body/--fs-hint`，**全 dp**）+ 间距（`--sp-1/--sp-2/--sp-3`，dp）。
  - **组件首件套**（两屏所需最小集）：`.panel`（面板底）、`.scrim`（全屏模态暗罩）、`.title/.hint`、`.hud-row`（HUD 行）、`.bar/.bar-fill`（进度条）、`.card`（卡片按钮：hover/active 态 + `{{rarity}}` 品级色 class 挂钩）、`.btn`（对话框按钮）。全部引 `var(--token)`。
- 尺寸/字号/偏移**全 dp**；布局用 flex/百分比（出口判据② 口径）。
- 品级色 class（`.card.rare` 等）随首件落地但模板升级池无 rarity 列（v1 卡片只有 label 字段）——**icon/rarity 字段消费归 ③e**（图鉴有贴图与品级数据）。

### 3. HUD 屏（hud.rml + PlayerHud 改写）

- 文档：六行（hp/xp 带 `--c-hp/--c-xp` 填充条 + time/kills/best/wave 纯文本），top-left dp 偏移；`<link>` 引 theme.rcss。
- PlayerHud：`Ui.Set(...)`×6/帧 → `UI.SetText(hud, id, text)`×6 + `UI.SetStyle(hud, "hpfill"/"xpfill", "width", pct)`×2 + `UI.Apply()`（≈8 op/帧 ≈ 0.5KB arena，预算内）；WaveStart 事件回调同改（SetText + Apply）。
- 场景实体 `UI_HUD`：`UIDocument{sourceAssetGuid=hud.rml, showOnStart=1}`（通道 A）。
- 视觉语义对齐 RtUi 版（同六行/同色语义）；死亡相位 HUD 冻结末帧语义不变（Dead 早退）。

### 4. 卡片屏（cards.rml + PlayerCombat 改写）

- 文档：`.scrim` 全屏（半透明暗罩——模态标准形态，兼层序断言观察窗）+ 居中 `.panel`：`#title` + `#cards` 容器（`data-template="card"`，`<ui-template data-name="card"><button class="card" data-event="pick"><span data-field="label"/></button></ui-template>`）+ `.hint`。死亡对话 = 同文档 SetItems 单条（纸面验证 ⓪ 单条形态）。
- PlayerCombat：`ShowCards/ShowDialog` → `UI.Show(cards, modal:true)` + `UI.SetText(title)` + `UI.SetItems(cards, card, items)` + `UI.Apply()`；`HideCards` → `UI.Hide(cards)`；`CardPick()` 轮询 → **UI.Events 订阅**（`GameMain.Configure` 静态订阅一次，跨局存活先例 = TestScript UiProbe）：Click(ev="pick") → 静态待选 key → `PlayerCombat.Update` 消费（事件天然一次 = 消费式回读语义保持）。
- modal:true（ADR-014 纸面验证 ⓪ 同款）：M7 游戏侧让出 + `AnyModalShown` 在模板中活证；模拟已 `Time.Scale=0` 冻结，行为与 RtUi 版等价。
- `DocumentReloaded` → GameMain 按"最近一次卡片态"（title/items/shown 静态记录）重放重灌（shown 才重放——隐藏态重放会凭空亮屏）；`PlayerCombat.Start` 复位 shown 记录（跨局归位对齐，EnterPlay 大扫除清 stale 同拍）。
- 场景实体 `UI_Cards`：`UIDocument{sourceAssetGuid=cards.rml, showOnStart=0}`（装载免 IO，显隐归 C#）。

### 5. 数字键选择通道退役（RtUi 版遗留 UX）

- 依据：①Input 是按钮位语义且游戏输入入输入快照（回放纪律），数字键属 UI 交互用户 IO **不入快照**（09 §7），经游戏输入通道接数字键 = 破纪律；②RmlUi 键盘焦点导航（方向键+Enter）原生可用，v1 不接线；③点击通道完备。hint 文案改「点击卡片选择」。M7 手柄/键盘焦点导航接线 = 登记项（Steam 目标确认后）。

### 6. GUID 段与资产落位

- 新 UI 资产 GUID（生成期固定，7e5730 段区别 png/prefab/table 段）：`kHudRml=0x7e57300000100001`、`kCardsRml=0x7e57300000100002`、`kThemeRcss=0x7e57300000100003`。
- 落位 `Assets/UI/`（hud.rml / cards.rml / theme.rcss + .meta；AssetType::Rml/Rcss，③b 已转正）。

## 任务分解

### T1 dp 坐标系（Engine/Ui + EditorApp）

- `UiSubsystem.h/.cpp`：`SetDpReferenceHeight/DpReferenceHeight/DpRatio` + `Render()` ratio 喂入（Impl 增 `dpRefH/dpRatio` 缓存）。
- `EditorApp.cpp` gameUi_ Init 段（`:3034` resolver 装配旁）：`SetDpReferenceHeight(720)`。
- 冒烟探针 `TryGetElementBox(doc, id, &w, &h)`（GetBox Border 尺寸——TryGetItemCenter 同款 DOM 读数，dp 断言源）。

### T2 模板资产三件 + 生成器（EditorApp.cpp vs_template 段）

- GUID 常量三枚 + `WriteUiAssets(root/Assets)`：theme.rcss（token 区 + 首件套）/ hud.rml / cards.rml + 三 .meta。
- `GenerateVsTemplate`：场景两实体（`UI_HUD` showOnStart=1 / `UI_Cards` showOnStart=0，Emplace<ecs::UIDocument>）+ README 增补 UI 段（六屏进度 + 换肤说明）。
- `--gen-vs-template` 再生成 `Templates/vs-survivor`（入库目录同步）。

### T3 C# 改写（WriteGameSources 内嵌源）

- GameMain：Configure 增 `UI.Events.Subscribe(OnUiEvent)`（Click pick 待选 key 静态槽 + DocumentReloaded 卡片态重放）+ 卡片态记录（title/items/shown）。
- PlayerHud：全量改写为 UI.SetText/SetStyle + Apply（六行 + 双条）。
- PlayerCombat：UpdateCards/Die/Revive 改文档通道（Show/Hide/SetItems/待选 key 消费）；`Ui.ShowCards/ShowDialog/HideCards/CardPick/Ui.Set("over")` 调用全数退役。

### T4 smoke-template 断言随迁（EditorApp.cpp 5198+/5722+）

- `g_tplUiDocZero` → `g_tplUiLoads`：EnterPlay 后 `DocumentLoadCount()==2`（HUD+cards 通道 A；装载点单一性护栏升级为**恰 2**）。
- HUD：`TryGetElementText(hud, "hp"/"xp"/"time"/"kills")` 非空 + `best` 含 "123"（存档回显语义随迁）+ WaveStart 后 `wave` 非空。
- 卡片链：升级时 `ContainerItemCount(cards,"cards")==3` → **点击注入**（TryGetItemCenter + SetPointer + ProcessMouseButton down/up——smoke-uirml 同款）→ 升级应用（LevelUp 后待选清）→ `IsDocumentShown(cards)==false`；死亡对话框单条（==1）→ 点击复活链同型。
- **层序断言**：卡片显示期 gameRT HUD 文字区像素被 scrim 压暗（cards 前 N0 / 中 N1≪N0 / 后 N2≈N0 三拍采集）——cards 文档在 HUD 之上的像素级证明（scrim 属 cards 文档；若层序颠倒 scrim 压不住 HUD 文字）。
- verdict 行升级：`cards(doc=1/1/…) uidoc=2`；RtUi 断言位退役（通道本体保留，svr-test 消费）。

### T5 smoke-uirml 扩 dp 断言（EditorApp.cpp 夹具 + 主循环）

- 夹具 uirml.rcss 加 `#dpbox{width:100dp;height:50dp;…}`（角位避让既有像素断言区）、uirml.rml 加 `<div id="dpbox"/>`。
- 断言：`DpRatio()==rtH/720`（浮点容差）+ `TryGetElementBox(dpbox)` ≈ (100×ratio, 50×ratio)±0.5——dp 端到端机器证明；既有 px 断言不动（ratio 只缩 dp）。

### T6 验证

- `--gen-vs-template` 再生成 + 模板目录 diff 核对（预期：+3 资产 ×2 文件、场景 +2 实体、Game/ 三文件改）。
- smoke-template 全绿（含新断言链）；smoke-uirml 脚本/无脚本双模式全绿（回归 full 覆盖）。
- 回归 `tools/editor-regression.sh full` 15/15（首跑负载抖动复跑判读 = T1 先例）。
- bench/replay 口径核：零改动预期（bench 场景零 UIDocument；UI ops 不入哈希；金回放三档在 bench-sim 与模板无关）。

### T7 文档落账

- DevLog 收口条目；本批文件勾销；M6b.md 批次表 ③d-1 行 + Status；AGENTS.md 批③段同步；ADR-014 B1 实证注记（ratio 变化原生触发全文档重排，兜底不需要——落 M6b.md 决策注记处即可，不动 ADR 主体）。

### T8 真人验收（余用户，不阻塞勾销）

- 模板 Play 视觉走查：HUD 六行/血条经条、升级三选一（scrim+卡片+点击）、死亡对话框、层序观感；换肤演示（改 theme.rcss 一个 token 重跑 = 出口判据② 现场形态）。

## 验收判据（全过才勾销）

1. ✅ smoke-template 全绿：`hud(doc)=YES saveLoad=YES wave=YES cards(doc seen/pick/hidden)=YES layer(86/0/89=OK) death(seen/revive)=YES tables=YES uidoc=2`（exit 0；两轮复跑数值一致 = 确定性）。
2. ✅ smoke-uirml 双模式全绿含 dp 位：`dp(ratio=0.644 box=64.4x32.2/OK)`——dp 端到端机器证明（100dp×ratio）；无脚本模式补跑 `=> OK`（items/ev 位为 n/a 口径）。
3. ✅ 回归 full 15/15（2026-09-29 首跑全绿，零抖动）。
4. ✅ bench/replay 零改动口径成立（装载点只在 EnterPlay 扫描；模板场景装载恰 2；bench 自播种场景零 UIDocument）。
5. ✅ 模板再生成 diff 干净（+Assets/UI/ 三件 ×2 文件、场景 +2 实体、Game/ 三文件；prefab/meta guid 时间戳噪声为历次再生成固有）。
6. ⏳ 真人验收（T8，余用户）。

## 实现期发现（偏离批文件预设计的落账）

1. **B1 兜底问题机器答案**：RmlUi `SetDensityIndependentPixelRatio` 变化原生触发
   全文档 dp 属性重排（`OnDpRatioChangeRecursive`，源码核）——"resize 时
   ReloadStyleSheets() 兜底"**不需要**。
2. **RmlUi 全屏覆盖元坑**：静态 body 下 absolute/fixed 子元素**百分比尺寸解析到
   RootBox（非盒）= 零包含块**（scrim 实测 0×0、flex 居中甩出画布）；且 right/bottom
   对向偏移不做尺寸推导（UpdateOffset 单边锚定）。修 = body 画布约定
   `position:relative; width:100%; height:720dp`（720dp×比率 ≡ 画布全高——坐标系
   自洽的全屏表达），scrim 百分比吃 body 盒。L2 皮后续全屏元照此。
3. **合成点击两连坑**：①悬停扫掠逐帧盖注入位（uirml 60/61 让位同款、时点动态）；
   ②让位后 mouse 落位正确而 GameView `IsItemHovered` 恒假（仅 template 会话复现，
   根因未明）。落点 = **引擎直灌点击**（SetPointer + 指针保持窗 + 两帧 down/up，
   绕 ImGui 悬停链）；ImGui→RmlUi 路由链由 smoke-uirml 60/61 帧点击独立覆盖。
4. **死亡链催命**：文档化卡片事件往返使局内时序后移、波 2 走近余量薄（一轮贴边
   一轮超时）→ 武装 50 帧未死则追击怪贴脸传送（保 Hazard 真实路径）。
5. **画布尺寸跨会话可变**（824×464 / 1308×736 均现）——层序采样区改 DpRatio 派生，
   不写死 px。
6. **T8 后修（2026-09-29 真人验收反馈）**：HP/XP 进度条填充恒空——`.bar-fill`
   无 `display` 规则，RmlUi `display` 默认值 = **inline**（StyleSheetSpecification
   注册默认，与 CSS 用户直觉相反；RmlUi 官方样例 rml.rcss 全量显式
   `div{display:block}` 即此因），inline 元素 width/height 被布局忽略 → SetStyle
   写入成功但盒子恒 0。修 = L2 组件全显式 display（`.bar-fill/.panel/.title/.hint`
   补 `display:block`，`.title/.hint` 顺带修 inline 上 text-align 无效的潜伏
   居中错位）。**烟测盲区教训**：既有 `fx(text/bar)` 位是场景 FxChannel 通道
   （M6a 批①），非 RmlUi HUD；文本探针（TryGetElementText）测不出"样式写入
   但布局未生效"。补 `hud(bar=)` 位：TryGetElementBox 读回 fill 宽对分数 ×
   120dp × ratio（±3px），**阴性验证过**（回退 display → `bar=NO => FAIL` 而
   文本位仍 YES = 盲区实证）。[DevLog](../DevLog/2026-09-29-m6b-b3d1-t8-bar-fill-fix.md)
7. **T8 后修②（用户提案）**：L2 进度条改**原生 `<progress>`**（value/max 走
   SetAttr 属性通道、fill 引擎定位非 DOM 子元素 = 布局坑结构性免疫、白得
   direction 表盘/fill-image；value 变更仅 geometry_dirty 比每帧 SetStyle 宽
   更便宜）。`Pct()` 退役；UiSubsystem 加 `TryGetElementAttrF` 探针；smoke
   `hud(bar=)` 断言换轨 = 轨道盒 + value 属性回读对文本行（阴性验证过——
   注意阴必须连模板再生成，smoke 复制源是模板目录非内嵌串）。
   [DevLog](../DevLog/2026-09-29-m6b-b3d1-t8-native-progress.md)

## 风险与既知边界

| # | 坑 | 处置 |
|---|---|---|
| R1 | ratio 每帧浮点抖动（rtH 稳定时无抖；窗口拖拽逐帧变 = 每帧重排属预期成本） | 变化才喂（缓存比较）；无 rAF 节流需求（编辑器 GameView 拖拽低频） |
| R2 | scrim 压暗后既有 smoke-template 像素断言（玩家环等）时序重叠 | 层序三拍采集窗口与玩家环断言帧错开；实现期对表 |
| R3 | 点击注入与 RmlUi 悬停/焦点状态（GameView 未聚焦时 pointer 仍可喂） | smoke-uirml 先例：SetPointer/ProcessMouseButton 直灌 gameUi_，不依赖 ImGui 焦点 |
| R4 | GameMain 静态订阅跨局累积（Configure 每域一次 ≠ 每局一次） | 域内 Configure 一次 + PlayReset 清计数（③c 同款）；卡片态 shown 由 PlayerCombat.Start 复位 |
| R5 | 卡片事件一帧延迟（#16 头派发）vs CardPick 同帧轮询 | 升级/复活逻辑容忍 1 帧（Time.Scale=0 冻结态无竞态） |
| R6 | theme.rcss 热重载路径（.rcss → ReloadStyleSheets 保 DOM） | ③b 已交付；真人验收 T8 演示项 |

## 关联

- 下一批：③d-2 铺量批（余四屏 + 档1 流程状态机 + smoke 随迁收尾 + svr-test 全流程接通）——本批两屏即其样板。
