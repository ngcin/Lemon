# M8 批② 完工 —— 编辑器光贴图竖切（三段式渲染链机器面收口）

- 日期：2026-10-11（当日开工当日机器面收口）
- 批文件：[Plans/M8/2026-10-11-b2-lightmap-editor-slice.md](../Plans/M8/2026-10-11-b2-lightmap-editor-slice.md)（开工三项拍板见其 §4）
- 设计权威：[ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md) M1/M3/M5 + [02 §8](../EngineDesign/02-Rendering-Vulkan.md)（衰减/gizmo 形态本批定形入册）

## 落地清单（机器面）

- **三段式渲染链**：场景段 sceneRT → 光照段 lightRT（CLEAR=ambient）→ 合成段 gameRT（composite 全屏 gamma 直乘）→ 豁免段（251/252/254 + RmlUi 同块内）。仅 GameView（idx=1）；**关灯帧走原路径逐位同**（smoke-light memcmp 机器证）。
- **引擎件**：`LightBatcher`（实例环 SSBO slot 2 + 3 混合管线 Additive/Screen/Subtract）+ `LightCompositor`（Prepare 纯 CPU：Global 去重取首个 + WARN / SpriteMask 滤除（批③）+ 预算截断 WARN / ambient）+ `light.vert|frag` + `composite.vert|frag`（lemon_embed_shader ×4，D10 护栏扩三 frag）。
- **RHI 扩展**：BlendMode 尾插 **Screen**（One/OneMinusSrcColor）与 **Subtract**（REVERSE_SUBTRACT）；零新 Format（RGBA8Unorm 拍板核证通过，02 §8 注记与实现一致无需翻案）。
- **提取通道**：RenderableManager 光/blob 双缓冲数组（插值/剔除按 range 光圆、Global 恒可见）+ SceneExtractor `View<Transform2D, Light2D|BlobShadow2D>` 双通道（位索引/version/差集释放镜像 sprite 语义；悬空 maskSprite 不阻断）。
- **SpriteBatcher 分段**：Bake append 模式 + RecordRange 批区间 + Reserve 前置容量（段间扩容 UAF 防线）。
- **编辑器面**：SceneView 灯 gizmo（Point/Spot range 折线圈 + Spot 楔形两边线 + Global 不画 + Blob 椭圆虚框，色全不透明与手柄黄 b 通道距 40 可分离）；Inspector maskSprite 升 ED_ASSET 接 sprite 下拉（AssetRef 分派按组件名分派，Light2D 无 guid 面）。
- **QualityManager 编辑器壳首接**：QualityParams + lightRtScale（¼/½/全）+ lightBudget（32/64/64）；渲染帧钟 EMA 降档；lightRT 按档重建（lemon-game 壳随批③）。
- **bindless +2**：sceneRT/lightRT 高位槽 250/251（建 RT 时绑一次，#L20 记账未近界）。

## 实测数字

| 项 | 结果 |
|---|---|
| 单测 | **34,864**（+67：衰减曲线/角向 softstep/提取通道/Compositor Plan 四组 +62；观感支持期 tag UTF-8 截断修复 +5，见追记） |
| ctest | 4/4 |
| 回归 full | **23 步全绿**（22→23，smoke-light 新步首跑绿；smoke-ui retry 1 = 09 §9 注入抖动家族，隔离复跑 2/3 绿且红计数器逐次不同（回归内 save=0 / 隔离 gsel+rename=0）——归因口径同 M7c 批⑨） |
| smoke-light | 七断言全 PASS（--validate 全程）：三段式开灯帧 diff=1,881,124B / 灯下·灯外 maxΔ=165 / drawLights=3 + ambient=(1.00,0.96,0.84) / 三档 lightRT 327×184→654×368→1308×736 逐档精确 / gizmo 圈楔形 1596px + blob 虚框 184px / **关灯帧与基线逐位同** |
| bench-lights | **fps=515 PASS**（64 灯 + 200 精灵 + 8 blob 编辑态，frameAvg=1.94ms / frameMax=16.51ms；软门禁 55fps，数字入 ci-daily 报告） |
| bench-survivor 门禁 | fps=86 ≥ 76.5（回归内） |
| 金回放 | 零重录（本批无新组件——关灯路径逐位同 + 无哈希流面改动） |
| vtable / 组件 / 系统 | 59 / id 至 33 / 21 不变 |
| 构建 | 零警告（编译器面；ld 重复库 ×2 = 既登记项不变） |

## 过程记录

1. 首跑 smoke-light 六绿一红（gizmo=0）：gizmo 色 alpha=200/160 经 Alpha 混合向背景拉暗，CountPixelsNear 按原色匹配不上——两 gizmo 色改全不透明（与手柄黄同款口径），顺手修 Global 灯种子色 g/b 字节序笔误。
2. 回归首跑 smoke-light 本体 PASS 但 rc=1：灯播种在 SeedSmokeScene 后追加 5 实体撞基础冒烟守恒断言（10/5）——播种后重定 `smokeSeeded_`。
3. LightCompositor::Prepare 原内联 `batcher_.Bake`（碰 GPU 设备）——拆到 RenderLightPass，Prepare 成纯 CPU（单测可直调，职责更真）。
4. bench-lights 编辑态八段戳不全（非 Play 帧段界不打，pump/glue 天文数字）——只报全帧口径，GPU 分解归批⑥。

## 追记（2026-10-11 真人观感支持期）

- **观感夹具**：`demo/svr-test/Scenes/light.scene`（30 实体：白图染色暗底地砖 + hero/怪×2/boss + 四角四灯（Additive 红/绿、Screen 蓝/橙）+ 中央 Subtract 暗池 + 朝上 Spot 暖光 + 默认关的 Global + blob 标注位）。开灯/关灯同场景截图差分 **67,672 px**（maxd=426）机器证光效在渲。是否入库随用户。
- **伴随 bug 修（引擎面，实抓即修）**：`Meta.tag[24]` 三处写入点（SceneArchive 读档 Blob24 / EditorContext::CreateEntity / Hierarchy 改名）裸按字节截断——中文 tag 超 24B 时多字节字符被拦腰切半，驻留内存的半字符让交互会话 autosave 序列化时 nlohmann 抛 `type_error.316` **崩编辑器**（无头跑不触发 autosave 故回归不可见）。修法 = `CopyTag24`（CoreComponents.h：UTF-8 边界对齐截断，超限回退到尾字符起始字节）三链路统一换用；单测 +5（**34,864**，前向解码器验证无半字符尾）。
- **观感第一轮反馈（用户 Play 截图）**：只见中央 Subtract 暗池显形、四角光池与暖锥光不可见——非渲染链 bug：夹具把 Global 留禁用 ⇒ ambient=纯白，LDR 光贴图钳 1.0 下 **Additive/Screen 对满值只加不亮**（数学空操作），唯 Subtract（做减法）可见；截图逐条吻合模型，反证链路行为正确。修夹具 = `L_Global_环境光` 启用（暖调 ×0.45 强度，地板压成昏暗底色）+ tag 缩到 UTF-8 安全长度。**使用约束（已补注 02 §8 ambient/昼夜条）**：摆加亮类灯（Additive/Screen）时 ambient 须留暗（<1）；ambient=白是“全亮无影”语义而非中性底。
- **观感支持第二项：Inspector 启用开关**：用户找不到灯开关——Light2D.flags 原以裸位域数字行（“标志位: 1”）暴露。升级 = 该行改「启用」复选框（bit0 = kLightEnabled，Bool 控件同款就地写 + Deactivated 属性轨 Undo；scrub/位域直改入口排除；i18n 双语标签 + tooltip 改用户话术）。smoke-light 复跑七断言 + editor-smoke PASS（kFSelect 无头选中灯实体即覆盖新分支），构建零警告。
- **真人观感收口（2026-10-11）**：用户过目 light.scene（Play 态 GameView 四角灯池/中央暗池/暖锥光/ambient 底色 + 启用开关现场切换）verdict「可以了」——**批② 出口末项勾销，批② done**。light.scene 留 demo/svr-test（回归不受影响，去留随用户）。
- **评审轮（2026-10-11，done 后 commit 前自审：三片并行深审 46 文件，九发现 P0×1/P1×2/P2×6 全即修）**：
  - **P0 light.frag Spot 内半角保护式写反（max→min）**：`max(halfInner, 0.999·halfOuter)` 在 innerRatio<1 时恒取 0.999·外半角 = 软边塌到外缘 0.1% 宽（默认 innerRatio=0.5 的锥光近硬边，与 CPU 纯函数背离）；连带 angle=0 时 smoothstep edge0==edge1 除零 NaN——改 `min` + halfOuter≤0 early-out（CPU `LightSpotAngular` 同判，02 §8 两处同式纪律恢复）。单测只测 CPU 函数、冒烟 Spot 只粗断言，故机器面未抓到——真 Bug 藏在 shader/CPU 双实现的分叉缝里。
  - **P1 关灯帧双 Bake**：三段式分支前遗留原 Bake 未挪 + 分支后又添新 Bake = 每视口每帧 2 次全量烘焙（开灯帧 3 次）；命令流仍逐位同（Bake 幂等，memcmp 冒烟抓不到）但白烧实例填充热路径（恰是 2026-09-26 零拷贝批要省的，且拖累 bench-survivor 门禁）。删前置 Bake——bench-lights **515→532fps**。
  - **P1 smoke-light 光斑断言假判据**：gameRT 灯下/灯外比较量的是场景内容差（红光 r 通道被 ambient r=1.0 饱和钳没 + 灯下点恰在 Player 精灵上），光源四边形整体不渲断言照样过。重构 = 改读 **lightRT**（无场景内容，判据免疫场景污染）：f102 捕获/f103 断言灯心 vs 远角 Δ>30，实测 **165**（理论 0.65×255≈166 精确吻合）；Global 强度 1.0→0.35 留 r 余量（同因：ambient 饱和让加亮类灯不可见——与观感轮夹具坑同根）。
  - **P2 六项**：捕获重录帧偏移（capReq 门改 capFrame 门——fetch 帧不再重录，取回像素与声明帧一致，组合 --smoke-anim 不再假红）/ SpriteBatcher append 容量契约断言（越界=写穿环或段间重建 UAF，从静默腐败变响断言）+ RecordRange 批界断言 / bench-lights 判决行回显 **tier+lightRT 尺寸**（EMA 自动降档的缩水负载自此可辨）/ LightBatcher 死 `<chrono>` / SmokeLightState 死 `phase[8]` / 注释纠错（Screen 光距 553 非 590）。
  - 复测全绿：构建零警告；smoke-light 七断言 + editor-smoke PASS（--validate，新断言首跑绿）；bench-lights 532fps PASS（tier=High lightRT=1308×736）；单测 **34,864** OK。

## 待办（移交）

- ~~15 分钟真人观感~~ ✅ 2026-10-11 用户过目通过（light.scene：三混合灯池 / ambient 底色 / 启用开关现场切换 / SceneView 圈+楔形+虚框）——批② done。
- 批③：lemon-game 装配（懒建 sceneRT + 双格式管线 + QualityManager 游戏壳）、SpriteMask 蒙版渲染、blob 椭圆渲染（数据面本批已备）、`Lemon.Lighting` 门面。
