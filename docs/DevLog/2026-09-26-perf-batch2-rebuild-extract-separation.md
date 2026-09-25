# 2026-09-26 · 性能批②——目标板 Rebuild 并行化 + 渲染提取共享 + 分离调参下放（五万场 33→40fps）

来源：[AI 并行化批](./2026-09-26-ai-parallelize.md)余量登记三件（按当时大小序）：
① TargetBoard Rebuild 串行 ② 渲染提取/烘焙 8.3ms ③ Separation 调参 5.6ms。
用户批准按建议开工。三件全落，**五万场 frameAvg 30.0→25.2ms（fps 33→40）**；
三批累计 48.5ms/21fps → 25.2ms/40fps。

## ① TargetBoard::Rebuild 并行化（串行 2.51→1.69ms）+ 认知修正

- **实现**（Systems.h/.cpp）：主线程先按 view 迭代序收集实体（view 无随机访问，
  list 序的等距平局语义系于此）→ ParallelFor 按 grain 2048 对齐切块（JobSystem
  块界契约）分桶到 per-chunk 缓冲 → 主线程按 chunk 序归并（= view 序逐位不变）
  → 各队 Grid::Build 逐队 Schedule 并行（排序键 (cellKey, 池索引) 唯一 → 结果
  唯一确定；任务内不嵌套 ParallelFor，JobSystem 纪律）。阈值 8192 以下/单线程
  档走原串行路径（增量参数 `JobSystem* jobs = nullptr`，存量调用零改动）。
- **认知修正（重要）**：上批登记"AI 余 9.9ms 的大头 = Rebuild 串行（max 尖刺
  47ms 即其指纹）"被临时计测推翻——五万场 Rebuild 全程仅 **2.51ms**（并行后
  1.69ms），AI 段剩余 ~7ms 在 Shooter 索敌/冷却段、开火生成段（串行工厂调用）
  与 Patrol 段。上批 47ms 尖刺指纹另有其因（本批后 sim max 仍 ~55ms：弹幕波峰/
  census 叠帧）。**下轮候选重排：AI 段的主靶是 Shooter 生成段串行化与索敌环扫，
  不是 Rebuild**。
- **验证**：新增 `TestTargetBoardParallelRebuildIsomorphic`（engine-tests 29671→
  **33375 checks**）：8700 实体 + 掺销毁重建（view 序 ≠ 创建序），串行/并行
  Rebuild 的 list 逐位强比较（TeamEntries 测试接口）+ 同 cell ±8px 等距平局对
  （钉"序先见者"）+ 随机查询 ×4 档半径双队对拍。

## ② 渲染提取（scene 段 8.15→6.03ms，fps 35→38，尖刺帧 >25ms 1479→758）

临时计测分解（五万场双视口）：**ExtractScene 3.37ms**（最大头）+ 每视口 ~2.0ms。
四刀，全部绘制输出同构（editor-regression 像素断言把守）：

1. **ComputeWorldTransform 无父快径**（Hierarchy.cpp）：无 Hierarchy/根父 =
   world 恒等 local（通式 rot=0/cs=1 特例），免链构建与 cos/sin。平铺大场全量
   走此径（内核 #1 消费端通用受益）。
2. **SetAll 整包推送**（Renderable + ViewportRenderer）：4 setter 独立寻址合
   一次（每实体省 3 次 entries_ 寻址）。
3. **实体映射槽位化**（ViewportRenderer）：`unordered_map<uint64_t>` find
   （五万实体 ~1ms/帧）→ 按 entt index 直下标数组 + `Scene::EnttIndex/EnttVersion`
   （Scene.h 新增，`entt::to_version` 公开 API + entt_traits entity_mask 断言钉
   布局）。回收同槽新实体（version 不同）就地回收旧 rid 防泄漏；差集改全容量
   顺序扫。ExtractScene 3.37→**~2.0ms**。
4. **双视口共享帧内计算 + Bake 尾段**（RenderableManager/SpriteBatcher/
   ViewportRenderer）：Extract 拆帧内共享段（有效性/插值/键归类/sortKey，按
   (simVersion, alpha) 缓存——第二视口原为 SetViewport 改版本号后全量重算）+
   per-viewport 段（剔除+分桶+桶内排序，搬运序逐位不变）；Bake 增尾段参数
   （overlay/血条），消 RenderViewport 每帧 `std::vector all(packets)` 堆分配
   + 50k×56B×2 合成拷贝（~11MB/帧）；text/fxBar 缓冲成员化（提取段零分配）。

## ③ Separation 调参下放（五万场 Separation 5.5→4.39ms；引擎默认零改动）

- **密度探针实证**（临时直方图，已撤）：有效邻居计数分布——Battle 场 **~98%**
  分离实体压在 maxNeighbors=10 截断线上、bench-survivor 基准场 **~94%** 同样
  在线（真实密度远超 10，计数停在截断处）。两个结论：改引擎默认**必然漂移
  基准场**（playerHp 判据会变）；高密场景收紧截断近乎线性省时（98% 实体的
  扫描量直接减）。另发现默认组合 densityCap(12) > maxNeighbors(10) → 密度
  衰减分支永不触达（死参数组合，文档化在 Physics.cs）。
- **通道**（参数场景侧化，引擎默认不动 = 基准场零漂移）：`World::Separation()`
  访问器（InstallDefaultSystems 填指针）→ NativeApiVtable 尾追 `setSeparation`
  （<0 保持；先例二同款：不入 StateHash + 基准场零调用 = 零重录）→ C#
  `Lemon.Physics.Separation(radius?, strength?, maxNeighbors?, densityCap?)`
  （NativeApi.cs 表尾同步 + Physics.cs 新增）。
- **Battle 场落参**（RedVsBlue.cs Awake 一次性调用）：maxNeighbors/densityCap
  10/12→**6/6**——Separation 5.5→4.39ms。未再压到 4：边际收益递减且堆叠观感
  变紧，场景侧随时可调（热重载 → Stop/Play 重播即可生效）。

## 零漂移复验（全绿）

- engine-tests **33375 checks OK**（+3704 = 并行同构单测）；
- 金回放 m5b2 三档原样 `--replay` **mismatches=0**（sim mt/st + script，零重录）；
- bench-survivor 900 帧 **playerHp=275793 逐位一致** PASS（fps=82，万怪场顺带
  吃到 Rebuild 并行收益，上批 79/80）；
- editor-regression quick **6/6**（②渲染改动像素断言把守）。

## 余量登记（下轮候选，按当前大小序）

1. **AI 段 8.9ms**（认知修正后主靶）：Shooter 开火生成段串行（工厂调用 + 事件
   入队，~2.9 万射手按 view 序）+ 索敌环扫密度、Patrol 段；生成段可走 03 §4
   契约第 3 条骨架（并行查询收集 → 主线程按池序归并提交）进一步消墙钟。
2. **scene 段剩 6.0ms**：每视口 Extract ~0.6ms + Bake/Record ~1ms（实工作，
   SSBO 实例写）；渲染侧并行（渲染线程接入 jobs）与 Bake 多线程化为大改，单列。
3. sim 尖刺帧（max ~55ms，4 个/跑）：弹幕波峰 + 30-tick census 叠帧，观察项
   维持（上批同款指纹，非 Rebuild）。

万级口径（bench-sim mt 4.1ms / survivor 12.8ms 全帧）：60fps 判据余量充分；
五万场 40fps 距"三批累计 45-50fps"预测差一口气——缺口即上批对 Rebuild 的
误判部分（①实际收益 0.8ms 而非 ~6ms），如实落账。
