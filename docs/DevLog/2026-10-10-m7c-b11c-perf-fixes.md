# M7c 批⑪ b11c — 引擎评审中危性能 8 项（review 2026-10-09 #M4/M7/M8/M11/M12/M16/M17/M19）

2026-10-10 · [批⑪ 批文件](../Plans/M7c/2026-10-09-b11-engine-review-fixes.md) · 来源 [评审报告](../Reports/2026-10-09-engine-code-review.md) · D3（M17 pimpl 方案 A）+ D5（全清 8 项，M7/M8/M11/M12/M19 → M16 → M17 → M4）已追认

## 事件

b11c 当日清：性能两条线 8 项全落。执行序按 D5——热路径五项先行、装载线两项居中、M4 垫底收口。

### 热路径零分配线（5 项）

- **#M7 TargetBoard::DeclareTeams 差量同步**：稳态（声明与现有槽逐位一致）直接 return——TeamList 对象（list + Grid 五组 vector 容量）跨帧保活，内容清空本就由紧随的 Rebuild 负责；原每帧 clear()+重建把堆容量全释放、下帧从 0 翻倍扩容。声明变化才增删槽（仍被声明的槽 std::move 保容量；consumed 防同 id 重复匹配，重复声明多槽各收条目的原语义保持）。
- **#M8 NearestAny 全表桶双径**：TargetBoard 增 allGrid_（TeamList::Grid 同款），Rebuild 三条路径（串行/回退串行/并行）统一建桶——并行路径并入逐队建桶任务序列（最后一个留主线程不空等，原形态保持）；NearestAny < kMinList 走线性快径（存量场景逐位不变）、≥64 走环搜。等距平局语义差异同队版 Nearest（Grid 注释既定口径，m5b2 金回放先例）。
- **#M11 结构命令批量投递**：新三导出 `lemon_scripts_attach_batch/destroy_batch/detach_batch`（DomainManager 池化单命令零分配，参数指针在 UCO 线程栈上、Done.Wait() 同步等待期间存活 = PostBatchTick frames 同款前提）。C++ 侧 ApplyStructural 连续同类 op 段合并（段边界 flush = ops 原序保持；段内逐条序不变 = 与逐条投递逐位同构）；NotifyPendingDestroys 改**先收集后单次投递**——顺带把 L13（迭代安全前提陈述与代码不符）做实：OnDestroy 回调内 vtable 就地建实体/加组件已是现实，原「导出只投递命令不改池」前提靠池不相交侥幸。旧 Entry 缺批量导出 = null 挂空回退单实体版（挂空安全纪律）。ApplyStructural case1 原逐条「通知→入队」交错改整段「全通知→逐条入队」——scene.Destroy 只入队不突变池，序差无观察面（等价性论证落批文件）。
- **#M12 Behaviours 双层实体索引**：TypeSlot 增槽内 `ByEntity`（id→Instances 下标，移除平移同步修正——与 RemoveAt 平移同阶 = 保序删除理论下界，swap-remove 会乱 tick 序不可用）；全局 `s_slotsByEntity`（id→槽号集合，槽号生命周期稳定不漂移；List 池化复用，批量路径稳态零分配）。Detach 经索引 O(k)（原全类型槽×全实例线性扫）；销毁序 = 输入实体序 × 槽注册序（slots Sort 升序）——**OnDestroy 可观察序与原实现逐位同构**（回放确定性；按槽分组的替代序被否决即因于此）。DetachOne/GetInstance 顺带 O(1)。同实体同槽唯一不变量（Attach 红字拒绝双挂）保证每槽至多一条目。
- **#M19 流式声部块级 SPSC Read**：MixVoices 声部级 refill 缓冲（至多 256 帧，声部间复用——声部循环串行）；原子发布从每帧（48kHz × 4.8 万次/秒/声部）降到每 refill 一次（≤188 次/秒）。字节序与逐帧读逐位同序（生产者按帧写）= 离线混音哈希不变；环空时 Read 只 acquire 比较的便宜逐帧重试保持（pump 实时性）。提前终结（终点/failed/fade-stop）丢弃预读余量无观察面（一次性播放不再读、环游标前移无消费者）。

### 装载期索引线（2 项）

- **#M16 AssetIndex 等值哈希**：Open 尾部（sort 后 = 路径序）建 byGuid_ + byWholeId_（先登记者保留 = 原线性版语义）；FindByGuid/FindByWholeSpriteId O(N)→O(1)——ResolveSpriteRefs 每实体（SpriteByGuid/SpriteByWholeId 两法都在每实体路径）、AtlasStore 每图集条目的已核实热点全覆盖。FindBySpriteId（切片区间版）无运行时热消费方（复核口径）不动。评审证据里的 AssetGpuCache 调用方已不存在（该文件被 TextureStore 取代），热点按现网消费方重新核实。
- **#M17 PrefabCache 预解析（D3 方案 A）**：SceneArchive 新增 `ParsedEntityTree`（pimpl——shared_ptr 跨 TU 持不完整类型，类型擦除删除器在 SceneArchive.cpp 创建点捕获；nlohmann 红线 = StagedSceneBuild 同款）。Build 期 parse 一次入 Entry（坏档装载期红字跳过——原 Spawn 运行时 WARN+Null 语义前移，H2「坏档红字跳过」契约同口径）；Spawn 走 `LoadEntityTree(scene, parsed)` 零解析 + pos/prefabId 回链就地展开（InstantiateJson 的 string 版保留——GameEntry vtable 通道/EditorContext 独立消费方）。string/预解析两版共核 LoadEntityTreeCore（逻辑逐行未动）。~PrefabCache/Clear 出 .cpp（Entry 析构唯一完整类型点）。

### #M4 UploadTexture 批量重载（垫底，实测拍板）

RHI 增 `UploadTextures(entries, n)`（逐张建 staging + 全部录进单个命令缓冲一次 submit/waitIdle；单张版 = 批量1 共核）。**mac/MoltenVK 实测负收益，调用方回退单张**：svr-test dev 启动装载（111 张帧图）单张 1.31s / 全量批 1.42s / 分块 16 张 1.36s（各 3 跑取中位）——单一大提交的 Metal 编码器切换开销 > 省下的 waitIdle 次数。批量 API 留存（UploadTexture 内部消费 = 批量1），Windows dGPU 真机验证后启用（评审动机场景「dev 启动/首次导入/丢失重导」在 Windows 提交模型下大概率正收益）——登记 08 路线图批⑪ 注记。AtlasStore/TextureStore 调用方保持原单张形态（负收益不做）。

## 验证（机器面）

- 构建零警告（mac）。
- 单测 **34,752** OK（= b11b 后基线，行为面等价重写零回归）。
- script-tests **1,830** OK（M11/M12 批量与索引对 attach/destroy 流零扰动；GC 判据全绿）。
- ctest 4/4；回归 21/21（含 game/scene/template 三 smoke）；bench-survivor **fps=81**（≥76.5 门）。
- **实测数字（出口判据 4）**：
  - M7/M8：bench-sim 万实体 Flee 场（新增 `--flee`，万怪全 Flee、600 帧）——修复前 avg **117–128ms/帧** → 修复后 **9.3–9.4ms/帧**（**≈12.6×**；等距平局语义差异致个别帧击杀链略异 = Grid 环扫既定容许口径，基准用途只看性能）。
  - M4：svr-test dev 装载三态对比（见上，负收益拍板回退）。
  - M11/M12（机制数字）：批量销毁 N 带脚本实体 = N 次 Command 分配（各含 ManualResetEventSlim ≈100B+闭包）+ 2N 次跨线程上下文切换 → 1 次池化零分配 + 2 次；Detach 扫描 O(N×总实例数) → O(N×k)（k = 每实体脚本数 ≤8）。
  - M16：每实体 guid 查询 O(资产数) → O(1)（哈希）；M17：spawn 每发全量 Json::parse（11 个 prefab 均摊至 Build 期一次）→ 零解析；M19：流式声部原子发布 48,000 次/秒 → ≤188 次/秒（256 帧块）。

## 遗留登记

- M4 批量上传 Windows 真机验证（mac 负收益已实证，API 留存待验）→ 08 路线图批⑪ 注记。
- bench-sim `--flee` 压测面随样例长存（性能回归口径可用）。

## 追记：b11c review 轮（同日）

独立只读评审对八项逐路径复核（并发划分/指针生命周期/字节序等价/段序保持/索引生命周期），七项论证成立，抓出一处 HIGH 已修：

- **R-a1（M12，high，已修）**：`Behaviours.Detach` 升序 Sort + 倒序循环的方向叠加 = 实际**降序**执行——多槽实体 OnDestroy 序与原全扫实现（槽注册序**升序**）相反，OnDestroy 的 op 入队/事件推/native RNG 消耗序倒序 = 金回放确定性破坏（单槽实体不受影响，1,830 全绿未覆盖多槽销毁序）。修法：降序 Sort + 倒序循环 = 升序处理且每步摘尾（机制不变）。补 `TestMultiSlotDestroyOrder`（探针 typeId 23/24/25 表尾注册、事件号 1881(A)→1882(B) 到达序严格先 A 后 B + 干净收场；覆盖 M11 批量销毁段 + M12 索引化组合链；热重载清单断言 23→26）。
- **M8 等距平局口径交底**：`all_.size() ≥ 64` 且存在 Flee 查询的场景，等距平局从「池序靠前者」变「环扫先见者」——队版 Nearest 同款既定例外（m5b2 金回放先例）。回归金回放三档 mismatches=0 = 现有金档不含该组合的实证；**后续新录金档若含 ≥64 实体 + Flee，按此口径录制**。
- 评审确认的机制要点（记录判据）：M11 批量参数 = 成员 vector data()（Done.Wait 阻塞期主线程不可能并发改）；case1「全通知→逐条入队」无观察面（Destroy 仅打 tag 入队、DestroyQueueTag 不入组件注册表、vtable 无同步结构通道）；case5 收集/flush 双查槽窗口隔着的 OnDestroy 无法同步改 C++ 槽（op 队列下一帧应用、实体已亡、Alive 闸拒）；M12 的 OnDestroy 内无法重入 Attach/Detach（internal 唯一驱动方是 C++ 主线程，用户代码走 op 队列）——迭代中 List 追加不可达；M16 建表晚于 Open 内全部 id 派生/坏账循环、建表后 entries_ 无写点；M19 生产者帧对齐写 = 批读返回恒帧倍数无截断余量。

复验：构建零警告 / 单测 34,754 / script-tests **1,834**（+3 销毁序断言）/ ctest 4/4 / 回归 21/21（见下）。
