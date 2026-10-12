# M8 批⑦ 开工 —— 文档站 v1 基座 + 09 §6.11 光照/FX 专项落账（日1）

- 日期:2026-10-12(批文件落账 + 日1 完成基座/§6.11/首两页)
- 批文件:[Plans/M8/2026-10-12-b7-docsite-closeout.md](../Plans/M8/2026-10-12-b7-docsite-closeout.md)
- 时序背景:与批⑥ 后半并行开工(M8.md §3 排程注记背书——批⑦ 不消费 soak 结果);**段收官 D 组 gate = 批⑥ mac 侧收口**(soak B2 红旗判读落账,红旗未清不勾销 08 §0 M8 行)。

## 1. 设计裁决五条(批文件 §3,默认执行)

- **D1 工具 = 零依赖 node 静态生成器,非 Docusaurus**:yami 对标的是职能(分类可导航用户手册)非工具链;Docusaurus 数百 MB node_modules + 本机网络坑与「最小版」冲突。生成器 `tools/build-manual.mjs` ~290 行,受限 markdown 子集 + **不识别语法硬失败**(错渲染不如不渲染);探测核证 = 本机无 python markdown 模块、无可复用 vendored marked。
- **D2 落位 = `docs/Manual/` 六区制新区**(非 yami 式根级 Documentation/):用户手册 = 长期活文档,性质同 EngineDesign;数字前缀 NN-slug.md 排序即导航序(「目录树即索引」同款);产物 `build/manual/` 不入 git(build/ 已 ignore)。
- **D3 incremental 模板「若余量」判否 → v1.1 触发式**:B2 红旗归因未完 + 文档站十三页主体占满档期;M8.md ⑦+ 候选池已登记。
- **D5 手册 zh-CN 单语**(en 列 v1.1);**D6 手册不复载数字,链回 EngineDesign/DevLog**(防两处漂移)。

## 2. 日1 交付(全部可运行验收)

- **文档站基座(A1–A4 ✅)**:`docs/Manual/` 区注册(docs/README 六区制表 + 文体分流行;Lemon/AGENTS.md 流程约定 + 根 AGENTS.md 目录地图同步)+ `tools/build-manual.mjs`(零依赖;子集 = 标题/段落/粗斜/行内码/链接/图/两级列表/围栏码/表格/引用/水平线;站内 .md 链接重写 .html + 死链即败;`../` 外链原样保留 = 仓库内 markdown 正常链接)+ `build-manual.sh` 包装 + 侧栏分组导航/当前页高亮/上下页/style.css/index.html 落地页。
  - 验证:正式站 2 页生成过;**负面测试两条各验 exit 1**(原始 HTML 行「子集无原始 HTML」、站内死链「05-none.md」);嵌套列表 HTML 结构核对(子 `<ul>` 正确内嵌父 `<li>`)。
- **09 §6.11 光照与后处理专项落账(C1 ✅)**:四回归步口径表(smoke-light **九断言** `base/onVsBase/onDiff/mask/blob/plan/tier/gizmo/offIdentical`——名与 Δ 阈以 `EditorAppSmokeLight.cpp:306` verdict 与源码注释推导为准,断言阈统一 >30;light-smoke 游戏壳 + fps≥60;smoke-fx/fx-smoke 关链逐位同两壳各证)+ bench-lights 三读数沿革(515→919→881)+ 质量三档 lightRT 两壳尺寸 + 与 §6.8 金回放关系(基准场零 Light2D 实例先例)+ #L5 探针引用。压测 A 终测表不重复(批⑥ E1 已落 §6.10 尾)。
- **首批两页(B1/B8 ✅)**:`01-intro.md`(定位边界/能力面/平台表/五分钟跑起来/分层导读)与 `08-lighting.md`(组件字段表 + C# API 表逐名核对 `Lighting.cs`;**两拍时序**(SetGlobalAmbient/BlobShadow.Set 组件缺失时 op2 暂存返回 false,引擎默认值第二拍前已可见——下一 tick 再调落值)与**补间在途禁写同字段**两条坑显式成文;DayNightCycle 缩减样例取自 svr-test 真实消费者)。
- 构建/查看:`./tools/build-manual.sh && open build/manual/index.html`(file:// 直开,零服务器)。

## 3. 余项

- B2–B7/B9–B13 十一页内容(02 入门链 → 09–13 表现/系统/发布;超期预案 = 核心七页先行)。
- 段收官 D 组:08 §0 M8 行勾销 + M8.md 终态 + AGENTS.md 瘦身 + 收官 DevLog——**等批⑥ B2 对照跑 A 判读落账后触发**。
