# ADR-017：LoadScene 档2——场景管理与换场架构（单 registry 成员标签，Unity 语义对齐）

- 日期：2026-10-08（M7c 批⑤ 设计定案）
- 状态：**已采纳**（**D1–D8 已拍板 2026-10-08 用户：均按建议**，含两处讨论改判——D3 LoadSceneAsync 由砍单改判进计划（用户论证：TD/ARPG 关卡初始化重（NPC/商店/事件）+ 大地图无进度条体验差，"现在不做，后面还是要增加"）；D2 Additive 维持 v1 预留（用户问询塔防/守卫剑阁副本形态 → 结论不需要，ARPG 无缝大世界才是真消费者））
- 影响：[08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md) §2 M6b 行 + 砍单清单第 8 条（转正式排期，本 ADR 即开工前置）、[M6b.md](../Plans/M6b/M6b.md) 出口判据⑥（**债清**：2026-09-30 收官时"评估结论落 ADR"挂账至今，本 ADR 即清偿）、[M7c.md](../Plans/M7c/M7c.md) 批⑤–⑩、[03-ECS-Runtime](../EngineDesign/03-ECS-Runtime.md) §2（Scene 语义修订注记——随批⑥ 落地同步）、[04-CSharp-Scripting](../EngineDesign/04-CSharp-Scripting.md) §2（SceneManager API 面——随批⑦ 落地同步）

## 背景

1. **分档史**：2026-09-24 M6 replan 发现产品壳完全无排期（C# 无 LoadScene、引擎无场景切换机制），拆两档——档1 = 单场景状态机零引擎改动（M6b 批③d-2 落地，svr-test 全流程 + 真人验收过）；档2 = LoadScene 独立评估（**档1 不够用再开工，开工前置 ADR**）。
2. **档1 自律税实证**（svr-test [GameFlow.cs](../../demo/svr-test/Game/GameFlow.cs) 363 行）：12 tag 手工清场清单（新实体类型漏登记 = 重开漏怪）/ `SweepArmed↔SweepObserved` 握手（SceneOps 次帧应用的时序细节漏给游戏侧）/ EnTT 逆序 Show 手工 re-Show / `Audio.Paused` 残留静音 bug（review 2026-10-02 #2 才抓出）——四者根源同构：**清场语义靠游戏自觉，非引擎承诺**。
3. **需求升级（2026-10-08 用户）**：幸存者类实际多关卡（草原/森林/火山）+ 武器收集页/成就等多 UI 场景——单场景局限成立，档1 不再够用；LoadScene 需参考 Unity 实现含命名；需 DontDestroyOnLoad。
4. **价值主张**：档2 不是"多一个 API"，而是把档1 的清场自律税变成引擎保证——换场后新场景拿到**机器可证明的干净世界**（smoke 断言面：旧场实体归零、DDOL 幸存者精确清单）。

## 开工前核证（2026-10-08 会话考古）

引擎现状（决定成本的三件套 + 支撑件）：

| # | 事实 | 位置 |
|---|---|---|
| 1 | **SDK 生命周期零缺口**：Awake/OnEnable/Start/Update/LateUpdate/OnDestroy 全有；OnDestroy 恰好一次语义（F-08.2，实体级 destroyNotified 防双发，销毁/Detach/换域三路共用） | `GameObject.cs:136-141`、`ScriptBox.h:24-26` |
| 2 | **托管实例在 C# 侧**（Behaviours 表按 (typeId, 实体句柄) 键控）；实体句柄 = registry 局部索引（两 registry 各有"entity 1"，句柄冲突无场景域限定） | `Behaviours.cs:158`、`Scene.h:118-119` |
| 3 | **系统管线/渲染提取/C# tick 全吃单 `Scene&`** | `SystemPipeline.h:46`、`SceneExtractor.h:28` |
| 4 | World 已有 `scenes_` + `active_` + CreateScene/SetActiveScene | `World.h:244-245` |
| 5 | UiDocOrigin {Scene, CSharp, Edit} + UnloadDocument(origin 过滤)——R10 前瞻（"LoadScene 档2 前瞻不堵死"）即为此路预留 | `UiSubsystem.h:38` |
| 6 | SceneArchive::Load = 清空重建；EntityRef "eN" 索引 remap；跨子树引用置空先例（SaveEntityTree） | `SceneArchive.h` |
| 7 | **运行时贴图 eager 全量预载**（GameEntry 装配 = Init→Open→LoadAll）——"地图资源大"的成本在启动不在换场 | `TextureStore.h:34` |
| 8 | JobSystem 有单线程诊断档（threadCount=1 就地执行）——未来资产阶段上 worker 时回放口径不受污染 | `JobSystem.h` |
| 9 | vtable 49 槽（M7c 批① 后）；尾加零重排 = 金回放零重录纪律（ADR-016 同款先例） | — |

Unity 语义核查（镜像基准，四源实证）：同步 `LoadScene` 本为**下一帧装载**（semi-asynchronous，官方文档）；装载序 **Awake/OnEnable → sceneLoaded 回调 → Start**（论坛 + StackExchange 实证）；`DontDestroyOnLoad` **仅根生效**，标记子对象 = 整棵根树搬入 DDOL 场景（CSDN 实证）。

## 决策

### D1 架构：单 registry + SceneMembership（核心拍板）

**"场景"从执行/数据边界降级为数据分组**：运行时 Play 世界单 registry，实体带 `SceneMembership`（场景句柄 + DDOL 位）；场景档案（handle/name/path/isLoaded）住 World。

**多 registry（Unity 字面形态）被否决的三硬伤**：

- 句柄 registry 局部 → C# 实例表 / EntityRef 序列化 / 提取层 index→槽映射全要加"场景域"限定（全局唯一句柄空间是现状隐含契约）；
- DDOL 迁移 = 跨 registry 重建实体 + 逐类型拷组件 + **C# 实例表重键**——实例与实体脱钩 = 幽灵脚本温床（最易出静默 bug 处）；
- 管线/提取/tick 全改多场景循环，且循环序（先跑完 A 场所有系统还是按系统交错）无干净答案——Unity additive 的共享世界语义恰好给不出来。

**单 registry 的兑现**：

- **DDOL = 改标签，O(1)**：实体不死、句柄不变、ScriptBox 不动、C# 实例天然续命、EntityRef 天然有效——正是 Unity DDOL"对象不死"的语义本体，比 Unity 内部真搬家还便宜；
- **tick/提取零改动**：本来就是全 registry 遍历，DDOL 实体照常 Update/照常渲染（Unity 同款）；
- **换场 = 帧边界销毁 membership==旧场实体**：两阶段销毁 / OnDestroy 恰好一次 / tween 随实体自清全部现成；
- **物理共享单 broadphase** = Unity additive 共享物理世界语义（跨场景对象可碰撞，Unity 行为）；
- **Additive 后手便宜**：多组共存不互斥 + UnloadSceneAsync + SetActiveScene 落点（~1.5 天）。

**语义细则**：

- membership 为运行时组件，**不入 .scene 序列化**（装载/实例化单点打标；漏标孤组 = smoke 断言面，零容忍）；
- 编辑器编辑态单组 = 全 registry（现状完全兼容）；Scene（entt 封装）类保留，编辑器 scene_/playScene_ 双 registry 现状不动，EnterPlay/StopPlay 归位复用（StopPlay 整弃 play registry 含 DDOL 组——Unity 编辑器 Stop 同款）；
- StateHash 以分组迭代体现（分组序固定 `[DDOL, …additive…, active]`）；membership 入不入哈希流的等价口径批⑥ 批文件定案（金回放零重录预期成立，若需重录一次则落账）；
- 实例化落点：`Instantiate.Prefab` → membership = active 场景句柄。

### D2 Additive：v1 预留不实现 + 用途表落账

Unity 社区五类真实用途对 Lemon 的价值评估：

| 用途 | 说明 | 对 Lemon |
|---|---|---|
| 常驻管理者场景（DDOL 替代品） | Unity 团队嫌 DDOL 场景编辑器里隐藏不可编辑 | **弱**——Lemon DDOL 组编辑器可见（批⑩），且 C# 静态类本就跨场存活 |
| 大世界流式分区 | 开放世界切块随玩家流入流出，无缝大地图 | **强，ARPG 远期**——真做连续无缝大世界才需要 |
| UI 覆盖场景 | HUD/菜单做成叠在玩法上的场景 | **无**——UIDocument/origin 通道粒度更细 |
| 巨型地图分块装载 | 摊平装载峰值 | **弱**——LoadSceneAsync 分帧预算后动机基本消失 |
| 光照/环境按场景 | 3D 事项 | 2D 无关 |

**塔防/守卫剑阁结论（用户问询的裁定）**：守卫剑阁本体 = 单图 + 触发器刷波 + 同图 NPC 商店——单场景形态；多关卡选图 = Single + 读条；**副本（dungeon）= 离散关卡切换 = Single + LoadSceneAsync + 进度条（WoW/Diablo 式），Additive 帮不上忙**。真上场时点仅两个：ARPG 连续无缝大世界 / 大厅在副本期间不卸载打完秒回（有读条的话 Single 也成立）。

**实现形态（后手）**：`LoadSceneMode.Additive` 枚举 v1 即入 API 面（Unity 对齐），调用 = 红字"未实现（ADR-017 D2 预留）"；接通 = 多组共存 + UnloadSceneAsync + active 语义 + 编辑器多场景组 UX。

### D3 LoadSceneAsync：统一管线（讨论改判：砍单 → 进计划，批⑧）

**一条管线两个门面**：同步 `LoadScene` = "预算不限、下帧边界立即激活"的特例（Unity 同步版本就是下一帧装载，语义同构）；`LoadSceneAsync` = 同协议 + 分帧预算 + 激活门 + 进度上报。**不存在两套装载代码；回放/smoke 走同步路径。**

- **分帧状态机**（域线程，每帧限预算，开工实测定量级 ~4ms）：`Parse（JSON）→ Build（实体+组件构造）→ Resolve（脚本槽/GUID 归一/SpriteRef）→ Assets（占位 no-op：贴图 eager 预载现状下已常驻；M9 tilemap 大数据/未来按需装载接入此位）→ ActivationGate → Essential 段换场（D1 协议原样）`。工作小一帧走完 = 与同步路径同帧效。
- **AsyncSceneLoad**：`progress`（0..1；`allowSceneActivation=false` 时封顶 0.9——Unity 同款）/ `isDone` / `allowSceneActivation`（默认 true）/ `completed` 事件 / `GetAwaiter()`（可 await）——Unity AsyncOperation 同构。
- **游戏侧重初始化归游戏侧**（引擎不越权把 Awake/Start 异步化——Unity 也不）：模板给初始化器样例 = `sceneLoaded` 后分帧铺 spawn（每帧 N 个，防激活帧一次性 spawn 500 NPC 卡帧）+ 加载屏进度 = 引擎段×权重 + 自报游戏段混合；加载屏 = code-mounted RmlUi 文档（origin=CSharp 跨场幸存）+ 原生 `<progress>` 元素（③d-1 先例）。加载期间旧场景照常 tick（Unity 同款；要冻结自己 Time.Scale=0）。
- **确定性契约（唯一真设计张力）**：分帧完成时机依赖帧预算，跨机器不确定。定契约为——**激活帧 = 回放流里记录的确定性事件；中间 progress 为纯呈现量，玩法逻辑只许挂 sceneLoaded/isDone/completed，禁挂 progress 分支**（ADR 响亮规则 + 契约测试；无机器强制，登记已知敞口）。回放走同步路径 + 记录激活帧。

> **批⑧ 落地注（2026-10-08）**：Build 段建进暂存 registry（主世界逐位不动 = 加载帧哈希流全等的结构基础）；激活帧原子集成按台账复刻同步路径槽位分配序列 ⇒ 异步/同步激活句柄逐位一致（孪生用例机械证明，03 §12）。Resolve/Assets v1 = 占位段（Assets 承担 DOM 分帧回收；GUID/SpriteRef 归一留在激活帧 afterBuild——脚本实例必须活在最终句柄上，预备段无宿主依赖工作可做，压测数字落账后如需再升 stageResolve 钩子）。单槽统一跨同步/异步（新请求 WARN 取代在途者，被取代 op completed 不推）。

### D4 寻址：路径 > 唯一 stem > 响亮失败；无 build index

解析序：项目相对路径（`"Scenes/Forest.scene"`）精确命中 → 唯一文件名 stem（`"Forest"`，Unity 式便捷）→ 不唯一/缺失 = 红字响亮失败。**不做 Unity build settings 场景清单**（Unity 最著名的脚手架坑）——场景即资产，包内按 GUID 编目；`ResolveEntryScene` 泛化为 `ResolveScene`（packager 校验面同步）。GUID 重载 = v2 后手（与 prefab GUID 生态对齐）。

### D5 DDOL 非根调用：WARN + 作用于根树（Unity 兼容）

Unity 行为 = 标记子对象时整棵根树幸存；Lemon 同款 + WARN 交底（防静默惊喜）。根树重标签 = 遍历 Hierarchy 子树打 DDOL 位。重复管理器防重（Unity 经典坑）= 游戏侧 `if (instance == null)` 惯例（模板示例），引擎不自作聪明造单例基类。

### D6 Time.Scale 跨场保留；Audio.Paused 换场强制清

`Time.Scale` 不自动清零（Unity `timeScale` 全局跨场同款），游戏/模板显式管。`Audio.Paused` 相反：**换场时引擎强制清 false**（GameFlow 静音 bug 的类型级根治；Unity 无对应物，Lemon 特有面）。

### D7 DontDestroyOnLoad 落点：LemonBehaviour public static

C# 静态成员可经派生类名访问 → 脚本内裸调 `DontDestroyOnLoad(gameObject)`，Unity 手感一致；不造 `Lemon.Object` 类（避与 `System.Object` 遮蔽冲突）。

### D8 namespace：flat `Lemon.SceneManager`

门面惯例（UI/Audio/Save/Instantiate 同款）；不建 `Lemon.SceneManagement` 子命名空间。

## 换场帧协议（v1 Single 同步路径；async 同协议加分帧门）

```
C# 某帧 Update 调 SceneManager.LoadScene("Forest")
  → op 入队（vtable 尾加）→ 当帧照常跑完
→ 下一帧 Essential 段（结构命令应用点，Awake/OnEnable 同段）执行换场：
 ① 旧场实体逐个 OnDestroy（既有销毁通知路径；序 = 逆创建序定死——Unity 不保证序，Lemon 保证 = 确定性。**2026-10-08 批⑥b 修订**：实现取通知管线的池序——确定序同保（回放两侧同源），"逆创建序"措辞按确定性意图收口，改池序 = 金回放重录红线；落账 [03 分册 §2](../EngineDesign/03-ECS-Runtime.md) 落地注②）
 ② DDOL 标记的根实体连同子树重标签（D5：根树整体幸存）
 ③ 旧场非标记实体两阶段销毁 + 提交；随行清扫：
    tween 随实体自清（既有）· FxChannel 非实体附着物整场清（消灭"常驻实体逐个 KillAll"）
    · UiDocOrigin==Scene 文档自动卸载（既有过滤器，R10 兑现）· 实体声部随死自停（既有）
    · Audio.Paused 强制清 false（D6）
 ④ SceneArchive::Load 装新场（打 membership）→ GUID 归一 → SpriteRef 解析 → 脚本 Resolve → Awake/OnEnable（既有链）
 ⑤ 事件推送（对齐 Unity 时序）：sceneUnloaded（①③ 收口后）→ sceneLoaded（Awake/OnEnable 后、Start 前）→ activeSceneChanged（active 指针拨动时）
 ⑥ 本帧 Start → Update 照跑（Unity：装载当帧新对象即参与 Update）
```

## 跨场景全局量策略

| 项 | 策略 | Unity 对齐 |
|---|---|---|
| Time.Scale | 保留，游戏显式管 | timeScale 全局同款 |
| Audio.Paused | 换场强制清 false | 无对应物（Lemon 特有，防静音类 bug） |
| BGM 全局单槽 | 续播；游戏显式停 | DDOL 音乐管理同效果 |
| 存档三档 | 项目级保留（既有） | 同 |
| 资产库 | 常驻不卸载（无引用计数；内存上限游戏侧自负，文档写明） | Unity Resources 卸载之别 |
| C# 静态/域 | 跨场存活（既有） | 同 |
| TweenTable/FxChannel 实体附着 | 随实体死自清（既有/补整场清） | 粒子随对象销毁同款 |
| PrefabCache | 常驻 | 同 |

## C# API 面（批⑦ 落地；命名逐项对齐 Unity）

```csharp
public static class SceneManager {
    public static void LoadScene(string nameOrPath);
    public static void LoadScene(string nameOrPath, LoadSceneMode mode);          // Additive = 红字未实现（D2）
    public static AsyncSceneLoad LoadSceneAsync(string nameOrPath, LoadSceneMode mode = LoadSceneMode.Single);
    public static Scene GetActiveScene();
    public static void SetActiveScene(Scene scene);                               // 实例化落点随之（Instantiate.Prefab）
    public static int  sceneCount { get; }
    public static Scene GetSceneAt(int index);
    public static Scene GetSceneByName(string name);   public static Scene GetSceneByPath(string path);
    public static event Action<Scene, LoadSceneMode> sceneLoaded;
    public static event Action<Scene>               sceneUnloaded;
    public static event Action<Scene, Scene>         activeSceneChanged;
}
public enum LoadSceneMode { Single, Additive }
public readonly struct Scene {                                                    // 无 buildIndex（D4）
    public int Handle;  public string name;  public string path;
    public bool isLoaded;  public bool isValid;  public int rootCount;
}
public readonly struct AsyncSceneLoad {                                           // Unity AsyncOperation 同构
    public float progress;  public bool isDone;
    public bool allowSceneActivation { get; set; }                                // false 时 progress 封顶 0.9
    public event Action<AsyncSceneLoad> completed;
    public System.Runtime.CompilerServices.TaskAwaiter GetAwaiter();
}
// LemonBehaviour 上（D7）：
public static void DontDestroyOnLoad(GameObject go);                              // 仅根生效；非根 = WARN + 作用于根树（D5）
```

## 验收映射（M7c 批⑥–⑩；总览页登记）

- **批⑥ 引擎核心**（membership/场景档案/换场协议/StateHash 分组/回放扩展）：smoke-scene 单跳 + 单测 + 回归 full + bench 门禁不降 + 金回放零重录验证。**开工首查项 = 全 `Scene&` 调用面盘点**（系统/提取/编辑器/夹具）+ membership 哈希流口径定案。
- **批⑦ SDK 门面**（SceneManager/Scene/AsyncSceneLoad/DontDestroyOnLoad/三事件 + vtable 尾加）：smoke-scene 四跳全链——每跳断言：旧场 membership 实体归零 / DDOL 幸存者精确清单且句柄不变 / origin=Scene 文档卸载数 = 预期 / origin=CSharp 文档存活 / Audio.Paused==false / 事件序 sceneUnloaded→sceneLoaded→activeSceneChanged / StateHash 分组稳定 / 零 membership 孤组。script-tests 扩。
- **批⑧ LoadSceneAsync**（分帧状态机 + AsyncSceneLoad + 加载屏样例 + 回放契约用例）：大场景分帧压测（预算实测入 DevLog）+ progress 单调、0.9 封顶、completed 恰一次 + 回放激活帧逐位一致（async 装载、同步回放）。**✅ 2026-10-08 机器面**：20k 实体×4 组件压测（staged 帧 ≤4ms+块容差 / 激活帧 ~13ms / 同步对照 ~370ms，DevLog）+ TestSceneAsyncMachineContract（契约/门控/失败/单槽）+ TestSceneAsyncReplayFrameContract（孪生逐帧哈希全等 + 句柄集合相等）+ script-tests TestSceneAsyncSdk（progress/门控 0.9/await 域线程续跑/completed 恰一次/取代取消/Additive 无效 op）+ scene-smoke 第 5 跳（宿主端到端，门控变体）+ 模板加载屏样例（LoadingScreen.cs + loading.rml）。
- **批⑨ 消费者迁移**：svr-test 拆多场景（MainMenu + Grass/Volcano）+ **RunSweeper 删除** + GameFlow 瘦身行数对比入 DevLog（价值主张量化账）+ vs-survivor 模板随迁 + 真人走查 + 回归 full。
- **批⑩（可选）编辑器打磨**：Play 态 Hierarchy 场景组显示 + DDOL 徽标 + i18n 词条。

## Non-goals（v1 砍单，防 scope 膨胀）

Additive/UnloadSceneAsync 实现（D2 预留）· 多场景编辑态（编辑器 additive 编辑）· 运行时 CreateScene · Unity 式 build index 清单 · 资产按需装载/LRU（Assets 阶段占位即未来接入口）· Lighting per-scene（3D 事项）。

## 后果与风险

- **Scene 语义重构波及面**（批⑥ 主体工作量）：SceneArchive 装载打标/保存过滤（编辑态全 registry 默认 = 向后兼容）/ StateHash 分组 / SpatialHash 共享语义；03 分册 §2 注记随批⑥ 同步。
- **progress 分支违约敞口**：契约测试 + ADR 响亮规则，无机器强制——已知登记（与 Unity 同级敞口）。
- **membership 漏标孤组**：装载/实例化单点打标 + smoke 零孤组断言。
- **回放金档**：vtable 尾加 + 组件 id/系统序零变动 → 零重录预期（批⑥ 验证；membership 哈希流口径若致重录，批文件落账并重录一次）。
- **档1 知识不废**：单场景内重置（RunSweeper 式局内清场）仍是合法模式；档2 提供的是粗粒度换场 + 引擎保证的清场承诺，两者互补。
