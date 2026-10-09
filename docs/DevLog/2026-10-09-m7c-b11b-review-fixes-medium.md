# M7c 批⑪ b11b — 引擎评审中危缺陷 14 项修复（review 2026-10-09 #M1–M22）

2026-10-09 · [批⑪ 批文件](../Plans/M7c/2026-10-09-b11-engine-review-fixes.md) · 来源 [评审报告](../Reports/2026-10-09-engine-code-review.md) · D2（M6 本批修）/ M9/M10 等按追认口径

## 事件

b11b 当日清：评审确认的中危缺陷面 14 项全修。开工门 = 复审采纳的 **ASAN（mac-san）提前门**——首跑即实报 M5（FxChannel 定长 memcpy 对 6 字节串 `"stale"` 越界读 15 字节，global-buffer-overflow），评审「ASAN 必报」预言实证；修复后门全绿（34,722 checks 零报告）。

### 坏数据 abort 面（5 项）

- **#M3 ImmediateSubmit DEVICE_LOST abort**：`RHI.cpp` ImmediateSubmit 改 bool 返回，submit/waitIdle 对 `VK_ERROR_DEVICE_LOST` 特判（红字 + 置 `lossPending` + 跳过断言）——不可就地 HandleDeviceLost（调用方仍持旧 allocator 的 staging 资源，重建 = 跨 allocator 释放），恢复由帧循环 `AcquireNextImage` 头部统一驱动（与 acquire 探明同路）；`UploadTexture` 早退短路批量上传循环，`IsDeviceLost` 并入 lossPending。守卫条件只看 lossPending 不看 deviceLost——HandleDeviceLost 重建期 recreateCallbacks 的重上传必须在新设备照常执行（实现期自查抓出的对偶坑）。
- **#M13 manifest 切片账对账**：`AssetIndex.cpp` 块账 sane 域收口循环并入 `sliceCount != gridCols*gridRows` 判定——陈旧 manifest 配新 .meta 网格时清块转全幅 + WARN（原会以 spriteId 0 触发 Atlas.cpp 保留号断言 release abort）。
- **#M14 LBF1 头域检**：`FontBake.cpp` LoadBakedFont 增 pageW/pageH 非零且 ≤16384（GPU maxImageDimension2D 域，LAT1 kAtlasMaxImageDim 同口径）——防 `(size_t)w*h*4` 2^64 回绕（0x80000000² ≡ 0）让小文件过字节对账后巨尺寸 CreateTexture。
- **#M15 stoull 抛异常**：`AssetIndex.cpp` ReadFontImporter outline 色改 `ParseHexU32`（整串消费 ≤8 位，HexToGuid 同款手写）——.meta 用户可手编，`"outline":[2,"zz"]` 原从 Open 全链无捕获 → boot/packager terminate。
- **#M21 EntityRef 前缀解析**：`SceneArchive.cpp` EntityRef 读档要求整串消费（endptr 落串尾）+ 首字符数字（拒前导空白/符号）——`"e5x"`/`"e 5"` 原静默指向实体 5，现红字 + 保持默认 null。

### 设备丢失边角（2 项）

- **#M1 持久离屏 RT WAR 竞态**：`BeginDynamicRendering` 获取屏障 srcStage/srcAccess 参数化，离屏 RT 传 FRAGMENT_SHADER+SHADER_READ——执行依赖跨 submit 边界有效，对帧 N-1 尾部 ImGui 在途采样建立依赖（原 TOP_OF_PIPE 空首作用域 = 偶发视口残影/撕裂 + 规范级 UB）；交换链路径默认值零改动。
- **#M2 RmlUi 几何句柄跨块释放**：`RmlUiBackend.cpp` GeoAlloc 增 `gen` 世代号（RecreateAfterLoss 递增），ReleaseGeometry 见旧世代直接丢弃句柄只 delete——设备丢失重建后翌帧 Rml 卸载旧文档回调的外来句柄，原会入延迟环在新 vblock 上 `vmaVirtualFree` = VMA TLSF 自由链表污染。

### 语义与纪律（7 项）

- **#M5 FxChannel 越界读源串**（ASAN 首门实报）：PopupTextEx 定长 `memcpy 15B` 改 `snprintf` 截断拷贝（RtUiChannel 同款）——伤害数字 "5"/"12" 主路径短于 15 字节，原恒越界读源尾部（标准级 UB，ASAN 首跑即报；非 ASAN 静默）。
- **#M9 lemon_scene_event 线程纪律**：Exports 改走 DomainManager.PostSceneEvent 池化域线程投递（PostUiEvents 同款）——sceneLoaded/sceneUnloaded/activeSceneChanged 的用户 ALC 委托与 await 续段原在 UCO 调用线程内联执行，与 UnloadScript 的 `alc.Unload()` 同线程，违反 ADR-010 D1「卸载线程从未触碰用户 ALC」；RunPooled 同步等待保 ADR-017 协议⑤时序（sceneLoaded 先于新场脚本同帧 Start/Update），SceneManager 头注「域线程同步续跑」由假变真。
- **#M10 Events 迭代活表 + 静默吞**：DispatchPackets 改 for 索引迭代（容量重查容忍尾部增删，零分配保 GC 纪律）——handler 内 Subscribe/Unsubscribe 原使 foreach 枚举器抛 InvalidOperationException，逃逸出 per-handler try 后被 RunPooled 静默吞掉且当批剩余事件全丢；PostBatchTick/PostBatchEvents 补 Error 检查红字（原只有 PostUiEvents 有）。
- **#M18 音频锁内末引用释放**：AudioEngine Tick 退役 stream 挪 `retire` 向量（声明先于 lock_guard → 析构后于解锁）——一次性流式播完的必经路径原在持 impl_->mtx 时 fclose，DataCallback 等同一把锁，磁盘 IO 高压可逼近 ~10ms 回调期限；PlayLocked feed 改引用传入（拒绝路径所有权留调用方锁外析构）+ outRetire 接槽复用弃养旧流。Shutdown 路径不动（设备已 uninit 无回调竞争）。
- **#M20 UI Init 失败半初始化态**：UiSubsystem::Init 三失败分支各自正确拆净（backend 失败 = impl_.reset；Rml::Initialise 失败 = 不可调 Rml::Shutdown 只拆 backend；CreateContext 失败 = 完整 Shutdown 序）——原保留 impl_ 会让后续显式/析构防御 Shutdown 调从未 Initialise 的 Rml::Shutdown（debug 断言中止、release 空指针 UB），「UI 缺席降级运行」契约变崩溃。
- **#M22 lemon-game 失焦卡键**：InputCollector 补 FOCUS_LOST/MINIMIZED 分支清 keyDown + leftDown——gameplay 输入直读该表，Window.cpp 的同款修复只清 m->keys（IsKeyDown 面）而 lemon-game 零调用该接口 = 修复落在不读的路径上。
- **#M6 投射物扫掠防穿透**（D2 追认：本批修）：HitboxSystem 命中环改阈值门控子步进——`|v|·dt > 2×hitRadius` 时沿本帧位移段（prevPos = pos - v·dt）取 ≤64 个采样圆心、间距 ≤ 2×hitRadius 无缝覆盖，慢弹 K=1 原单点零扰动；段首重采样由命中记忆/iFrames 去重。TriggerSystem（probe=4 族，阈速 4320）未随动——评审自认较次要，登记遗留。

### H3 专项 wipe 用例（b11a 承诺兑现）

smoke-uirml 增专用夹具（wipehost/wipebox/wtpl，`display:none` 零像素扰动，cards 契约断言面不动）+ 第二局帧窗 220–240 编排：形态一 SetInnerRml 打宿主（子树含容器+tpl+proto 全灭，重声明同构标记）、形态二打 ui-template 自身（proto 灭、tpl 元素存续 = H3-1 残洞判定面）。断言三段：建容器后行数 1（seed）/ 打后缓存读数 -1（条目真删非悬挂 evict）/ 两形态重建各克隆成活 1 行。裁决并入终帧 verdict + `h3(...)` 打印位。

## 验证（机器面）

- ASAN（mac-san，address+undefined）：开工门 + 终门两跑，34,722 checks 零报告（M5 修复实证；SceneTests 换场预算天花板断言 sanitizer 构建豁免——插桩 2-4x 减速推过任何固定墙钟天花板，终端性/孤儿/计数断言不受影响，非机制回归）。
- 构建零警告（mac；顺手清 GameFx.cpp 既存 unused lineH——批⑪出口判据要求）。
- 单测 **34,751** OK（b11a 后 34,723 + 28）：新增 `TestProjectileSweepAntiTunnel`（GameplayTests，M6：8192px/s 弹 136px 步长对 200px 处目标 tick2 扫掠命中恰一击、非穿透弹即毁）、`TestManifestSliceCountMismatch`（M13：4×4 网格配 count=8 陈旧账清块转全幅、本体号保留）、`TestFontImporterBadOutline`（M15：`"zz"` 色 boot 不 terminate + 默认色保持 + 姊妹字段正常）、`TestBadEntityRefFormatRejected`（EcsTests，M21：e1x/e空格1/e-1 三变体保持默认 null + 合法 eN 对照组往返非空）、TestFontBakeGolden 增 M14 三坏头分支（零维/2^31 回绕积 0 截断载荷/超 16384 域）。
- script-tests **1,830** OK（M9/M10 改动零回归——池化投递同步等待保时序、for 索引迭代对非变异 handler 语义不变）。
- ctest 4/4。
- smoke-uirml `--validate` 脚本+无脚本双模式 **=> OK**（h3 seed=1 evict=1 host=1 tpl=1；既有 470 帧编排零扰动：items=2/1、contract=1、d8/evict3 全绿）。
- 回归 21 步：首轮 20/21（`--scene reopen` overlay sel=10<20 噪声红——隔离重跑 3/3 全绿 sel=169；09 §9 口径归因），全量复跑 21/21。bench fps=84（≥76.5 基线门）。

## 遗留登记

- M3 的 lossPending → 恢复链路无自动化测试（需真实设备丢失中线上传；SimulateDeviceLoss 钩子可在后续批做合成验收）；代码面经走查 + 全 smoke 绿。
- M18 的锁外释放时序无直接断言（行为等价重构，现网 AudioTests 全绿；时序观测需设备回调延迟探针，性价比低）。
- M22 无单测（InputCollector 在 lemon-game 运行时 TU，SDL 事件合成不值当；game-smoke 步把守）。
- TriggerSystem 扫掠（probe=4 族）登记待 profile 证据再动。
- M7/M8/M11/M12/M16/M17/M19/M4（中危性能 8 项）归 b11c；L 类低危归 b11d。
