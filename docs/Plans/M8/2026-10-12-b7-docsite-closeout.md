# M8 批⑦ —— 文档站 v1 + 09 §6.11 光照/FX 专项 + 段收官

Status: in-progress（2026-10-12 开工落账 + **同日日1 完成**：A1–A4 基座全过（2 页生成/负面测试原始 HTML 与死链各验 exit 1/嵌套列表结构核对）+ C1 §6.11 落账 + B1 intro + B8 lighting（API 逐名核对 Lighting.cs）；[DevLog](../../DevLog/2026-10-12-m8-b7-docsite-start.md)；**同日日2 完成：B2–B7/B9–B13 十一页全数落齐——十三页站生成 13 页零错误（零死链/零越子集语法），intro 全页导航链通**；[DevLog](../../DevLog/2026-10-12-m8-b7-manual-pages.md)；**A/B/C 三组全部收口；余 = 段收官 D 组 gate = 批⑥ mac 侧收口**（soak B2 红旗判读落账），见 §3 D4）

## 1. 输入条件（2026-10-12 探测核证）

- **批⑥ 现状与本批时序**：A（压测终测三跑 91/90/91）/C（#L5 探针关账）已收口；soak v5 硬判据全过但 **B2 RSS 红旗**（稳态 +44.5MB/h 完美线性，对照跑 A 待重跑）；D 组（#M4 Windows 轮）待用户窗口。**M8.md §3 排程注记背书：批⑦ 不消费 soak 结果，批⑥ 后半与批⑦ 并行压缩段尾**——本批文档面（A/B/C 组）即日动工；**段收官 D 组显式 gate 在批⑥ mac 侧收口**（红旗未清即勾销 08 §0 = 假收官）。
- **出口判据**（M8.md 批次表 ⑦ 行 + 08 §0 M8 行）：文档站可导航覆盖核心工作流 / 09 §6.11 实测数字落账 / 08 §0 M8 行勾销 / 段收官 DevLog。
- **yami 对标形态**（`~/GameProjects/yami-rpg-editor/Documentation/Docusaurus/`，只读参照）：docs/ 目录数字前缀分类（001.basic-interfaces / 002.inspectors / 003.event-commands / 004.hotkeys / script-api / intro）+ i18n/zh。**对标其职能（分类可导航的用户手册），不搬其工具链**（裁决 D1）。
- **本机工具事实**：node v24.12.0 / pnpm 10.26.2 / python 3.10.11（**无** markdown 模块）；本机无可复用 vendored marked/markdown-it。⇒ 文档站工具 = 零依赖 node 静态生成器（D1）。
- **内容素材盘点**：`Engine/Scripting/dotnet/Lemon.SDK/` 22 个门面文件（Lighting/SceneManager/SceneOps/GameObject/Input/Audio/Ui/GameUI/Fx/Tween/Table/Save/Time/Events/Physics/Assets/Anim/Balance/Behaviours/StateBag/Scripting/LemonSDK）；组件 id 至 33（ComponentCatalog 全反射驱动 = 手册组件表单一来源）；`Templates/vs-survivor` 生成器；`demo/svr-test` + `demo/ui-test` 实例项目；设计基准 00–09 十一分册 + ADR-019 等决策记录。**注意**：`EngineDesign/Editor-Manual-Test-Guide.md` 是编辑器手工测试向（内部验收用），不是用户手册——本批新建 `docs/Manual/`，不复用不改写。
- **回归口径现状**：09 §9 已登记 full 26 步（L477/L516）——无口径滞后欠账（M8.md §1 #9 的 21 步滞后已被批②–④ 顺手修复）。
- **smoke-light 九断言权威口径**（`EditorAppSmokeLight.cpp:306-316`）：`base / onVsBase / onDiff / mask / blob / plan / tier / gizmo / offIdentical` 九位全 1 才 PASS——§6.11 与手册光照页按此引用。

## 2. 文件/行级变更单

### A 文档站基座（零依赖静态站）

- [x] **A1** `docs/Manual/` 区注册：`docs/README.md` 五区制表加行（第六区：**长期用户手册**（活文档）：数字前缀 `NN-slug.md` 排序即导航序，维护同 EngineDesign）+ `AGENTS.md` 目录地图注记。—— **✅ 2026-10-12**：README 目录表 + 文体分流表各加行；Lemon/AGENTS.md 流程约定与根 AGENTS.md 目录地图同步「五区制→六区制」。
- [x] **A2** `tools/build-manual.mjs`：零依赖 node 生成器——受限 markdown 子集（标题/段落/粗斜体/行内码/链接/图片/无序有序列表/代码栅栏/表格/引用/水平线），**不识别的语法硬失败退出非零**（渲染错误不允许静默吞掉）；页序 = 文件名数字前缀（目录树即索引，README 规约同款）；产物 `build/manual/`（file:// 直开，相对链接零 fetch）。—— **✅ 2026-10-12**（~290 行；子集白名单见头注；引用体递归解析、表格对齐冒号、嵌套列表两级）。
- [x] **A3** 站点壳：侧栏导航（当前页高亮）+ 内嵌 CSS（阅读版式，代码块/表格样式）+ 页间上一页/下一页。—— **✅ 2026-10-12**（侧栏分组 GROUPS 表 + `index.html` 跳转落地页——站点产物非仓库 INDEX 文件，README 规约不冲突）。
- [x] **A4** 构建验证：全页生成零错误 + 站内相对链接断链检查（生成器内置——死链 = 构建失败）；`tools/build-manual.sh` 一行包装。—— **✅ 2026-10-12**：正式站 2 页生成过；负面测试两条各验 exit 1（原始 HTML 行、站内死链）；嵌套列表 HTML 结构核对（`<ul>` 正确内嵌父 `<li>`）。

### B 使用手册内容（v1 十三页，zh-CN）

- [x] **B1** `01-intro.md`：引擎定位与边界（纯 2D/品类/不做 3D 不做重物理）+ 五分钟跑起来（mac/win preset、lemon-editor 首开）。素材：00 总纲、AGENTS 构建段、Windows-Build-Guide。—— **✅ 2026-10-12**（含能力面一览/平台表/lemon-game 直开/「如何读本手册」分层导读——手册不复载数字的 D6 纪律成文）。
- [x] **B2** `02-getting-started.md`：新建项目（project.lemon / .lemon/ 目录）+ vs-survivor 模板生成器 + lemon-game 运行 + 出包初览。素材：06 资产分册、Templates/。—— **✅ 2026-10-12**（项目布局树 + project.lemon 实例（逐字段对模板实物）+ 向导模板表 + `--gen-vs-template` + 运行三形态 + 热重载口径）。
- [x] **B3** `03-editor.md`：布局/SceneView/Hierarchy/Inspector/Assets 四面板 + gizmo + Play 模式 + i18n 切换。素材：05 编辑器分册。—— **✅ 2026-10-12**（核心七面板职责表 + Play/Stop 快照沙盒语义（Play 中改动只落 Play World、Stop 即丢；Undo 禁用）+ 保存/自动备份 + 日常操作节）。
- [x] **B4** `04-components.md`：组件目录全表（id/字段/用途，ComponentCatalog 单一来源）+ Add Component 工作流 + 运行时语义。素材：03 ECS 分册。—— **✅ 2026-10-12**（34 组件按域五表，字段名逐个对 `ComponentCatalog.cpp`；FIELD_RT 运行时态不入档语义 + team 2 惯例成文）。
- [x] **B5** `05-scripting.md`：C# 脚本模型（生命周期/批量 API + 事件队列两通道）+ Lemon.SDK 门面总览（22 文件分域表）+ 第一个脚本 walkthrough。素材：04 C# 分册、Lemon.SDK。—— **✅ 2026-10-12**（生命周期七回调表对 `GameObject.cs`；IForEachSystem 示例对 `Scripting.cs` 签名（`ref readonly Chunk`，`Time.DeltaTime` 源码核对）；**结构命令两拍**语义成文；SDK 十域总览表）。
- [x] **B6** `06-scenes.md`：场景/.scene 格式 + SceneManager 多场景 + LoadSceneAsync + DDOL + 加载屏模式。素材：M7c 批⑦⑧⑨、SceneManager.cs。—— **✅ 2026-10-12**（API 表 + 换场帧协议⑤时序 + AsyncSceneLoad 成员表（**progress 纯呈现量禁挂玩法分支** + allowSceneActivation 门）+ 跨场壳模式（GameFlow 种子/加载屏）+ v1 边界（Single 唯一/Additive 红字）；ADR-017 实链）。
- [x] **B7** `07-assets.md`：资产管线（sprite/图集/prefab/.asset/GUID/.baked）+ 引用与重定向。素材：06 分册。—— **✅ 2026-10-12**（.meta 实例 + manifest 体检 + 十类资产表 + 孤儿清扫口径 + 编辑器态/发布态图集两态 + GUID 十六进制串脚本引用口径）。
- [x] **B8** `08-lighting.md`：Light2D（四类型/三混合）+ BlobShadow2D + 蒙版光语义（rgb×a 无衰减）+ ambient 使用注意（加亮类场景 ambient 须留暗）+ 质量三档 + DayNight 循环。素材：ADR-019、02 §8、批③④⑤ DevLog。—— **✅ 2026-10-12**（字段表/API 表逐名核对 `Lighting.cs`；**两拍时序与补间在途禁写同字段两条坑显式成文**；DayNightCycle 缩减样例取自 svr-test 真实消费者；设计依据三链尾）。
- [x] **B9** `09-fx.md`：PostFx 链（bloom 双滤波三级/色调曲线/冲击波 UV 扰动）+ QualityParams.fxChain 档位语义 + 关链零成本口径。素材：02 §4、批④。—— **✅ 2026-10-12**（**世界空间 Fx（飘字/血条）与全屏 FX 链分开成两节防混淆**；Fx API 表逐名核对 `Fx.cs`；质量三档全景表对 `Quality.h`（Low 20k 粒子/32 灯/关链）；v1 无脚本面 FX 参数位如实登记）。
- [x] **B10** `10-ui.md`：RmlUi（.rml/.rcss、UIDocument 声明式装载、C# Ui/GameUI 门面）。素材：ADR-014、M6b 各批。—— **✅ 2026-10-12**（**三层分工**开篇（世界空间 HUD/RtUi 快速 HUD/RmlUi 完整界面）；`Ui.cs`（ShowCards/CardPick/ShowDialog）与 `GameUI.cs`（UI.Apply 提交制 + UI.Events 反向）两门面分工成文；「忘了 Apply = 屏上不动」坑登记）。
- [x] **B11** `11-audio.md`：AudioSource + .baked 烘焙格式 + 覆写规则。素材：ADR-015、M6c。—— **✅ 2026-10-12**（组件字段表 + Audio 门面九 API 逐名核对 `Audio.cs`；**TimeScale 不自动映射暂停**的流程冻结防线 + 循环点烘焙期口径成文）。
- [x] **B12** `12-packaging.md`：lemon-packager 出包 + Windows 真机清单 + 独立运行时。素材：M7a b7、Windows-Build-Guide。—— **✅ 2026-10-12**（两步出包命令 + 参数表 + **自检行四 Miss/extra 全 0 判据** + HintPath 重锚注意 + lavapipe 提示；命令形态对 Windows-Build-Guide §6）。
- [x] **B13** `13-debug.md`：调试旗标面（--validate/--frames/--vsync/--screenshot/无头 svr.* 系列）+ 金回放口径（用户视角）+ 常见坑（验证层 dyld 绕行）。素材：09、AGENTS 本机坑。—— **✅ 2026-10-12**（两壳旗标表对 `EditorEntry.cpp` usage 与 GameEntry CLI 注释；DYLD 绕行/不限速挂机冻结/结构命令两拍/补间禁写/无头站桩死亡五坑清单 + 确定性回放用户视角（Scripting.Rng 纪律））。
- 内容纪律：手册页只写**用户视角的用法与语义**；设计依据/实测数字链回 EngineDesign 与 DevLog（手册不复载数字，防两处漂移）；API 名以 Lemon.SDK 源码为准逐名核对。

### C 09 §6.11 光照/FX 专项落账

- [x] **C1** `docs/EngineDesign/09-Testing.md` §6.10 后新小节 **§6.11 光照与后处理专项（M8）**：四回归步口径（smoke-light 编辑器九断言 base…offIdentical / light-smoke 游戏壳三段式+fps≥60 / smoke-fx / fx-smoke 含关链逐位同两壳各证）+ bench-lights（软门禁 55，批⑥ 读数 881）+ bench-survivor-lit 终测（门禁 60，三跑 91/90/91，alive=10507/lights=64/fxPass=5/5）+ 质量三档 lightRT 尺寸两壳实测（编辑器 327×184→654×368→1308×736 / 游戏 320×180→640×360→1280×720）+ #L5 探针结论引用（§6.8 金回放与灯无关口径：基准场零 Light2D）+ 关灯零成本口径（02 §8 引用为主）。—— **✅ 2026-10-12** 落账（九断言名与 Δ 阈值以 `EditorAppSmokeLight.cpp:306` verdict 行与源码注释推导为准——文档两处 166/170 之差 = 实测波动，断言阈统一 >30；压测 A 终测表已在 §6.10 尾（批⑥ E1 先落），§6.11 引用不重复）。

### D 段收官（**gate = 批⑥ mac 侧收口**，B2 判读落账后执行）

- [ ] **D1** `08-Development-Roadmap.md`：§0 总览表 M8 行勾销注记 + §166 段落终态 + incremental 模板「若余量」判否注记（移 v1.1 触发式，见 D4）。
- [ ] **D2** `M8.md` ⑦ 行终态 + `AGENTS.md` 当前阶段段瘦身（M8 内联历史压缩为总览 + 链接，M7c 同款瘦身先例）。
- [ ] **D3** 段收官 DevLog（`2026-10-XX-m8-closeout.md`）：段出口判据逐条对表（压测 A/关灯零成本/soak/文档站/真人走查）。
- [ ] **D4** incremental 模板移 v1.1 登记（本批 §3 D3 裁决；M8.md ⑦+ 候选池行随本批落）——若批⑥ B2 快速判「场景性堆积」且文档面提前完工，可复活评估，默认不做。

## 3. 设计裁决（默认执行，用户可否决）

- **D1 文档站工具 = 零依赖 node 静态生成器，非 Docusaurus**：yami 对标的是职能（分类可导航用户手册）非工具链；Docusaurus 拉数百 MB node_modules + 本机网络坑（git 代理时通时断、CDN 不稳）与「最小版」定位冲突。生成器 ~200 行受限子集解析；手册内容自控故子集可控，**不识别语法硬失败**（错渲染比不渲染更糟）。落 `tools/` 为开发工具（ccache 同类，非引擎依赖，不进 THIRD_PARTY.md/07 移植矩阵）。
- **D2 文档站落位 = `docs/Manual/`（五区制扩六区），非根级 Documentation/**：用户手册是长期活文档，性质同 EngineDesign；yami 式根级新目录与既有五区制地图冲突。源文件即 markdown 手册（双读者：站内渲染 / 仓库直读），数字前缀排序即导航序（README「目录树即索引」规约同款，不设 INDEX 文件）；生成产物 `build/manual/` 不入 git。
- **D3 incremental 模板移 v1.1（「若余量」判否）**：批⑥ soak B2 红旗归因未完（对照跑 A 待重跑；若判引擎真泄漏则修复插队占档期）+ 文档站十三页主体 2–3 日——余量条件不成立。候选池登记为触发式（消费者出现即做，同 anchor 采样口径）。
- **D4 段收官 gate = 批⑥ mac 侧收口**：文档面即日动工的背书是 M8.md §3 排程注记（批⑦ 不消费 soak 结果）；但 08 §0 M8 行勾销必须等 B2 结论落账——红旗未清即收官 = 把未决缺陷埋进已勾销段。#M4 Windows 轮维持 b6「可后挪不阻塞」敞口口径。
- **D5 手册语言 = zh-CN 单语**：与 docs 全库一致；en 列 v1.1（yami 的 i18n 对标项届时再议）。
- **D6 手册不复载实测数字**：性能/断言阈值一律链接 EngineDesign/09 与 DevLog——手册的稳定性优先，数字是 DevLog 的事。

## 4. 排程（预估 2–3 日）

- 日1：A1–A4 基座 + C1 §6.11 + B1/B8（intro/lighting 先行——素材最新最不易错）。
- 日2：B2–B7（入门链：项目→编辑器→组件→脚本→场景→资产）。
- 日3：B9–B13 + 全站构建验证 + 断链检查；批⑥ B2 结论落地即触发 D 组收官。
- 超期预案：B 组页可分两波交付（核心工作流七页为先，UI/音频/出包/调试四页后补）；A 基座与 C1 不可让（出口判据直接挂靠）。

## 5. 完工记录

（开工时空；段收官记录落 DevLog `2026-10-XX-m8-closeout.md`）
