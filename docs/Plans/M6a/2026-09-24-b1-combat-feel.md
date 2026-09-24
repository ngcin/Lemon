# M6a 批①：表现与打击感 —— Animator Play/Queue/CrossFade + 位图数字飘字/世界血条

Status: done（2026-09-24 勾销；[DevLog](../../DevLog/2026-09-24-m6a-b1-combat-feel.md)）

> **验收落账（§3 判据逐条）**：① ctest 3/3 ✅（engine-tests **13263** +118、
> script-tests **1551** +91、布局探针 28B 双侧）；② m5b2 三档金回放零重录
> mismatches=0 ✅（Animator2D 尾加 × 零实例——09 §6.8 推论第二次兑现）；③
> smoke-anim 扩展 PASS（queue hit/back=YES、fx=YES）+ editor-regression full
> **14/14**（不增步，anim 链内扩）✅；④ smoke-template `hitClip=YES fx(text/bar
> =YES)` 入 tplOk => OK（验收③ 模板可复现）✅；⑤ bench-survivor **fps=78 PASS**
> + `fx(256,128 饱和)`（验收④；sim 10.30 / scene 1.51 台账入 09 §6.10）✅；
> ⑥ 文档回写 03/04/06/08/09 + DevLog + 本页 ✅。
> 实施偏差两处：CrossFade 首版倒计减穿零误入 Queue 分支（测试③ 抓出，先取模
> 再减修正）；`Anim.IsFinished` 原计划撤掉（SDK 无 clip total 可判——伪 API 改
> `Queued`）。存量观察：smoke-anim 模式 play-roundtrip byte-exact=NO 为基线
> 复现（与本批无关，DevLog 登记）。

> 拆分自 [M6a 总览](./M6a.md)（08 §2 M6a WBS 第 2 条）。两条腿：**A 线 = 动画状态控制**
> （受击/攻击/死亡 Play 中可切——验收③）；**B 线 = 世界空间表现通道**（飘字/血条恒走
> sprite 管线、池化——06 §8 恒定原则，验收④）。动工前必读：[03 §8.1](../../EngineDesign/03-ECS-Runtime.md)
> （帧映射语义）、[03 §10](../../EngineDesign/03-ECS-Runtime.md)（池化纪律——"飘血数字"
> 已预列）、[06 §8](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（恒定原则）、
> [09 §6.8](../../EngineDesign/09-Testing.md)（加字段零重录推论，批⓪ spriteGuid 先例）。

## 1. 现状盘点（2026-09-24 逐行核对）

### 1.1 A 线：Animator 只会"自动播"，不会"切"

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| A | `Animator2D` 16B 布局冻结，六字段齐备；clipId 语义 = clip 资产 GUID 低 32 位 | `RenderComponents.h:27-34` | 尾加字段可行（见 §2 决策 1） |
| B | AnimatorSystem 只做 time 推进 + 帧映射；`playOnStart=0` = 暂停开关（M5 无 Play API 注记原文"归 M6a 批①"） | `Systems.cpp:761-797`、`03:203` | 无换段机制：受击/攻击/死亡无法打断当前段 |
| C | 三金档基准场（m5b2 sim-mt/st/script）零 Animator2D 实例（批③ §18 核对） | M5 批③ §18 | 尾加字段零重录的机械前提成立 |
| D | C# 镜像 16B + 探针行同步在案 | `Components.cs:77-86`、`LayoutTables.cs:111-118` | 尾加后 28B 双侧同步（机械） |
| E | clipId 可客户端自算：`clipId = (uint)guid`（低 32 位直取），无 SpriteOfGuid 式查表需求（spriteId 是登记号才需要；clip 不是） | `ClipTable.h:6-7` | SDK Play/CrossFade 可**零 C ABI**实现（纯 Read/Write） |
| F | 模板 Mob.prefab/玩家已带 Animator2D（monster-walk/hero-walk clip） | `vs_template` `EditorApp.cpp:893/944` | A 线模板接线只差 hit clip + 脚本切段 |
| G | Hit 事件 payload：弹道 Hit `[0]=伤害 [1][2]=位置`（`Systems.cpp:582-588`）；Hazard Hit 只有 `[0]=伤害`（`:650-654`）——dst 实体可读 Transform 补位 | `Events.h` | 飘字数据源齐备 |

### 1.2 B 线：世界空间 HUD 无通道（HUD 只在 ImGui 叠层）

| # | 现状 | 锚点 | 含义 |
|---|---|---|---|
| H | World 级呈现通道先例 ×2：`RtUiChannel`（8 槽 HUD）+ `RtUiCards`——不入 StateHash、EnterPlay 新建 World 自清零、C# 经 vtable 写 | `World.h:30-68`、`ScriptHost.cpp:120-159` | 飘字/血条通道同款第三通道 |
| I | **位图字体页已在**：`BitmapFont`（5×7×95 字形烘焙独立图集槽 1）+ `DrawText` 产 SpritePacket（文本段，同帧多段同键合批） | `Renderer/BitmapFont.h`、`ViewportRenderer.cpp:466`（SceneView 实体名标签即世界空间用例） | "位图数字页"= 复用内置字体页，零新资产面 |
| J | 白精灵四边形先例：`PushOverlayQuad`（WhiteSprite + scale=世界像素边长 + 层/序 sortKey） | `ViewportRenderer.cpp:484-501` | 血条 = bg+fg 两四边形，同款数学 |
| K | SpriteBatcher.Bake 三段：精灵/粒子/**文本**（段间层序递增不并批） | `SpriteBatcher.h:31-34` | 飘字走文本段、血条走精灵段，零渲染器改动 |
| L | 粒子 = 表现层模拟先例：渲染帧 dt、非确定可接受、不入回放、CPU AoS 池 swap-and-pop、预算封顶 | `Particles.h:1-9` | 飘字/血条的 Simulate 同语义 |
| M | GameView 渲染路径：`RenderViewport(idx=1)` 无 overlay/无文本——fx 注入点 | `ViewportRenderer.cpp:414-481` | 进 Play 时读 `ctx.ActiveWorld().Fx()` 产包 |
| N | 层位现状：精灵 0..N < 粒子 250 < 字体 254；`SortingOverride`（层序覆盖）已注册未消费 | `RenderComponents.h:23`、`BitmapFont.h:23` | 血条 251 / 飘字 252（压精灵与粒子、让位 UI 文本） |
| O | bench-survivor 证据链先例：RESULT 行判据 + PASS 组合（anim/hazard 同款）；C++ harness 可直写通道（零跨界成本） | `EditorApp.cpp:3878-3958` | 验收④ 的压测口径挂这里 |

## 2. 设计决策（本批定案，违者走 ADR）

1. **Animator2D 尾加换段队列三字段（FIELD_RT，16→28B）**：
   ```cpp
   uint32_t nextClipId = 0;  // 0 = 无队列；目标 clip（GUID 低 32 位）
   float fadeRemain = 0.0f;  // <0 = Queue（收尾/回绕点切）；>0 = CrossFade 倒计时
   uint16_t nextLoop = 1;    // 切换时写入 loop（权威在实体，切换点更新）
   uint8_t _pad[2] = {};
   ```
   FIELD_RT（`ComponentRegistry.h:42`：入状态哈希不入档）——队列是运行时状态、无需
   序列化，但影响 curFrame/spriteId 演化必须入哈希（未来含 Animator 的金档确定性）。
   **零重录不靠新哈希旗标**：三金档零 Animator2D 实例（盘点 C）→ 字段迭代从不发生
   → 哈希流逐字节不变（09 §6.8 推论，批⓪ spriteGuid 同款先例）。C# 镜像
   `Components.cs` + `LayoutTables.cs` 探针同步 28B。**禁区：不得给基准场播
   Animator2D**（bench-sim/bench-script 场景构造零改动即满足）。
2. **换段语义（引擎侧，仅 clip 表命中时消费；三条切点规则）**：
   - **先推进、后判定**：本 tick 先按现行算术推进 time（含 loop 回绕减法、非 loop 钳
     total），再判定切换；切换 = `clipId=nextClipId; time=0; loop=nextLoop; nextClipId=0;
     fadeRemain=0` + 新段首帧当帧生效（curFrame=0、sr.spriteId=frames[0]）。
   - **Queue（fadeRemain<0）**：非 loop 当前段收尾（time 钳 total）即切；loop 段在回绕
     点（本 tick 发生了 period 减法）切——受击组合拳 = `Play(hit, loop=0) + Queue(walk)`。
   - **CrossFade（fadeRemain>0）**：每 tick `fadeRemain -= dt`，到 0 即切；非 loop 当前段
     提前收尾也即切。**帧动画无姿态混合**——CrossFade = 倒计时切段（视觉等效"干净
     收尾再切"），命名保留 Unity 心智、04 §3.2 对齐清单声明此差异。
   - **暂停冻结整个 Animator**：playOnStart=0 时 time/curFrame/spriteId 三态冻结
     （M5 语义不变）+ 队列倒计时与切点判定同冻（暂停 = 冻结，无一例外）。
   - **无 clip / clipId 未命中（M2 路径）**：队列机制整体旁路（字段不消费不动）——
     M2 逐位不变；**nextClipId 未命中 clip 表 = warn-once 丢队列**（防拼写错误的
     静默 M2 回退——与 clipId 未命中走 M2 的宽容不同：队列是显式指令，目标不存在
     是作者错误）。
3. **SDK `Lemon.Anim` 纯字段读写，零 C ABI 改动**（盘点 E）：
   ```csharp
   Anim.Play(GameObject, uint clipId, bool loop = true)   // 立即切段（同段 = 重播，清队列）
   Anim.Queue(GameObject, uint clipId, bool loop = true)  // 当前段收尾/回绕点切（fadeRemain=-1）
   Anim.CrossFade(GameObject, uint clipId, float fade, bool loop = true) // fade<=0 ⇒ Play
   Anim.Pause(GameObject) / Resume(GameObject)            // playOnStart 0/1
   Anim.IsPlaying(GameObject) / IsFinished(GameObject)    // 非 loop 且 time>=total = 播完
   Anim.ClipId(string guidHex)                            // 16 hex → 低 32 位（自算）
   ```
   全走既有 `Native.Read/Write<Animator2D>`（低频语法糖口径同 GameObject 门面）。
   Play 覆写 nextClipId=0（打断在途队列）——受击优先于一切在途切换。
4. **FxChannel = World 第三呈现通道（RtUi 同款，不入 StateHash）**：
   - **飘字池**：定长 256 环形槽（满 = 最老者淘汰——03 §10 纪律），条目
     `{char text[16]; float x,y; uint32 color; float age;}`，寿命 0.8s、上浮 24px、
     末 30% 淡出（Simulate 常量曲线，渲染帧 dt——粒子先例，非确定可接受）。
   - **血条槽**：按实体键控 128 槽，命中覆写（frac/color/age=0/sticky 重置），默认
     sticky 3s（受击显伤条自隐；玩家条每帧刷新即常显）；实体死亡由渲染侧跳过（不
     提前释放，等 sticky 自然过期或覆写）。
   - `PopupText(text,x,y,color)` / `Bar(entity,frac,color,width=32)` / `Simulate(dt)` /
     `Clear()`；EnterPlay 新建 World 自清零（RtUi 同机制，零额外接线）。
   - **血条产包在通道内**（`ExtractBars(out, whiteSpriteId, resolvePos)` 纯数学，可单测）；
     **飘字产包在视图侧**（字形表是 BitmapFont 的 GPU 态，引擎测试无 GPU——通道只存
     状态，ViewportRenderer 逐条 DrawText）。
5. **vtable 尾加 2 项（安全降级同 RtUiSet）**：`fxPopup(text,x,y,color)`、
   `fxBar(entity,frac,color,width)`；SDK `Lemon.Fx.Text(string/float, Vec2, color)` /
   `Fx.Bar(GameObject, frac, color, width=32)`。旧宿主 = null 判空丢弃。
6. **渲染消费只进 GameView**（SceneView 是编辑视角，不掺游戏表现）：`RenderViewport
   (idx=1)` 且 `ctx.Playing()` 时——通道 Simulate（成员自计时 clamp [0,0.1]s）→ 视口
   剔除（+64px 边距）→ 飘字 `Font().DrawText(...)` 层 252（charScale=1 世界像素，
   锚点左下 + rise(age)）入文本段；血条 bg（暗色 w×4）+ fg（color·frac×4，左锚）
   白精灵四边形层 251 入精灵段（`all` 向量 append）。恒走 sprite 管线零额外 pass。
7. **位图数字页 = 复用内置 5×7 字体页**（盘点 I）：数字/短文本（伤害/治疗/+25%）
   16 字符内，charScale=1 世界像素 1:1。自定义数字图集（yami 风大数字）无消费者
   不预做——登记后续（出现手感诉求再尾加，接口不占位）。
8. **验收④ 压测口径 = 池满饱和**：bench-survivor 帧循环内 C++ 直写通道至饱和
   （每帧 256 飘字 + 128 血条全量在场 = 最坏情形），RESULT 增 `fx(texts, bars)` 证据，
   PASS 增 `fxOk`（饱和实证）——≥45fps 门槛不动即验收④ 落账。

> **拍板记录（2026-09-24）**：①CrossFade 语义取"倒计时切段"而非渲染层双精灵 alpha
> 混合——帧动画无姿态混合是本质限制，双 Renderable/实体代价不成比例，先交付切段
> 语义（受击/死亡场景 Play+Queue 组合已闭环）；②hit/attack 视觉段用 yami 表尾帧
> 组合（hero cells[8,7]/monster cells[7,6] @12fps loop0）——美术级受击动画随用户
> 游戏素材库走，引擎侧只验机制；③飘字/血条参数（寿命/上浮/宽度）v1 取常量，
> 不上 API 面——手感调参诉求出现时尾加 `fxPopupEx`，不预做配置面。

## 3. 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿（引擎 + 布局探针 28B + script-tests），新增用例见 T1/T2/T3 所列；
2. **三档金回放零重录**：m5b2 金档原样 `--replay` mismatches=0（决策 1 机制保证；
   09 §6.8 推论第二次落地）；
3. `--smoke-anim` 扩展 PASS：hit clip + 帧循环直写 Play(hit)+Queue(walk) → 断言切段
   发生（clipId→hit）→ 收尾回切（clipId→walk && time 重归零区间）+ fx 通道计数/
   上浮/过期断言；`tools/editor-regression.sh full` 13/13（不新增步——anim 链内扩）；
4. `--smoke-template` PASS 不回归 + 新增证据：mobHitClipSeen（怪 clipId==hit 段采样
   帧 >0）+ fxTextSeen/fxBarSeen（通道计数>0 采样帧）——验收③"模板内可复现"主体；
5. `--bench-survivor --frames 900`：≥45fps（门槛不动）+ `fx(256,128)` 饱和证据
   （fxOk）——验收④；anim/hazard/director 既有判据不回归；
6. 回写：03（§3.3 行注 + §8.1 换段语义 + §10 飘血数字落账）、04（§3 Anim/Fx 面 +
   §3.2 CrossFade 差异声明）、06（§8 恒定原则落账注记）、08（批① 状态）、09
   （测试计数 + §6.10 台账行 + 验收④ 数字）、DevLog 条目、本页勾销、总览表更新。

## 4. 确定性与回放影响

- **Animator2D 尾加 FIELD_RT**：三金档零实例 → 哈希流不变（§2 决策 1；红线 = 基准
  场不得播 Animator2D）。换段规则纯函数化（收尾/回绕点/倒计时到零），无 RNG 消费，
  子流占用不变；time 钳制/回绕算术不动——既有帧映射逐位不变（新增判定在既有路径
  之后、且 nextClipId=0 时零副作用）。
- **FxChannel 不入 StateHash**（ComputeStateHash 只读 Scene，World 级通道天然排除
  ——RtUi 先例）；C# Fx 写发生在脚本 tick 内（确定性时序）但产物纯呈现；Simulate
  渲染帧 dt 非确定——回放/录像均不感知。
- **vtable 尾加**：旧 Entry 无新导出 = SDK 判空调用安全降级（RtUiSetEx 同款先例）；
  NativeApiVtable 与 NativeApi.cs 两侧同步改（表尾追加零扰动既有项）。
- **模板 PlayerCombat 增 Hit 订阅**：脚本行为变化只影响模板 Play 场景（非金档面）；
  smoke-template 断言集只增不减。

## 5. 任务分解（T1→T6 依序；A/B 两线在 T1/T2 后可交错）

### T1 引擎：Animator2D 换段队列 + AnimatorSystem + 引擎测试 —— 约 1 天

- `RenderComponents.h`：尾加三字段（决策 1 布局，28B static_assert 16→28）；
  `ComponentCatalog.cpp` kAnimator2D 加三行 FIELD_RT（nextClipId/fadeRemain/nextLoop）
  + ED_HIDE（队列是运行时指令态，作者面不手编——灰显都省）；
- `Systems.cpp AnimatorSystem`：决策 2 三切点规则（先推进后判定；Queue 收尾/回绕点、
  CrossFade 倒计时、暂停全冻、M2 旁路、nextClip 未命中 warn-once 丢队列——warn 位
  挂系统成员，SystemPipeline 每局重建 World 即复位）；`Systems.h:163` 注释更新；
- C# 镜像：`Components.cs` Animator2D 尾加三字段（+2B pad 衬齐 28B）+
  `LayoutTables.cs` 探针三行；
- 测试（`tests/engine_tests.cpp`）：`TestVerifyAnimatorQueue` 九组——Queue 非 loop
  收尾即切（切后 curFrame=0/spriteId=新段首帧）、Queue loop 回绕点切、CrossFade
  倒计切（fade=2 tick）、CrossFade 期间非 loop 收尾提前切、暂停冻结队列（fadeRemain
  不减/切点不触发）、Play 清在途队列、nextClip 未命中丢队列、无 clip 表队列旁路
  （M2 逐位锚——time 推进与无字段时逐位一致）、孪生世界 300 tick StateHash 相等
  （含队列态）；`TestVerifyAnimatorFrameMapping` 复跑（既有九组不回归）。

### T2 引擎：FxChannel + 单测 —— 约 0.75 天

- 新 `Engine/ECS/FxChannel.{h,cpp}`（CMake 源表追加；纯数据零 GPU 依赖）：
  `FxText{char[16]; float x,y; uint32 color; float age}`×256 环形 + `FxBar{uint64
  entity; float frac; uint32 color; float age; float width}`×128 键控；`PopupText/
  Bar/Simulate/Clear/Count`；`ExtractBars(out, whiteSpriteId, resolve(entity)→Vec2,
  viewRect)` 产 bg+fg 双四边形（层 251、序=槽序稳定；实体死/出视口跳过）；
- `World.h`：成员 `fx_` + `Fx()` 访问（RtUi 旁同款注释块——呈现层专用不入
  StateHash）；
- 测试：`TestVerifyFxChannel` 六组——飘字池满最老者淘汰（第 257 条覆写第 1 条）、
  Simulate 上浮/淡出/到期释放（age 曲线采样点断言）、Bar 命中覆写刷新（age 归零
  sticky 重置）、sticky 过期释放、计数正确性、ExtractBars 数学（frac 宽度/左锚/
  视口剔除/死实体跳过——白精灵 id 用假号纯断言 sortKey 与 scale）。

### T3 SDK + 桥：Anim.cs / Fx.cs + vtable 尾加 + script-tests —— 约 0.75 天

- `Lemon.SDK/Anim.cs`（决策 3 全 API；Play 清队列 + playOnStart=1 + time=0；Pause/
  Resume 只动 playOnStart；IsFinished 无 clip 表 = false）；
- `Lemon.SDK/Fx.cs` + `NativeApi.cs` 尾加 fxPopup/fxBar（判空降级）+
  `ScriptHost.h NativeApiVtable` 尾加两行 + `ScriptHost.cpp` 实现（g_world 窗口
  约定同 RtUiSet）；
- script-tests（`TestScript.cs` + C++ 断言侧）：`AnimFxProbeBehaviour`——帧1
  `Anim.Play(hit,loop:false)+Anim.Queue(walk)` 报 Custom 编码段；帧2 `Pause` 报
  playOnStart=0；帧3 `Resume+CrossFade(walk,fade=1tick)` 报队列态；帧4 `Fx.Text
  ("12")+Fx.Bar(self,0.5)`；帧5 读回 Animator2D 报 ClipId/FadeRemain 校验码后自毁；
  C++ 侧逐帧断言（含 World.Fx() 计数=1+1）。

### T4 编辑器：GameView Fx 消费 + smoke-anim 扩展 —— 约 1 天

- `ViewportRenderer.{h,cpp}`：`RenderViewport(idx==1)` 进 Play 读
  `ctx.ActiveWorld().Fx()`——成员自计时 Simulate（clamp [0,0.1]）→ 剔除 → 飘字
  `Font().DrawText` 层 252（锚左下 + rise·age，alpha 淡出乘色）入 textPackets 段；
  血条 `ExtractBars` append 进 `all` 精灵段（决策 6 布局/层位）；
- smoke-anim 扩展（不增回归步）：`WriteAnimSheetAssets` 加 `anim-hit.clip`
  （cells[3,2] @12fps loop0，固定 guid `kAnimHitClipGuid=0x5bd31a7c30000003`）；帧
  循环 frame==60 直写 AnimHero 字段（Play(hit)+Queue(walk) 等价写法）→ 采样断言：
  切段发生（clipId==hit 低 32 位曾为真）→ 收尾回切（clipId==walk && time < total）；
  同场景进 Play 后 C++ 写 Fx（PopupText("12")+Bar(实体)）→ 断言通道计数 + Simulate
  位置上移 + 过期清零；`--smoke-anim` 输出行增 `queue(...)` `fx(...)` 段；
- 手工核验一次（DevLog 记录 + 截图）：GameView 飘字/血条视觉（字体放大锯齿/血条
  压怪层次）。

### T5 模板 + bench：hit clip 资产 + PlayerCombat 接线 + 证据链 —— 约 1 天

- `vs_template` 生成器：`hero-hit.clip`（cells[8,7] @12fps loop0，guid
  `0x5bd31a7c20000003`）+ `monster-hit.clip`（cells[7,6]，guid
  `0x5bd31a7c20000004`）落 Assets + 拷贝清单（`EditorApp.cpp:825`）；重生成模板；
- `Templates/vs-survivor/Game/PlayerCombat.cs`：构造器 `Subscribe(Hit)`——dst 是怪
  （Team 1）→ `Anim.Play(dst,hit,loop:false)+Anim.Queue(dst,walk)` + `Fx.Text
  ($"{damage:0}", pos, 0xFFFF6050)`（弹道 Hit 用 payload 位置、Hazard 读 dst
  Transform）+ `Fx.Bar(dst, hp.Cur/hp.Max, 0xFF30B0F0)`；玩家受击 → `Fx.Bar` 常显；
  hit/walk clip 低 32 位常量进脚本（`Anim.ClipId` 自算注释）；
- smoke-template verdict 扩计数：`mobHitClipSeen / fxTextSeen / fxBarSeen`（帧循环
  采样，判据 4）；
- bench-survivor：帧循环 fx 饱和灌入（决策 8——弹道命中位采样 256 飘字 + 采样怪
  128 血条每帧刷新）+ RESULT `fx(texts=%u bars=%u)` + PASS 组合 `fxOk`；
- 09 §6.10 台账行：Animator（队列判定增量）与 fx 渲染段耗时。

### T6 全量验证 + 文档回写 + 勾销 —— 约 0.5 天

- 验证命令全跑（§7）+ 判据 1–5 逐条落账；
- 文档回写（判据 6 清单）+ DevLog 条目（含 T4 手工核验截图）+ 本页勾销 +
  `M6a.md` 总览表/08 §2 状态更新。

**合计约 5 个工作日。**

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| Animator2D 28B 拖大万怪池（bench 40KB→70KB 缓存面） | bench-survivor 判据 5 兜底（≥45fps 门槛不动）；若回归：队列判定短路（nextClipId==0 早退——实现本就这么写，成本 ≈ 一次分支） |
| CrossFade 无姿态混合被当 bug 报 | 04 §3.2 对齐清单显式声明 + API 注释；受击场景 Play+Queue 组合已闭环（决策拍板①） |
| 飘字 C# 高频写（每 Hit 一 call）跨界成本 | 命中事件频率 = 命中率（模板量级 ~20/s）低频安全；万怪级由 bench C++ 直写（本就不过桥） |
| 池满最老者淘汰丢关键飘字 | VS 品类伤害数字本就海量短命，淘汰序 = 时间序公平；容量 256（单屏可视数字远低于此） |
| 血条每帧解析实体 Transform 的渲染成本 | 128 槽上限 × O(1) 查找 = 微不足道；bench 判据实测兜底 |
| 帧循环直写字段（smoke-anim）与脚本写路径竞态 | 冒烟写在编辑器帧循环（脚本 tick 之外），同帧序固定——非真竞态；写法 = Anim.Play 字段等价（注释声明） |
| vtable 尾加破旧 Entry 共存 | 表尾追加零扰动（M5 批①/④ 两次先例）；SDK 判空降级 + script-tests 断言 |
| 28B 探针/镜像双侧失配 | LayoutTables 探针行机械同步；lemon-script-tests 双向校验兜底（既有纪律） |

## 7. 验证命令（批①完工口径）

```bash
cmake --build --preset mac --target lemon-tests lemon-script-tests lemon-editor
ctest --test-dir build/mac --output-on-failure
# 三档金回放零重录（判据 2；m5b2 金档不得重录）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
# 动画链（含 queue/fx 扩展断言）+ 回归全量
build/mac/Editor/lemon-editor --project <tmp> --smoke-anim --frames 180
tools/editor-regression.sh full build/mac                            # 13/13
# 模板链 + 压测（验收③④）
build/mac/Editor/lemon-editor --smoke-template --frames 3000
build/mac/Editor/lemon-editor --bench-survivor --frames 900          # ≥45fps + fx(256,128)
```
