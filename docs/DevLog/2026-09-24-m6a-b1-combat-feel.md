# 2026-09-24 M6a 批①：表现与打击感（Animator Play/Queue/CrossFade + 飘字/世界血条）

[批文件](../Plans/M6a/2026-09-24-b1-combat-feel.md)（分解与验收判据全文）·
[08 §2 M6a](../EngineDesign/08-Development-Roadmap.md) · 关联：[03 §8.1](../EngineDesign/03-ECS-Runtime.md)
（换段语义）、[04 §3](../EngineDesign/04-CSharp-Scripting.md)（Anim/Fx 面）、
[06 §8](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（恒定原则落账）

## 交付面

- **A 线动画状态控制**：`Animator2D` 尾加换段队列三字段（`nextClipId/fadeRemain/
  nextLoop`，FIELD_RT——入哈希不入档，16→28B 双侧镜像/探针同步）；AnimatorSystem
  三切点规则（Queue 收尾/回绕点切、CrossFade 倒计时切、暂停全冻、目标未命中
  warn-once 丢队列、无 clip 表整体旁路）；SDK `Lemon.Anim`（Play/Queue/CrossFade/
  Pause/Resume/IsPlaying/Queued/ClipId）**纯字段读写零 C ABI**（clipId = GUID
  低 32 位客户端自算）。受击组合拳 = `Play(hit, loop=false) + Queue(walk)`。
- **B 线世界空间表现**：World 级 `FxChannel`（RtUi 同款第三呈现通道，不入
  StateHash）——飘字池 256 环形 + 血条槽 128 实体键控（最老者淘汰，03 §10
  飘血数字预列项落账）；GameView 消费恒走 sprite 管线（飘字 = 内置位图字体页
  层 252 文本段，血条 = 白精灵双四边形层 251 精灵段，Simulate 渲染帧 dt 粒子
  先例）；SDK `Lemon.Fx`（Text/Bar）经 vtable 尾加 `fxPopup/fxBar`（旧宿主判空
  降级）。模板 PlayerCombat 接线（Hit 订阅 → 受击切段 + 飘字 + 血条）。

## 实测数字

| 判据 | 结果 |
|---|---|
| ctest 3/3 | engine-tests **13263**（+118：TestVerifyAnimatorQueue 九组 + TestVerifyFxChannel 六组）；script-tests **1551**（+91：TestAnimFxSdk 五帧链 + 类型表 12）；布局探针 Animator2D 28B 双侧一致 |
| 金回放零重录 | m5b2 三档原样 `--replay` **mismatches=0**（Animator2D 尾加 × 零实例——09 §6.8 推论第二次兑现，先例三复证） |
| smoke-anim（120 帧） | `prog(maxFrame=3 slice=YES) queue(hit=YES back=YES) fx(text/bar=YES) => OK`（anim 链内扩断言，回归不增步） |
| editor-regression full | **PASS=14 FAIL=0**（批⓪ 时 13/14——本轮 smoke-ui 偶发未复现） |
| smoke-template | `hitClip=YES fx(text=YES bar=YES)` 加入 tplOk => OK（脚本面端到端：Hit 事件 → Anim 切段 + Fx 通道，验收③ 模板可复现） |
| bench-survivor（fx 饱和） | alive=10436、anim 10002/10002、**fx(texts=256 bars=128 饱和)**、frameAvg 12.76ms **fps=78 PASS**（判据 ≥45；sim 10.30 / scene 1.51——fx 渲染并入 scene 段 +0.1~0.2ms，09 §6.10 台账行） |

## 实施记录与偏差

- **CrossFade 倒计时减到负值曾被误判 Queue 分支**（按符号分流）——首版实现
  `fadeRemain -= dt` 减穿零后落入 `<0` 走"等收尾"路径，测试③ 抓出（5.5 tick 半
  余量写法）；修正为先取模再减。**教训：模式字段与数值字段共享存储时，分支判定
  必须取减前状态**。同因：两条倒计断言（引擎 ③⑤）踩 3dt−3dt 浮点不归零——测试
  写法改半 tick 余量（5.5dt/3.5dt），非实现问题。
- 分解偏差一处：`Anim.IsFinished` 原计划 SDK 面提供，实施发现 SDK 侧无 clip
  total 可判（fps/帧数在引擎表）——伪 API 撤掉，改 `Queued`（在途队列可观测）+
  IsPlaying 收窄为暂停开关语义；"播完"的可观测证据 = 排队段已切（clipId 变化）。
- smoke-anim 帧数下限 60→120（切段链 frame 60 起才注入）；回归脚本步内 --frames
  120 本就满足，步数不变。
- 模板 PlayerCombat 改动同步进了生成器内嵌源码（WriteGameSources 直写 Game/
  文件——只改入库文件会被下次重生成覆盖）；重生成 diff 复核 = 实体 guid/时间戳
  逐运行随机面外逐字节一致 + hit clips/PlayerCombat 增量。

## 提交前 review 轮（2026-09-24）

- **颜色通道序陷阱（真 bug，已修）**：`SpritePacket.colorBits` 是 **RGBA 序**
  （`r|g<<8|b<<16|a<<24`，同 `SpriteRenderer.colorRGBA`），而 `Lemon.Ui` 惯例色是
  **ABGR 序**（ImGui PushStyleColor 语义）——原稿把 Ui 惯例暖红 `0xFFFF6050`
  直搬进 Fx（该值在 RGBA 下是蓝紫；实际上它在 ABGR 下也是蓝——两序皆错）。
  全量（模板/生成器内嵌/smoke-anim/bench/TestScript 探针/script-tests 断言）
  改为真暖红 `0xFF5060F0`（r F0/g 60/b 50）；`FxChannel.h` 与 `Lemon.Fx` 注释
  显式声明色序并警示"勿与 Ui 的 ABGR 常量互拷"。怪血条 `0xFF30B0F0`/玩家绿
  `0xFF60D060` 在 RGBA 下恰为红/绿，保留。
- 飘字锚点文档修正：`FxText.x/y` 语义 = **首字符中心**（DrawText pos 同 overlay
  quad 中心约定，文本向右延伸），原注释"左下"不准。
- FxChannel 环形/键控池不变式复核（环写位=窗口末端、淘汰序=插入序、满池覆写
  最老槽）；Bake 三段层序契约（精灵 ≤250 → 血条 251 → 文本 252）与既有层位
  约定一致性确认。修复后 ctest 3/3 + smoke-anim/smoke-template 复跑全绿。

## 观察项（登记不改码）

- **smoke-anim 模式下 `editor-smoke play-roundtrip byte-exact=NO` 为存量问题**：
  基线（批⓪ commit 5437279，本批改动全 stash）同样复现——与本批无关；anim 链
  步骤的 grep 判据（`smoke-anim: .* => OK`）不受影响（回归 14/14 实证）。归档
  待查：tempdir 项目路径下 Stop 恢复比对失败的具体漂移面。
- 自定义数字图集（yami 风大数字）无消费者不预做；飘字/血条参数（寿命/上浮/
  宽度）v1 常量，手感调参诉求出现时尾加 `fxPopupEx`。
- GUI 手工走查（GameView 飘字/血条视觉：字体放大锯齿、血条压怪层次）留用户
  真人验收——断言链已覆盖机制面（与批⓪"真人验收两件"同口径）。
