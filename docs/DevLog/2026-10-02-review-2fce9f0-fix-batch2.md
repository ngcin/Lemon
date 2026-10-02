# review 2026-10-02（基线 2fce9f0）修复批 ②——medium 大扫除 + low 快赢

> 范围：[评审报告](../Reports/2026-10-02-code-review-2fce9f0.md) 批①（[批①条目](./2026-10-02-review-2fce9f0-fix-batch.md)）
> 之后的剩余项中，**架构级/需设计决策的除外**（见文末移交）——本批共 **53 项**：
> medium 20 条（#7/#9/#14/#15/#16/#17/#18/#23/#25/#27/#28/#31/#32/#33/#34/#35/#36/#38/#39/#42）
> + low 33 条（#45/#51/#52/#54/#55/#56/#57/#58/#59/#65/#66/#67/#72/#73/#74/#76/#77/#78/#79/
> #83/#86/#88/#89/#90/#91/#92/#93/#94/#95/#96/#99/#101/#102）。
> 基线 = 批① 提交 11204ba。

## 修复明细

### C# 桥（域线程契约 + 异常隔离）

- **#14 UI 事件派发归域线程**：`lemon_ui_events_dispatch` 原在引擎管线线程内联执行用户
  ALC 代码（UI.Events 订阅 handler），击穿 ADR-010 D1「卸载线程从未触碰 ALC」的换装前提、
  与游戏事件 PostBatchEvents 线程契约不对称。改 DomainManager 新增池化 `PostUiEvents`
  （UiEventBody/单槽命令，与 PostBatchEvents 同款零分配形态），导出侧投递——同帧同序
  （Post 等待完成），仅执行线程归位。
- **#15 装配异常不再静默**：两半——①Behaviours.Attach 的 `slot.Factory()`（用户构造器）
  补 try/catch 红字跳过（C++ 侧槽已写、实例未挂 = 脚本哑火必须可见；原是唯一无护栏
  生命周期）；②DomainManager.PostBatch 的 cmd.Error 原直接丢弃（注释宣称「Batch 内部
  已做异常隔离」对 attach 路径不成立），改红字不重抛。
- **#16 behaviours_list UTF-8**：类型名表改 `Encoding.UTF8`（ASCII 逐字节不变）；原
  `(byte)char` Latin-1 直出非 ASCII 类名 = 非法 UTF-8 且经编辑器落 .scene className 持久键。
- **#17 ExecutionOrder 数值全序**：RebuildOrder 原「Order==0/非0 两桶」→ 索引稳定排序
  `(Order, 注册序)`（负 Order 早于默认桶、非零 Order 间数值比较；全仓零使用者，零行为风险）。
- **#18 lemon_dm_tick 异常护栏**：唯一执行用户代码却无 try/catch 的 UCO 导出（Post 重抛
  逃逸 = coreclr abort）——补护栏返回 NaN 哨兵；DomainManager 硬编码 "TestScript" 的
  测试耦合以注释登记（解耦需动 load 协议，随 M7a 宿主抽象）。
- **#57 幂等 load 路径校验**：已装载时对不同 assemblyPath 的重复 load 原静默返回成功
  （调用方无从分辨装了请求的域还是维持旧域）——红字拒绝（UnloadScript/ReloadScript 清
  s_loadedPath）。
- **#58 同名类响亮拒绝**：类型身份 = 裸 t.Name，跨命名空间同名类并存会让 TypeIdOf 静默
  路由到先注册者——Register 时检测冲突红字拒第二个（同类型重复注册保持幂等）。
- **#59 Anim.ClipId TryParse**：原 ulong.Parse 裸抛——改 TryParse + 红字一次返回 0
  （GuidOf/ResolveClip/Audio 同口径）。
- **#51 typeId 超 255 响亮丢弃**：SceneOps.AttachScript/DetachScript 的 CompId 是 byte，
  原 `(byte)typeId` 静默截断会挂错行为类——入队前拒绝。

### ScriptHost C++（死代码退役 + 不变量成文）

- **#54**：dmUnload_/batchTickFn_ 两死必需导出退役（卸载实际走 dmReload_ 整域重建、帧执行
  走 scriptsTickFn_ 合一通道）——不再解析也不再作 Initialize 必需闸（旧 Entry 删导出不再
  无谓失败）。
- **#55**：scriptsNeedTick_ 只读不写（「无实例早退」从未接线）——删死标志保诚实；空场景
  每帧 ~66µs 托管往返的优化登记 M7a。
- **#52**：NotifyPendingDestroys 循环内跑托管 OnDestroy 的迭代器不变量成文（当前导出全为
  投递式；加同步 native 改池导出前必须先收集再回调）。
- **#56**：NativeSpawnSprite 注释改口——就地建实体的可见性比命令缓冲晚一拍是有意偏差，
  不再声称「两路一致」。

### 渲染内核

- **#7 DestroyTexture 槽位兜底**：与 DestroyBuffer 对称——Impl 增 `boundTextureIds[256]`
  记账（BindTextureToSlot 写、CreateBindless/设备丢失复位），DestroyTexture 把仍指向被销毁
  纹理的槽改写为惰性 dummy 黑 1×1（CreateTexture/UploadTexture 全管线 = 清黑确定值），
  防陈旧渲染采样已销毁 view + WARN 可见；dummy 随 DestroyAllGpuState 销毁。注意改写先于
  取 `textures[]` 引用（DummyView 建纹理会扩容向量）。冒烟实证：smoke-uirml 热重载 3 次、
  smoke-template 换项目 2×3 页各触发一次，零刷屏。
- **#9 粒子剔除含半径**：中心点测试 + 硬编码 64px margin → 半对角线半径（生命期较大端
  尺寸 ×√2/2，RenderableManager 同口径）——大光晕粒子屏幕边缘整颗突失修复。分桶搬运与
  Renderable 的去重为纯重构，登记后续。
- **#45 桶内补排稳定化**：sortKey 末 24 位创建序 16.7M 次后回绕产生并列，非稳定 sort
  帧间可变（alpha 混合闪跳）——改 std::stable_sort（并列按池序确定）。

### UI 子系统

- **#23 字体链降级不再禁用层**：系统字体链（macOS 路径）缺席时原 return false 整个 UI
  层禁用——主/兜底倒置（THIRD_PARTY 登记意图 = 引擎 Noto 主、系统链兜底）；改红字降级，
  宿主（编辑器 Init 后即注册引擎 Noto）照常给字。macOS 行为零变化（smoke font=Noto 复验）。
- **#72 模板换名响亮**：容器态首条 SetItems 定型，后续携带不同 templateName 原被静默忽略
  ——ContainerState 记名、冲突 ContractFail（ADR-014 M2 响亮失败纪律）。
- **#73 dp 参考高归零复位**：SetDpReferenceHeight(0) 原不重置 dpRatio——补复位 + 喂 ctx。
- **#74 死状态位删除**：shownDuringPlay 只写不读（清场判据已升级 origin）——字段与六处
  赋值全删。

### 系统与基座

- **#65 Animator 回绕护栏**：`while (time >= period) time -= period` 对 inf 整帧死循环
  ——改 fmod 一次到位 + 有限性/正周期护栏（正常值位级同值，回放零漂移）。
- **#66 SpatialHash 头卫生**：整含 ECS/Scene.h + CoreComponents.h 把 entt.hpp 传递扩散给
  Physics2D 全部消费者——改前置声明（Scene/Transform2D），完整收口仍待 #43 Scene.h pimpl。
- **#67 Systems.h 头注计数**：17 系统（13+4 占位）→ 20 系统；cell 64px 注释与 kCell=32 对齐。

### 编辑器壳

- **#25 DPI 双重缩放**：ApplyScale 原先 ApplyTheme(k) 全量乘 k 再 ScaleAllSizes(k) 追乘 =
  Retina 密度 2.0 下间距/圆角 = 设计值 ×4——改 ApplyTheme(1.0) 铺设计值 + ScaleAllSizes
  一道。smoke 探针（运行时测量 rect）自适应，全链复验绿。
- **#27 相机跟随缓存**：原每帧全池线性扫（Scene::Each 无早退），万实体场景每帧上万迭代
  ——三目标句柄缓存 + 逐帧轻校验（活着/有 Transform/判据成立），失效才重扫；校验语义与
  每帧重扫一致（换 Play 世界/目标死亡/换 tag 即失校验）。
- **#28 裁决副作用压轴**：SmokeTplVerdict 内联的第二项目检查（OpenProjectPipeline 换走
  整个会话：停双 watcher/复位图集/卸全部文档/重编译 dotnet）拆为独立
  `SmokeTplSecondProjectCheck`，调至全部只读裁决（FinalVerdict/overlay/SmokeUirmlVerdict）
  之后——原布局下这些裁决全在「项目已换走」的会话上执行，手动组合
  `--smoke-template --smoke-uirml` 时后者必 FAIL。冒烟输出顺序实证 + ids identical OK。
- **#76 Edit 态音频 Tick**：audio_.Tick 仅 Play 分支驱动——Edit 态试听的 done 声部回收/
  静音降级游标推进（同 clip 重触发节流窗过期）死路；else 分支补调。
- **#77** OpenSceneRecovery 整体替换场景补 undo_.Clear()（与 OpenScene/NewScene 同款）。
- **#78** 布局名注入防御：SaveLayoutIni/删除路径过 ValidLayoutName（拒路径分隔符与
  "."/".."，CJK 放行）——自由文本不再能越出布局目录写/删 .ini。
- **#79** Prefab 实例化日志改记本次 spawn 数（AliveCount 差分），不再拿全场景计数冒充。

### 资产管线

- **#31 Rescan 内容哈希增量**：hashCache_（路径→{mtime,size,hash}）门控——mtime+size 未变
  复用上轮哈希，原每轮全项目全文件重读（watcher 500ms 轮询下任一改动 = 全量同步 IO 卡
  UI 线程）；消失文件/重命名/换项目三处缓存出表。.meta 重解析保留（文件 ~200B，热改
  即生效语义依赖每轮重读）。
- **#32 项目名白名单**：名字拼进 popen 双引号串（POSIX sh 展开 $(...)/反引号 = 命令注入）
  与手写 project.lemon JSON——IsValidProjectName 改白名单（ASCII 字母数字/-/_/./空格 +
  ≥0x80 字节即 CJK 放行，其余 ASCII 全拒——shell/JSON 元字符恰好全在 ASCII）。
- **#33 新建 .meta 原子写**：裸 ofstream trunc → WriteFileAtomic + 失败红字（原崩溃留半截
  JSON → 解析 discarded → guid 重发 → 场景引用静默断链且永不自愈）。
- **#86** Rename 的 .meta 随行失败显式红字（原静默忽略 = guid 重发断链零可见）。
- **#88** HexToGuid 收长度：恰 16 位 hex 才合法（短串原按截断值放行错绑资产）。
- **#89** FindBySpriteId/SpriteIdRegistered 过滤 missing（与 FindClipByLowId/EntriesInDir
  同口径——Remove 后同帧窗口不再报已登记）。
- **#90** ControllerEdit AppendFloat 非整值 %g 最短表示（原 %.1f：阈值 0.15 保存后漂移成
  0.1/0.2；6 位有效数字，>6 位阈值仍近似——登记）。
- **#96** 模板 yami 素材拷贝检查 ec（与音频拷贝 #26 修同款：失败红字断生成，不产出缺件
  「成功」模板）。

### 面板/工具

- **#34** 三选一卡片浮窗坐标空间：gv 子窗口局部坐标 + SetNextWindowPos(Always) = 面板恒落
  编辑器左上方向——改 gvMin + 局部偏移的屏幕坐标。
- **#35** 层级搜索递归全子树（原只看一层：A→B→C→D 搜 D 永不可见）；深度帽 64 防坏档环链。
- **#36** 胶片带拖拽统一「插到目标帧前」：src<dst 原 insert(i) 实为插到目标之后（拖到相邻
  下一帧表现为两帧交换）且选中位按回退算——注释/实现/高亮三者矛盾；erase 后目标帧在 i-1，
  统一 insert(dst)/sel=dst。
- **#38** .tab 错误红字持久到下次提交成功（原渲染当帧即 clear = 16ms 后消失）。
- **#39 ThumbCache 借用泄漏**：借用 AssetGpuCache 页的条目原仍滞留待解码队列——Tick 重复
  解码+上传覆盖借用纹理，且 borrowed 标记使逐出/清场跳过销毁 = VkImage+描述符集随目录
  浏览无上界泄漏。改：借用判定先于入队（借成不入队）+ Tick 双保险跳过 borrowed。
- **#93** 视口拖放摘根入结构轨（与 Hierarchy 空白拖放同操作同轨，Ctrl+Z 行为一致）。
- **#94** ScriptBox 槽位换绑（combo 换类型）入结构轨——ScriptBox 不入 ComponentRegistry
  属性轨够不到，相邻挂载/移除都有 Undo。
- **#95 换项目旧代纹理释放**：ProceduralAtlas 新增 ReleaseGpu（调色板/图标页成员句柄 +
  字体页经 registry 取回——其句柄只在 registry，须 Reset 前）——原 Reset+Build 覆盖句柄
  ≠ 释放资源，每次项目切换净漏 3 纹理；采样器无 DestroySampler API（对象轻量）登记。
- **#99** ThumbCache Destroy 逐项 WaitIdle → Clear 一次/批量逐出一次。
- **#91** ProfilerPanel BeginTable 失败早退补 End()（窗口栈失衡地雷：当前 flags 不可达，
  加滚动 flag 即触发）——改 if 包裹。
- **#92** 「GPU 列」死控件接线（门控 GPU 时间行；原只写不读）。
- **#83** smoke-template diag gems/mobs 真·峰值（窗口计数取 max 后清窗；原 60 帧累加和冒充）。

### 文档

- **#42** 01 §1 铁律 3 加修订注记：「只有两条通道」→ 通道族（vtable 46 槽/结构命令/UI
  ops/生命周期表），不变式（禁逐实体逐帧跨界）保留。
- **#101** 01 §5 目录树漂移加修订注记（Math→Core、Input→ECS、Navigation/Assets 未建、
  Renderer 平铺、Scripting/dotnet、Platform/Ui 新增等），M7a 批⑧ 重绘时删除。
- **#102** 里程碑指涉随迁（2026-09-30 重排后遗留）：ScriptHost.h 存档钩子、UiSubsystem.h
  resolver、World.h RtUi 槽/存档通道四处 M8→M7a（OS 用户目录位仍归 M8）。

## 验证

- `cmake --build build/mac` 全目标 ✓；`ctest` **3/3**（engine-tests **34071** / imgui-
  isolation / script-tests **1775**——C# 侧改动后全绿）✓。
- `rhi-smoke --frames 60 --device-loss 30`：`deviceLoss=recovered` ✓（#7 记账表在丢失
  复位路径实跑）。
- `smoke-uirml --frames 500`：`=> OK`（#14 域线程化后事件链 ev/contract 探针照常；#23
  font=Noto 不变；#72/#73/#74 所在层）✓。
- `smoke-template --frames 3200`：主链全绿（hud/saveLoad/wave/kills=180/cards/death/
  flow 全 YES、saves OK、uidoc=6、aud 7/2/3 OK）；**second-project ids identical => OK
  且输出序在 overlay/uirml 之后**（#28 实证）；diag gems(peak)=8 mobs(peak)=15（#83 真
  峰值）。末尾 overlay-visible FAIL（sel=0/handle=0/label=0）——数值与批①基线逐位相同
  （本终端会话环境既有，评审 #28 登记项；本批把项目切换移到 overlay 之后仍未转绿 = 根因
  确系帧循环已停/capture 暂存前提而非项目切换）。
- `smoke-audio`（临时夹具）：`entries=1 baked=1 voices=1/0 => OK` ✓。
- `smoke-anim --frames 200`：FAIL 形态与批① stash 基线相同（本终端会话环境既有的 broad
  probe NOs；帧序链 multiadd count/order=YES 在位）——非本批回归；#36 拖拽语义属真人
  验收面。
- 金回放：零 vtable 槽/组件 id/系统序变动；#45/#65/#9 均为渲染面或位级等值改写，零重录
  预期成立（17 步回归待 GUI 会话跑全量）。

## 提交前自查轮（2026-10-03）

对工作树全量 diff 过了一遍，发现并当场修掉三处：

1. **#65 漏修两处同构回绕**（真 bug）：PingPong 分支（`while (time >= totalPP)`）
   与 loop 分支（`while (time >= total)`）两处仍是逐减 while——inf 挂起面与 M2 分支
   相同；且 `total==0`（空帧表 clip + loop）时 `while (time >= 0)` 恒真 = 无条件死
   循环。两处补同款 fmod + 有限性护栏（wrapped 语义保持：一次判定置位）。
2. **#7 兜底日志降级**：热重导入尺寸变化的次序是「先销毁旧纹理、后绑新纹理」
   （AssetGpuCache.cpp:86-90）——销毁时槽仍指旧纹理，WARN 在每次合法重导入/换项目
   误报（警告级刷屏会训练用户忽略警告）。降 LEMON_LOG。
3. **#32 拒绝文案对齐**：白名单化后提示仍说「禁止路径分隔符/: 与 ..」——改白名单口径。

自查确认无问题的关键点：#7 的 DummyView 引用失效顺序（先槽兜底后取 `textures[]`
引用）/deviceLost 门/dummyTex 三处复位；#14 池化单槽的重入安全（域线程 inline +
UiEvtBody 参数按值读取）；#25 只经 ApplyScale 一处铺样式；#27 双相机场景跟随缓存
不换新目标（与原「池序第一」语义的已知差异，登记真人验收）；#31 缓存三处出表
（消失/重命名/换项目）+ HashFile 失败不缓存；#36 拖拽双向推演自洽；#57 路径比较
（调用方恒传绝对路径）；#83 peak 字段无其他消费者。

修后复验：ctest 3/3 + engine-tests 34071 ✓；rhi-smoke `deviceLoss=recovered` ✓；
smoke-template 主链全绿 + second-project OK（dummy 改写日志现为 info 级）✓。

## 遗留与移交（评审剩余项归置）

- **medium 剩 10 条（架构级/需设计决策）**：#8 异步纹理上传（02 §5 缺口）、#12 ScriptBox
  className 24B 截断（加宽涉 C# 镜像布局同步）、#13 占位解析 O(n²)（休眠——占位 id 零生产
  调用方）、#26/#37 命令式结构轨（ADR-009 重设计）、#29 冒烟四段重复去重（纯重构）、
  #40 标签遍历预算（与提取合链方案）、#41 资产链下沉（M7a 批② 本体）、#43 EnTT 头 pimpl
  化（大重构 + 机器守卫）、#44 帧编排下沉（M7a 本体）。
- **low 剩 22 条**：#46/#48/#49/#50（序列化与封装注释/防线）、#53（CoreCLRHost 失败路径
  清理）、#61（timeScale=0 冻结不彻底——涉 RNG 消费序，金回放风险需先查金档覆盖面）、
  #63（Pool<T> 死模板删/接线）、#64（Chase 回源 registry 读）、#68（loop 秒 UB 转换）、
  #69（流式环批化）、#70/#71（音频死字段/Paused getter 需 vtable 桥）、#75（IME 焦点
  切换）、#80/#81/#82/#84/#85（冒烟框架卫生）、#97（日志环）、#98（RT 重建节流）、
  #100（模板 GUID C# 单源化）、#103（RtUi 双通道退役，M8）。
- #90 的 >6 位有效数字阈值精度、#95 的采样器释放（需 DestroySampler API）随各自模块
  后续批。
