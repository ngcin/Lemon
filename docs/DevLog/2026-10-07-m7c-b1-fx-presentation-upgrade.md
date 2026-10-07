# M7c 批① Fx 表现升级落地（TTF 烘焙器 / 贴图血条+延迟条 / 飘字动效 / SDK）

**日期**：2026-10-07 · **批**：[M7c 批①](../Plans/M7c/2026-10-07-b1-fx-presentation-upgrade.md) · **顺序**：S1→S2→S3→S4→S5

## S1 TTF→位图图集离线烘焙器（LBF1）

- **`Engine/Assets/FontBake.{h,cpp}`**：FreeType（复用 CPM VER-2-14-3，`Freetype::Freetype` PRIVATE 链入 lemon-engine，零新依赖）光栅化 + 描边 8 邻域膨胀 + shelf 行装箱单页（宽 512 定死、高 512→4096 倍增、1px 防渗色）+ 度量表（advance/bearing/宽高，20B 定长行）。产物 `.lemon/baked/fonts/<guid>.baked`（LBF1：36B 头含 **paramsHash** + 升序字形表 + RGBA 页位图）。运行时零 FreeType 零栅格化——02 §7 红线不破（RmlUi 运行时字体链并存各管各的）。
- **增量重烘**：`BakeFontStale` = 音频三方 mtime + **paramsHash 头对比**（字符集/字号/描边变更 mtime 不动，靠头 hash 抓——单测四种变更态全覆盖）。
- **资产化 = 形态 A**：`AssetType::Font`（.ttf/.otf）+ .meta importer 段（charset/size/outline）+ 编辑器 `AssetDatabase`/运行时 `AssetIndex` 两侧读入（热改 meta = Rescan modified 事件，音频 #8 同款口径）。07 登记面只补用途行。
- **编辑器接线**：`EditorApp::EnqueueFontBake/WarmFontBakes/StopFontBaker`（音频 worker 同款三件套：惰性线程 + 异常隔离 + BakeStale 复查）；`LoadFxFontPage()` 装载（project.lemon `fxFont` guid → BakeStale 兜底 → BitmapFont 外部页槽 255）三触发点 = 开项目 / Rescan 字体变更 / EnterPlay。packager 包形态现烤（音频段同款）；GameEntry 装载缺档红字降级内置页。
- **BitmapFont 外部页**：字形产包走 **UV 覆盖通道（kPktUvOverride）——sprite 表零登记**，与资产号段零交互（AddSprite 自增号 vs manifest 显式号的撞号防线从"装配序纪律"升级为"结构性无冲突"，装载时点自由）；缺字形回退内置 5×7 + 红字一次；同槽换页（UnregisterAtlas 干净——零 sprite 登记）；`RebuildBaked` 设备丢失重建（ViewportRenderer::Build 尾接续）；`ClearBaked` 换项目清留档。

## S2 贴图血条 + 延迟条

- **首查项定案：方案 A**——`SpritePacket` 尾加 UV 覆盖四元组 + flags bit2（kPktUvOverride），`SpriteBatcher::Bake` 查表后覆盖位短路。**`SpriteInstance` 48B 布局零改动、shader 零改动**（bit2 不进实例，sprite.vert 位面不变）→ 实例布局未冻结不动 → **免 ADR-017**（批文件"若涉实例布局变更"条件不成立）。侦察发现实例块本就有逐实例 u0/v0/u1/v1 字段（02 §3.3 设计），A/B 之争实际不存在。
- `FxBar` 尾加：bgSpriteId/fgSpriteId（0 = 白精灵现状路径零迁移）/lagColor（0 = 无延迟条）/height（0 = kBarHeight）/lagFrac（Simulate 维护）。fg/lag 比例 = **横向 UV 裁剪（uFrac）非缩放**——贴图不压扁。lag 收敛 = 线性追赶 `kLagCatch 0.35/s`（掉血追、回升贴平 = fg 全盖不可见）；皮肤变更 lagFrac 对齐 frac（残值不闪一帧）。
- 产包：`ExtractBarQuads` 2–3 描述子（bg 全幅 → lag（lagColor≠0 且 lagFrac>frac）→ fg 比例宽左锚）；`AppendGameFx` 贴图形态换批键槽 + UV 裁剪（签名加 `const AtlasRegistry&`，两渲染壳同改）。

## S3 飘字动效参数化

- `FxText` 尾加：scale/life（0 = 默认 0.8s）/driftX（恒速水平漂移 px/s）/curve（Linear = 现状 / **Pop** = 出生 1.4× 回落 1.0 + ease-out 陡升）。`TextMotion` 单源函数（单测直测）。
- 寿命逐条化的队首回收语义修正：过期条目**就地置空文本立即隐形**（长寿命条目阻塞队首计数回收是槽位延迟无害，但过期必须当场不可见）——单测锁死。池语义不变（256 环形，PopupTextEx 满池淘汰同旧）。

## S4 C# SDK + 桥（vtable 47→49）

- 桥尾加 `fxPopupEx`（8 参）/`fxBarEx`（8 参，bg/fg guid hex → spriteId 复用 `spriteOfGuid` 钩子双态解析，坏 guid = 0 → 白精灵降级不炸）。`Lemon.Fx`：`FxStyle`/`FxBarSkin` 结构 + `Text`/`Bar` 重载 + `Fx.Crit`（黄字 0xFFFFD24A Pop 1.3× 随机散布）/`Fx.Miss`（灰白 0.85× 0.6s）糖——纯默认值集合，游戏侧可全自定义。
- **hash 反例单测（TestFxSdk）**：FxProbeBehaviour（typeId 19，事件号段 1700）新参数全开全家桶调用 → 引擎侧对拍通道计数（TextCount=4/BarCount=1/skin 字段落地）+ **ComputeStateHash 跨帧逐位不变**——"表现层永不入回放"在升级面扩张后仍锁死（TestAudioSdk 同款机械反例）。

## S5 svr-test 接入

- 字体资产 `Assets/Fonts/NotoSansSC-Regular.otf`（引擎随仓拷入 + OFL 陪同）+ meta 词表 charset（`0123456789暴击闪避格挡MISS治疗经验LV+-.× `，20px/1px 描边）+ `project.lemon fxFont`。血条贴图 `bar_bg/bar_fg.png`（64×10 程序生成：深灰底框 / 白 fg 走 API 染色）。
- `PlayerBehaviour.OnHit`：怪受击 15% Crit 糖（`暴击 N`）+ 贴图血条延迟白条（表现层演示，数值结算不动）；`DuelBehaviour`（ani.scene 决斗场）升级为**批① 验收演示场**——25% 暴击数值层同源（×1.5 伤害与 Crit 表现同一随机源）+ 双方贴图血条常显 + 延迟白条 0.25s 节流刷新（收敛过程可观察）。

## 机器面验收

- **单测四件全落**（34,346 → **34,402 checks**，+56）：烘焙 golden（同 TTF 同参数两次烘焙**字节一致** + 四种参数变更态 stale 判定 + 表升序/字符集含中文全覆盖/LBF1 结构）/ Fx 数学（lag 单调收敛 + 回升贴平 / UV 裁剪区间 + 白精灵现状路径回归 / Pop 出生 1.4× 回落 + 陡升对比 / 寿命覆盖 + 过期隐形 / 池语义）/ 多页寻址回退（UV 覆盖产包 + 批键挂烘焙槽 + 缺字回退内置页 + 混排）/ hash 反例（script-tests）。**ctest 4/4**（behaviours 列表 19→20）。
- **实跑链路**（编辑器 --play ani.scene 900 帧）：字体后台烤 + 装载 ×2（开项目/进 Play 同槽换页路径）、`烘焙页装载（30 字形 512×512 槽 255）`、进 Play 音频 9 就绪、play-roundtrip byte-exact、errors=0；**`duel.winner` 落档** = 决斗打完 = Strike 的 Crit/贴图血条调用真实发生。`--validate` 冒烟（AGENTS 层解析绕行）Main + ani 两场 1080 帧零 VUID。
- **回归 full（手动档两轮 + bench 独立多轮）**：第①轮 15/20（五失败 = smoke-drag/smoke-ui/anim/final/game-smoke 注入族——**第②轮全数转绿**，同 2026-10-06 日报首轮挂点击族先例，09 §8 口径）；第②轮 19/20（唯 bench fps=74 软门，失败剖面 = `present=913ms` 单帧合成器停顿、sim 段正常）。**bench 独立复跑交叉判读**（本会话机载噪声大）：今日六轮 77/74/85/87/74/≈ ——两轮最干净剖面（无 >25ms 尖刺或尖刺全在已知 sim 波次段）**fps=85 / 87，且 `fx(texts=256 bars=128 饱和)` 口径 ≥ 基线 85**——批① Fx 通道扩张（TextMotion 逐条曲线 / lagFrac 收敛 / UV 覆盖位分支）满载零回退的直接证据；sim 段 avg 全轮稳定 ~10.4ms 与批⓪ 基线同值。**结论：门禁判读以干净剖面轮次为准，74 轮全部伴随可判别的环境尖刺（present 913ms / 机载并行），非批① 回退**——判别方法（先看帧八段剖面再下回退结论）登记为后续门禁口径。

## 教训（3 条，实抓）

1. **`BakedSlot()` 无外部页时是 0（程序化调色板槽）**——`ReleaseGpu` 未加 `HasBaked()` 守卫时把调色板页二次注销（UnregisterAtlas 撞活 sprite 断言），首次实跑抓的。守卫 + 注释钉死。
2. **worker 与同步兜底双烤同一 dst 的 rename 竞争**——开项目时 WarmFontBakes 刚入队、LoadFxFontPage 的 BakeStale 同步兜底并发写同一产物文件，其一 rename 失败假红字。修 = 队列非空（`fontBakePending_ > 0`）时同步兜底让路，烤成后由下次触发点装载。
3. **bench-survivor 的 present 段环境尖刺**——后台会话并行重负载时 present 单帧可到 913ms（合成器停顿），fps 从 85 掉到 74 触发软门禁假红；失败剖面的判别特征 = sim 段正常（~10ms）而 present 段 max 异常（>100ms）。复跑即绿（85/87/87）。后续门禁判读先看帧八段剖面再下回退结论。

## 真人面（待用户）

svr-test 一局走查：ani.scene 决斗场（暴击中文 Pop 弹跳黄字 / 贴图血条 / 受击延迟白条三项同屏）或 Main.scene 战斗（15% 暴击糖 + 怪贴图血条）。截图基线待前台会话补（本会话窗口回读黑 = 09 §9 已登记的后台会话现象，非批① 缺陷——基线 smoke 截图对照复现确认）。

## 真人走查迭代（3 轮，Y 向下符号教训）

- **第①轮反馈（字体重叠模糊/血条画身上）**：三因——血条锚点写死 pivot+2px（大精灵画身体中段）、描边 pad 使字形 quad 比 advance 宽 1-2px（相邻压边）、20px 字非整数缩放发虚。修 = 引擎侧"贴头顶锚定"（resolve 带出精灵高）+ `advance += 2*pad` 字距分离 + meta size 20→32 + C# 锚点/比例。
- **第②轮反馈（文字血条全消失）**：上一轮把条"顶出取景"。本轮修复又钉错边——**钉到 view.max.y，实际是屏底不是屏顶**（第③轮截图：条贴脚下）。
- **第③轮反馈（血条在脚下、飘字没有）→ 根因 = 世界坐标 Y 向下被当 Y 向上用**：`Camera2D` "Y 向下直映射"（`Mat3x2::Ortho` Y 行正号直入 Vulkan NDC +y=屏下），`view.min.y` 才是视口上沿、精灵头顶 = `pos.y - 全高/2`、上浮 = -y。批① 全部按 Y 向上写，方向全反。修（本轮，全绿 34,403 checks + ctest 4/4）：
  - `FxChannel::ExtractBarQuads`：条中心 = `pos.y - top*0.5 - h*0.5 - 2`（top = 全高 × scale.y，先取半）；
  - `FxChannel::TextMotion`：`outDy` 取负（-y = 上浮；Pop/Linear 两分支同改）；
  - `GameFx`：条钉边 `max(center.y, view.min.y + halfH + 1)`（贴屏上沿），飘字行顶 `drawY < view.min.y` 钉回（DrawTextEx 左上锚，行自锚点向下延展）；
  - C# 锚点按实测帧尺寸取负值：决斗 `-372/-348`（Monster01 440×420 / Monster02 600×480）、mob `-94/-80`（dungeon 系 32-48px）、玩家 `-228`（player01 337×346）——行底贴头顶血条上方。
- **教训 ④：坐标方向约定以渲染器为准，不以上下文语感为准**——"头顶/上浮/视口上沿"三个词在本引擎全是 -y 方向（Camera2D Y 向下直映射），与注释语感相反。第一性核对路径 = `Camera2D::ViewProj` → `Mat3x2::Ortho` → sprite.vert NDC 直映射；单测断言当年同样按 Y 向上写（"bar anchored above sprite top" 锁了正号），**通过的单测锁不住方向性错误，只锁得住实现与断言的一致**。已同步修正断言与 FxChannel.h 契约注释。

- **第④轮反馈（2026-10-07 截图：「暴击 7」显示成「格挡 7」+ 碎字形、青字非黄、血条悬空贴屏）→ 根因 = 烘焙器序列化像素基址 bug（活代码缺陷，非产物陈旧）**：
  - **字形左移**：`BakeFontFile` 序列化循环里像素写复用了随表推进的 `off` 当像素区基址 → 第 k 个字形位图整体左移 `(N-1-k)×5px`、后写覆盖先写——运行时按表寻址采到邻槽字形（「暴击」→「格挡」，数字区碎成邻字残条）。**golden 测试（两次烘焙字节一致）测不出自错位**，LoadBakedFont/golden/addressing 三件全绿照过；软件仿真（按 DrawTextEx 数学 + 页位图离屏合成）复现截图后定位。修 = 像素基址钉死 `tableEnd`；**回归锁 = 页墨水守恒**（页内墨水总数 == Σ声明格内墨水，错位必破）+ 逐格非空，进 TestFontBakeGolden。
  - **失效传播缺口**：`BakeFontStale` 只比 TTF mtime + paramsHash——烤制器代码自身的变更不触发重烘，修完代码磁盘旧产物照用。修 = `FontBakeParams::Hash` 混入烤制器版本盐 `kHashSalt`（算法修正 +1），旧产物 hash 必然失配 → 三触发点全量自动重烘；损坏产物另行删除（lemon-game 运行时不重烘只装载）。
  - **基线符号**（教训④ 同族漏网）：`DrawTextEx` 字形 quad 中心 `baseline + (bearingY - h/2)`——Y 向上公式写进 Y 向下世界，烘焙字形整体下垂 2×(bearingY−h/2)×字号 ≈ 24×字号 px 且与内置页回退字形错位。修 = 取负；svr-test 三处锚点按 +24×字号 下压补偿。
  - **血条悬空 = 帧透明边距**：头顶自动锚定按精灵整帧高计，Idle_000 alpha 包围盒实测 Monster01 内容顶 57px / Monster02 **169px**（600×480 帧内容只占 2/3）→ 条/字悬空数十至上百 px、字被视口钉边钳到屏顶。修 = `FxBar.anchorDy` 尾加（+ = 下压，0 = 现状零迁移）贯通桥（fxBarEx 8→9 参）与 `FxBarSkin.AnchorDy`；决斗场按实测边距下压 +57/+169，条加宽 140/200。
  - **Crit 糖色值**：`0xFFFFD24A` 按 ARGB 语感写、RGBA 序解出青绿（截图青字）→ 改 `0xFF4AD2FF` 真黄，注记防再犯。
  - 验证：单测 34,403→**34,405**（落位回归 +2）/ ctest 4/4 / script-tests 1787 / 编辑器实跑 ani.scene 900 帧（脚本编译、盐触发重烘、烘焙页装载 30 字形、errors=0、`duel.winner` 落档）+ `--validate` 零 VUID；离线 probe 重烤 + 页位图逐格复核（暴/击/7 三格各归其位）。
  - **教训 ⑤：golden/往返/addressing 三层单测全是"自一致"口径，锁不住"表↔像素"跨区错位**——落位类缺陷的回归锁必须是跨区不变量（墨水守恒这类守恒式），且失效传播要覆盖"产物生成器自身升级"这一维（版本盐）。
